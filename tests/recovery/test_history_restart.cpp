// 测试清单 T-08：历史步骤在进程重开后折叠成“上一次保存”（从 tests/kernel/history/test_history.cpp 迁来）。
#include "pixcake/history/history_log.hpp"
#include "support/fake_file_system.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
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

const char* kPath = "docs/img.pxedit";

std::vector<pixcake::VersionLimit> limits() { return {pixcake::VersionLimit{"color.global", 1}}; }

pixcake::EditDocument make_document(const char* image_id) {
  auto created = pixcake::EditDocument::create(image_id);
  EXPECT(created.has_value());
  return std::move(*created);
}

void commit_exposure(pixcake::EditDocument& document, pixcake::test::FakeFileSystem& files, const char* value) {
  EXPECT(document.set_preview("color.global", "exposure", value) == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(document.revision(), "color.global", "exposure", limits(), files, kPath) ==
         pixcake::ResultCode::Ok);
}








}  // namespace

void test_b101_reopen_folds_to_last_save() {
  pixcake::test::FakeFileSystem files;
  auto document = make_document("img-1");
  pixcake::HistoryLog history;
  commit_exposure(document, files, "-1.50");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "-1.50", false) ==
         pixcake::ResultCode::Ok);
  commit_exposure(document, files, "0.25");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "0.25", false) ==
         pixcake::ResultCode::Ok);
  auto other = make_document("img-2");
  EXPECT(history.record_committed("img-2", other, "portrait.skin", "smooth", "10", false) ==
         pixcake::ResultCode::Ok);

  pixcake::EditDocument loaded;
  const auto opened = pixcake::EditDocument::load(loaded, files, kPath, limits());
  EXPECT(opened.code == pixcake::ResultCode::Ok);
  EXPECT(loaded.revision() == 2);
  EXPECT(loaded.committed_parameter("color.global", "exposure") == "0.25");

  const auto writes = files.writes.size();
  EXPECT(history.reopen_folded("img-1", loaded) == pixcake::ResultCode::Ok);
  EXPECT(files.writes.size() == writes);
  const auto* folded = history.steps("img-1");
  EXPECT(folded != nullptr);
  EXPECT(folded->size() == 1);
  EXPECT(folded->at(0).label == pixcake::kHistoryFoldedLabel);
  EXPECT(folded->at(0).label == "上一次保存");
  EXPECT(folded->at(0).capability_id.empty());
  EXPECT(folded->at(0).parameter_id.empty());
  EXPECT(folded->at(0).display_value.empty());
  EXPECT(folded->at(0).revision == 2);
  EXPECT(folded->at(0).snapshot == loaded.encode());
  EXPECT(history.current_index("img-1") == 0);
  EXPECT(history.select("img-1", loaded, 1, false) == pixcake::ResultCode::StateConflict);
  EXPECT(loaded.revision() == 2);
  EXPECT(loaded.committed_parameter("color.global", "exposure") == "0.25");
  EXPECT(history.select("img-1", loaded, 0, false) == pixcake::ResultCode::Ok);
  EXPECT(loaded.committed_parameter("color.global", "exposure") == "0.25");
  EXPECT(history.steps("img-1")->at(0).display_value.empty());

  EXPECT(history.steps("img-2")->size() == 1);
  EXPECT(history.steps("img-2")->at(0).display_value == "10");

  auto mismatch = make_document("img-9");
  EXPECT(history.reopen_folded("img-1", mismatch) == pixcake::ResultCode::StateConflict);
  EXPECT(history.steps("img-1")->size() == 1);
  EXPECT(history.steps("img-1")->at(0).label == "上一次保存");
  EXPECT(history.steps("img-2")->at(0).display_value == "10");

  EXPECT(history.record_committed("img-1", loaded, "color.global", "exposure", "0.25", false) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(history.steps("img-1")->size() == 1);
  EXPECT(history.steps("img-1")->at(0).label == "上一次保存");
}

int main() {
  test_b101_reopen_folds_to_last_save();
  return g_failures == 0 ? 0 : 1;
}
