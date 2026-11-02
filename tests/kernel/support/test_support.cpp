#include "pixcake/support/checksum.hpp"
#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/log_sink.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <cstdio>
#include <set>
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

}  // namespace

int main() {
  const pixcake::ResultCode codes[] = {
      pixcake::ResultCode::Ok,
      pixcake::ResultCode::UnsupportedFormat,
      pixcake::ResultCode::ImageCorrupt,
      pixcake::ResultCode::PermissionDenied,
      pixcake::ResultCode::DiskFull,
      pixcake::ResultCode::SourceMissing,
      pixcake::ResultCode::DocumentCorrupt,
      pixcake::ResultCode::CapabilityTooNew,
      pixcake::ResultCode::Cancelled,
      pixcake::ResultCode::NetworkFailed,
      pixcake::ResultCode::InsufficientBalance,
      pixcake::ResultCode::FastExportBusy,
      pixcake::ResultCode::StateConflict,
      pixcake::ResultCode::IoFailed,
  };
  const char* labels[] = {
      "成功",
      "格式不支持",
      "图片损坏",
      "权限不足",
      "磁盘已满",
      "原文件缺失",
      "文档损坏",
      "能力版本过新",
      "已取消",
      "网络失败",
      "点数或张数不足",
      "极速导出占用",
      "状态冲突",
      "输入输出失败",
  };
  std::set<std::string> seen;
  for (std::size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
    const char* label = pixcake::result_code_label(codes[i]);
    EXPECT(label != nullptr);
    EXPECT(std::string(label) == labels[i]);
    EXPECT(seen.insert(label).second);
  }

  EXPECT(pixcake::checksum_fnv1a64(nullptr, 0) == 0xcbf29ce484222325ULL);
  const std::uint8_t letter[] = {'a'};
  EXPECT(pixcake::checksum_fnv1a64(letter, 1) == 0xaf63dc4c8601ec8cULL);
  const std::uint8_t other[] = {'b'};
  EXPECT(pixcake::checksum_fnv1a64(letter, 1) != pixcake::checksum_fnv1a64(other, 1));

  pixcake::LogRecord record;
  record.code = pixcake::ResultCode::Ok;
  record.message = "成功";
  record.path = "/tmp/example.pxedit";
  record.revision = 3;
  record.capability_id = "color.global";
  EXPECT(record.message == "成功");
  EXPECT(sizeof(record.path) > 0);

  struct Latch : pixcake::FastExportLatch {
    bool enabled() const override { return false; }
  };
  Latch latch;
  EXPECT(!latch.enabled());
  return g_failures == 0 ? 0 : 1;
}
