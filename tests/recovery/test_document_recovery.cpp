#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/support/checksum.hpp"
#include "support/fake_file_system.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(bytes[offset + static_cast<std::size_t>(i)]) << (8 * i);
  }
  return value;
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    bytes[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu);
  }
}

void test_b100_posix_truncate_restores_backup_document() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / ("pixcake-editdoc-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root);
  const auto formal = (root / "img.pxedit").string();
  pixcake::PosixFileSystem files;
  auto document = *pixcake::EditDocument::create("img-1");
  document.add_mask_id("mask-a");
  EXPECT(document.write_snapshot(files, formal) == pixcake::ResultCode::Ok);
  const auto checksum = document.content_checksum();
  document.add_mask_id("mask-b");
  EXPECT(document.write_snapshot(files, formal) == pixcake::ResultCode::Ok);
  {
    std::ofstream out(formal, std::ios::binary | std::ios::trunc);
    out << "BAD";
  }
  pixcake::EditDocument loaded;
  const auto result = pixcake::EditDocument::load(loaded, files, formal, {});
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.used_backup);
  EXPECT(loaded.image_id() == "img-1");
  EXPECT(loaded.mask_ids().size() == 1);
  EXPECT(loaded.mask_ids()[0] == "mask-a");
  EXPECT(loaded.content_checksum() == checksum);
  fs::remove_all(root);
}

void test_b08_posix_truncate_without_backup_is_marked_empty() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / ("pixcake-editdoc-empty-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root);
  const auto formal = (root / "img.pxedit").string();
  pixcake::PosixFileSystem files;
  auto document = *pixcake::EditDocument::create("img-1");
  EXPECT(document.write_snapshot(files, formal) == pixcake::ResultCode::Ok);
  {
    std::ofstream out(formal, std::ios::binary | std::ios::trunc);
    out << "BAD";
  }
  pixcake::EditDocument loaded;
  const auto result = pixcake::EditDocument::load(loaded, files, formal, {});
  EXPECT(result.code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(result.recovered_empty);
  EXPECT(loaded.recovered_empty());
  EXPECT(loaded.image_id() != "img-1");
  EXPECT(!files.exists(formal));
  pixcake::EditDocument reopened;
  const auto second = pixcake::EditDocument::load(reopened, files, formal, {});
  EXPECT(second.code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(second.recovered_empty);
  EXPECT(!second.absent);
  fs::remove_all(root);
}

void test_c17_newer_file_format_is_document_corrupt() {
  auto document = *pixcake::EditDocument::create("img-1");
  auto bytes = document.encode();
  bytes[4] = 2;
  const std::uint64_t body_size = read_u64(bytes, 8);
  const auto checksum = pixcake::checksum_fnv1a64(bytes.data() + 16, static_cast<std::size_t>(body_size));
  write_u64(bytes, 16 + static_cast<std::size_t>(body_size), checksum);
  pixcake::test::FakeFileSystem files;
  files.files["docs/img.pxedit"] = bytes;
  pixcake::EditDocument loaded;
  const auto result = pixcake::EditDocument::load(loaded, files, "docs/img.pxedit", {});
  EXPECT(result.code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(result.code != pixcake::ResultCode::CapabilityTooNew);
  EXPECT(result.recovered_empty);
  EXPECT(loaded.recovered_empty());
}

void test_b111_crash_during_replace_reopens_last_complete_document() {
  pixcake::test::FakeFileSystem files;
  auto document = *pixcake::EditDocument::create("img-1");
  document.add_mask_id("mask-a");
  EXPECT(document.write_snapshot(files, "docs/img.pxedit") == pixcake::ResultCode::Ok);
  const auto checksum = document.content_checksum();
  document.add_mask_id("mask-b");
  files.arm_crash_after(3);
  bool crashed = false;
  try {
    document.write_snapshot(files, "docs/img.pxedit");
  } catch (const pixcake::test::SimulatedCrash&) {
    crashed = true;
  }
  EXPECT(crashed);
  files.crash_after = 0;
  pixcake::EditDocument loaded;
  const auto result = pixcake::EditDocument::load(loaded, files, "docs/img.pxedit", {});
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.used_backup);
  EXPECT(loaded.mask_ids().size() == 1);
  EXPECT(loaded.mask_ids()[0] == "mask-a");
  EXPECT(loaded.content_checksum() == checksum);
}

}  // namespace

int main() {
  test_b100_posix_truncate_restores_backup_document();
  test_b08_posix_truncate_without_backup_is_marked_empty();
  test_c17_newer_file_format_is_document_corrupt();
  test_b111_crash_during_replace_reopens_last_complete_document();
  return g_failures == 0 ? 0 : 1;
}
