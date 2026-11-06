#include "pixcake/logging/file_log_sink.hpp"
#include "support/temp_dir.hpp"

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

std::string read_all(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void test_o01_line_has_time_process_module_code() {
  pixcake::test::TempDir dir;
  const std::string logs = dir.path() + "/logs";
  pixcake::ResultCode code = pixcake::ResultCode::IoFailed;
  auto sink = pixcake::logging::FileLogSink::open(logs, "exportd", code);
  EXPECT(code == pixcake::ResultCode::Ok);
  EXPECT(sink != nullptr);
  if (!sink) return;
  pixcake::LogRecord record;
  record.module = "exportq";
  record.event = "export.terminal";
  record.code = pixcake::ResultCode::DiskFull;
  record.task_id = "e1";
  record.path = "/a\tb\nc";
  sink->write(record);
  sink.reset();
  const std::string text = read_all(logs + "/exportd.log");
  EXPECT(std::regex_search(text, std::regex("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z\texportd\t")));
  EXPECT(text.find("\texportq\t") != std::string::npos);
  EXPECT(text.find("\texport.terminal\t") != std::string::npos);
  EXPECT(text.find(pixcake::result_code_label(pixcake::ResultCode::DiskFull)) != std::string::npos);
  EXPECT(text.find("\te1\t") != std::string::npos);
  EXPECT(text.find("/a b c") != std::string::npos);
  std::size_t newlines = 0;
  for (const char ch : text) newlines += ch == '\n' ? 1 : 0;
  EXPECT(newlines == 1);
}

void test_o01_unwritable_directory_reports_failure() {
  pixcake::test::TempDir dir;
  const std::string blocker = dir.path() + "/not-a-dir";
  std::ofstream(blocker) << "x";
  pixcake::ResultCode code = pixcake::ResultCode::Ok;
  auto sink = pixcake::logging::FileLogSink::open(blocker, "renderd", code);
  EXPECT(sink == nullptr);
  EXPECT(code != pixcake::ResultCode::Ok);
}

}  // namespace

int main() {
  test_o01_line_has_time_process_module_code();
  test_o01_unwritable_directory_reports_failure();
  return g_failures == 0 ? 0 : 1;
}
