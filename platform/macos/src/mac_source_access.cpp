#include "pixcake/platform/mac_source_access.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <climits>
#include <sys/stat.h>

namespace pixcake::platform {
namespace {

constexpr std::uint8_t kScoped = 'S';
constexpr std::uint8_t kPlain = 'B';

std::string path_of(CFURLRef url) {
  char buffer[PATH_MAX];
  if (!CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8*>(buffer), sizeof(buffer))) return {};
  return buffer;
}

bool exists(const std::string& path) {
  struct stat info {};
  return ::stat(path.c_str(), &info) == 0;
}

CFDataRef bookmark_data(CFURLRef url, CFURLBookmarkCreationOptions options) {
  return CFURLCreateBookmarkData(kCFAllocatorDefault, url, options, nullptr, nullptr, nullptr);
}

// 解析书签。失败时返回空，missing 表示系统说文件不在了。
CFURLRef resolve_url(const std::vector<std::uint8_t>& bookmark, bool& stale, bool& missing) {
  stale = false;
  missing = false;
  if (bookmark.size() < 2 || (bookmark[0] != kScoped && bookmark[0] != kPlain)) return nullptr;
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, bookmark.data() + 1, static_cast<CFIndex>(bookmark.size() - 1));
  if (data == nullptr) return nullptr;
  CFURLBookmarkResolutionOptions options = kCFURLBookmarkResolutionWithoutUIMask;
  if (bookmark[0] == kScoped) options |= kCFURLBookmarkResolutionWithSecurityScope;
  Boolean is_stale = false;
  CFErrorRef error = nullptr;
  CFURLRef url = CFURLCreateByResolvingBookmarkData(kCFAllocatorDefault, data, options, nullptr, nullptr, &is_stale,
                                                    &error);
  CFRelease(data);
  if (url == nullptr) {
    if (error != nullptr) {
      const CFIndex code = CFErrorGetCode(error);
      // NSFileNoSuchFileError(4)、NSFileReadNoSuchFileError(260)：书签有效，但文件不在了。
      missing = code == 4 || code == 260;
      CFRelease(error);
    }
    return nullptr;
  }
  stale = is_stale;
  return url;
}

}  // namespace

ResultCode MacSourceAccess::create_bookmark(const std::string& absolute_path, std::vector<std::uint8_t>& out) {
  out.clear();
  if (absolute_path.empty() || absolute_path.front() != '/') return ResultCode::StateConflict;
  if (!exists(absolute_path)) return ResultCode::SourceMissing;
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(absolute_path.data()),
      static_cast<CFIndex>(absolute_path.size()), false);
  if (url == nullptr) return ResultCode::StateConflict;
  std::uint8_t kind = kScoped;
  CFDataRef data = bookmark_data(url, kCFURLBookmarkCreationWithSecurityScope |
                                          kCFURLBookmarkCreationSecurityScopeAllowOnlyReadAccess);
  if (data == nullptr) {
    // 非沙盒构建拿不到安全范围书签，退回普通书签（仍只指向这一个文件）。
    kind = kPlain;
    data = bookmark_data(url, 0);
  }
  CFRelease(url);
  if (data == nullptr) return ResultCode::PermissionDenied;
  const UInt8* bytes = CFDataGetBytePtr(data);
  out.push_back(kind);
  out.insert(out.end(), bytes, bytes + CFDataGetLength(data));
  CFRelease(data);
  return ResultCode::Ok;
}

ResolvedSource MacSourceAccess::resolve(const std::vector<std::uint8_t>& bookmark) {
  ResolvedSource result;
  bool stale = false;
  bool missing = false;
  CFURLRef url = resolve_url(bookmark, stale, missing);
  if (url == nullptr) {
    result.code = missing ? ResultCode::SourceMissing : ResultCode::PermissionDenied;
    return result;
  }
  result.absolute_path = path_of(url);
  if (result.absolute_path.empty() || !exists(result.absolute_path)) {
    result.code = ResultCode::SourceMissing;
  } else if (stale) {
    result.stale = true;
    std::vector<std::uint8_t> renewed;
    if (create_bookmark(result.absolute_path, renewed) == ResultCode::Ok) result.refreshed = std::move(renewed);
  }
  CFRelease(url);
  return result;
}

SourceScope::SourceScope(const std::vector<std::uint8_t>& bookmark) {
  bool stale = false;
  bool missing = false;
  CFURLRef url = resolve_url(bookmark, stale, missing);
  if (url == nullptr) {
    code_ = missing ? ResultCode::SourceMissing : ResultCode::PermissionDenied;
    return;
  }
  url_ = url;
  path_ = path_of(url);
  if (bookmark[0] == kScoped) {
    started_ = CFURLStartAccessingSecurityScopedResource(url);
    code_ = started_ ? ResultCode::Ok : ResultCode::PermissionDenied;
  } else {
    code_ = ResultCode::Ok;
  }
}

SourceScope::~SourceScope() {
  if (url_ == nullptr) return;
  CFURLRef url = static_cast<CFURLRef>(url_);
  if (started_) CFURLStopAccessingSecurityScopedResource(url);
  CFRelease(url);
}

}  // namespace pixcake::platform
