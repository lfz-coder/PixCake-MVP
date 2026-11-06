#include "pixcake/logging/file_log_sink.hpp"

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <string>
#include <sys/stat.h>

namespace pixcake::logging {

namespace {

std::string clean(std::string text) {
  for (char& ch : text) {
    if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
  }
  return text;
}

ResultCode make_directories(const std::string& directory) {
  if (directory.empty()) return ResultCode::IoFailed;
  std::string partial;
  for (std::size_t i = 0; i <= directory.size(); ++i) {
    if (i == directory.size() || (directory[i] == '/' && i != 0)) {
      partial = directory.substr(0, i);
      struct stat info {};
      if (::stat(partial.c_str(), &info) == 0) {
        if (!S_ISDIR(info.st_mode)) return ResultCode::IoFailed;
        continue;
      }
      if (::mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) {
        return errno == EACCES ? ResultCode::PermissionDenied
               : errno == ENOSPC ? ResultCode::DiskFull
                                 : ResultCode::IoFailed;
      }
    }
  }
  return ResultCode::Ok;
}

std::string utc_now() {
  const std::time_t now = std::time(nullptr);
  std::tm parts {};
  gmtime_r(&now, &parts);
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &parts);
  return text;
}

}  // namespace

std::unique_ptr<FileLogSink> FileLogSink::open(const std::string& directory, std::string process_name,
                                               ResultCode& code) {
  code = make_directories(directory);
  if (code != ResultCode::Ok) return nullptr;
  const std::string path = directory + "/" + process_name + ".log";
  std::FILE* file = std::fopen(path.c_str(), "a");
  if (file == nullptr) {
    code = errno == EACCES ? ResultCode::PermissionDenied
           : errno == ENOSPC ? ResultCode::DiskFull
                             : ResultCode::IoFailed;
    return nullptr;
  }
  return std::unique_ptr<FileLogSink>(new FileLogSink(file, std::move(process_name)));
}

FileLogSink::FileLogSink(std::FILE* file, std::string process_name)
    : file_(file), process_name_(std::move(process_name)) {}

FileLogSink::~FileLogSink() {
  if (file_ != nullptr) std::fclose(file_);
}

void FileLogSink::write(const LogRecord& record) {
  const std::string line = utc_now() + '\t' + clean(process_name_) + '\t' + clean(record.module) + '\t' +
                           clean(record.event) + '\t' + result_code_label(record.code) + '\t' +
                           clean(record.task_id) + '\t' + clean(record.document_id) + '\t' +
                           std::to_string(record.revision) + '\t' + clean(record.path) + '\t' +
                           clean(record.phase) + '\t' + std::to_string(record.duration_ms) + '\t' +
                           (record.deduplicated ? "1" : "0") + '\t' + (record.charged ? "1" : "0") + '\t' +
                           clean(record.message) + '\n';
  std::lock_guard<std::mutex> lock(mutex_);
  std::fputs(line.c_str(), file_);
  std::fflush(file_);
}

std::string default_log_directory() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') return {};
  return std::string(home) + "/Library/Application Support/PixCake/logs";
}

}  // namespace pixcake::logging
