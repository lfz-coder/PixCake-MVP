#include "pixcake/persist/write_lock.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pixcake {
namespace {

struct PathState {
  std::shared_ptr<std::recursive_mutex> mutex = std::make_shared<std::recursive_mutex>();
  std::vector<const FastExportLatch*> latches;
};

std::mutex g_registry_mutex;
std::unordered_map<std::string, PathState> g_paths;
thread_local std::unordered_map<std::string, std::size_t> g_thread_depth;

PathState& state_for(const std::string& path) {
  return g_paths[path];
}

bool latch_enabled(const PathState& state, const FastExportLatch* explicit_latch) {
  if (explicit_latch != nullptr && explicit_latch->enabled()) {
    return true;
  }
  return std::any_of(state.latches.begin(), state.latches.end(), [](const FastExportLatch* latch) {
    return latch != nullptr && latch->enabled();
  });
}

bool any_registered_latch_enabled() {
  for (const auto& [path, state] : g_paths) {
    (void)path;
    if (latch_enabled(state, nullptr)) {
      return true;
    }
  }
  return false;
}

}  // namespace

FileWriteLockResult FileSystem::try_write_lock(const std::string&) const {
  FileWriteLockResult result;
  result.lock = std::make_unique<NoopFileWriteLock>();
  return result;
}

void register_fast_export_latch(const std::string& path, const FastExportLatch* latch) {
  if (path.empty() || latch == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  state_for(path).latches.push_back(latch);
}

void unregister_fast_export_latch(const std::string& path, const FastExportLatch* latch) {
  if (path.empty() || latch == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  const auto found = g_paths.find(path);
  if (found == g_paths.end()) {
    return;
  }
  auto& latches = found->second.latches;
  const auto item = std::find(latches.begin(), latches.end(), latch);
  if (item != latches.end()) {
    latches.erase(item);
  }
}

WriteScope::WriteScope(FileSystem& files, std::string path, const FastExportLatch* latch)
    : path_(std::move(path)) {
  if (path_.empty()) {
    code_ = ResultCode::StateConflict;
    return;
  }

  {
    std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
    mutex_ = state_for(path_).mutex;
  }
  local_lock_ = std::unique_lock<std::recursive_mutex>(*mutex_);
  const auto owned = g_thread_depth.find(path_);
  if (owned != g_thread_depth.end() && owned->second > 0) {
    ++owned->second;
    owns_thread_lock_ = true;
    code_ = ResultCode::Ok;
    return;
  }
  {
    std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
    const auto found = g_paths.find(path_);
    if ((found != g_paths.end() && latch_enabled(found->second, latch)) ||
        any_registered_latch_enabled()) {
      local_lock_.unlock();
      code_ = ResultCode::FastExportBusy;
      return;
    }
  }

  FileWriteLockResult locked = files.try_write_lock(path_);
  if (locked.code != ResultCode::Ok || locked.lock == nullptr) {
    local_lock_.unlock();
    code_ = locked.code == ResultCode::Ok ? ResultCode::StateConflict : locked.code;
    return;
  }
  file_lock_ = std::move(locked.lock);
  g_thread_depth[path_] = 1;
  owns_thread_lock_ = true;
  code_ = ResultCode::Ok;
}

WriteScope::~WriteScope() {
  if (!owns_thread_lock_) {
    return;
  }
  const auto found = g_thread_depth.find(path_);
  if (found != g_thread_depth.end()) {
    if (found->second > 1) {
      --found->second;
    } else {
      g_thread_depth.erase(found);
      file_lock_.reset();
    }
  }
  if (local_lock_.owns_lock()) {
    local_lock_.unlock();
  }
}

}  // namespace pixcake
