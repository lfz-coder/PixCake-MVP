#include "sync_internal.hpp"
#include "pixcake/platform/sha256.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <set>

namespace pixcake::cloud_sync {
void require(bool condition, ResultCode code) { if (!condition) throw Failure{code}; }
bool key_valid(const std::string& value) {
  return value.size() == 9 && value[0] == 'f' && value.find_first_not_of("0123456789", 1) == std::string::npos;
}
bool identifier(const std::string& value, const std::string& prefix) {
  return value.size() == prefix.size() + 32 && value.compare(0, prefix.size(), prefix) == 0 &&
         value.find_first_not_of("0123456789abcdef", prefix.size()) == std::string::npos;
}
bool digest_valid(const std::string& value) {
  return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string digest(const Bytes& bytes) { return platform::sha256(bytes.data(), bytes.size()); }
Json parse(const Bytes& bytes) {
  require(bytes.size() <= kManifest);
  std::vector<std::set<std::string>> objects;
  std::size_t nodes = 0;
  return Json::parse(bytes.begin(), bytes.end(), [&](int depth, Json::parse_event_t event, Json& value) {
    require(depth <= 10 && ++nodes <= 200000);
    if (event == Json::parse_event_t::object_start) objects.emplace_back();
    if (event == Json::parse_event_t::key) {
      require(!objects.empty() && objects.back().size() < 32 && objects.back().insert(value.get<std::string>()).second);
    }
    if (event == Json::parse_event_t::object_end) objects.pop_back();
    if (value.is_string()) require(value.get_ref<const std::string&>().size() <= 4096);
    return true;
  });
}
Bytes encode(const Json& json) {
  const auto text = json.dump(); require(text.size() <= kManifest);
  return Bytes(text.begin(), text.end());
}
namespace {
ResultCode io_code() { return errno == ENOSPC ? ResultCode::DiskFull : ResultCode::IoFailed; }
struct Fd { int value = -1; ~Fd() { if (value >= 0) ::close(value); } };
}
bool regular(const fs::path& path) {
  std::error_code error;
  return fs::is_regular_file(fs::symlink_status(path, error));
}
void folder(const fs::path& path) {
  if (!fs::exists(path)) {
    require(::mkdir(path.c_str(), 0700) == 0, io_code());
  }
  require(!fs::is_symlink(path) && fs::is_directory(path), ResultCode::PermissionDenied);
}
void sync_directory(const fs::path& path) {
  Fd directory{::open(path.c_str(), O_RDONLY)};
  require(directory.value >= 0 && ::fsync(directory.value) == 0, io_code());
}
Bytes read(const fs::path& path, std::size_t limit) {
  Fd file{::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK)};
  require(file.value >= 0, errno == ENOENT ? ResultCode::SourceMissing : io_code());
  struct stat info{};
  require(::fstat(file.value, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0, ResultCode::IoFailed);
  require(static_cast<std::uint64_t>(info.st_size) <= limit);
  Bytes bytes(static_cast<std::size_t>(info.st_size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::read(file.value, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    require(count > 0, io_code()); offset += static_cast<std::size_t>(count);
  }
  return bytes;
}
void save(const fs::path& path, const Bytes& bytes) {
  const auto temporary = path.string() + ".tmp";
  Fd file{::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600)};
  require(file.value >= 0, io_code());
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::write(file.value, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    require(count > 0, io_code()); offset += static_cast<std::size_t>(count);
  }
  require(::fsync(file.value) == 0, io_code());
  require(::rename(temporary.c_str(), path.c_str()) == 0, io_code());
  Fd parent{::open(path.parent_path().c_str(), O_RDONLY)};
  require(parent.value >= 0 && ::fsync(parent.value) == 0, io_code());
}
std::string new_job(const fs::path& root) {
  const auto id = platform::random_identifier(); require(id.size() == 32, ResultCode::IoFailed);
  const auto path = root / ("job-" + id);
  require(::mkdir(path.c_str(), 0700) == 0, io_code());
  folder(path / "chunks"); folder(path / "files");
  Fd job{::open(path.c_str(), O_RDONLY)}, parent{::open(root.c_str(), O_RDONLY)};
  require(job.value >= 0 && parent.value >= 0 && ::fsync(job.value) == 0 && ::fsync(parent.value) == 0, io_code());
  return path.filename().string();
}
}  // namespace pixcake::cloud_sync
