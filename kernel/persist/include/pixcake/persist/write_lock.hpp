#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/fast_export_latch.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace pixcake {

// The main process registers the latch for a document path.  Other write
// entry points (preset, sync, layer and direct document commits) consult the
// same registry before touching the durable file.
void register_fast_export_latch(const std::string& path, const FastExportLatch* latch);
void unregister_fast_export_latch(const std::string& path, const FastExportLatch* latch);

class WriteScope {
 public:
  WriteScope(FileSystem& files, std::string path, const FastExportLatch* latch = nullptr);
  ~WriteScope();

  WriteScope(const WriteScope&) = delete;
  WriteScope& operator=(const WriteScope&) = delete;

  ResultCode code() const { return code_; }
  explicit operator bool() const { return code_ == ResultCode::Ok; }

 private:
  std::string path_;
  std::shared_ptr<std::recursive_mutex> mutex_;
  std::unique_lock<std::recursive_mutex> local_lock_;
  std::unique_ptr<FileWriteLock> file_lock_;
  bool owns_thread_lock_ = false;
  ResultCode code_ = ResultCode::StateConflict;
};

}  // namespace pixcake
