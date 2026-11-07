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

std::string file_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

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

void test_c54_committed_step_restores_without_writing() {
  pixcake::test::FakeFileSystem files;
  auto document = make_document("img-1");
  EXPECT(document.set_preview("color.global", "exposure", "drag-1") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.global", "exposure", "drag-2") == pixcake::ResultCode::Ok);

  pixcake::HistoryLog history;
  EXPECT(history.steps("img-1") == nullptr);
  commit_exposure(document, files, "-1.50");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "-1.50", false) ==
         pixcake::ResultCode::Ok);
  EXPECT(history.steps("img-1")->size() == 1);
  EXPECT(history.steps("img-1")->at(0).display_value == "-1.50");
  EXPECT(history.steps("img-1")->at(0).revision == 1);
  EXPECT(history.current_index("img-1") == 0);

  commit_exposure(document, files, "0.25");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "0.25", false) ==
         pixcake::ResultCode::Ok);
  EXPECT(history.steps("img-1")->size() == 2);
  const auto saved_bytes = files.files[kPath];
  const auto writes = files.writes.size();

  EXPECT(history.select("img-1", document, 0, false) == pixcake::ResultCode::Ok);
  // Selecting old content does not rewind the committed generation.
  EXPECT(document.revision() == 2);
  EXPECT(document.committed_parameter("color.global", "exposure") == "-1.50");
  EXPECT(!document.preview("color.global", "exposure").has_value());
  EXPECT(history.current_index("img-1") == 0);
  EXPECT(files.files[kPath] == saved_bytes);
  EXPECT(files.writes.size() == writes);

  EXPECT(history.select("img-1", document, 1, false) == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 2);
  EXPECT(document.committed_parameter("color.global", "exposure") == "0.25");
  EXPECT(history.current_index("img-1") == 1);

  auto other = make_document("img-2");
  EXPECT(history.record_committed("img-2", other, "portrait.skin", "smooth", "10", false) ==
         pixcake::ResultCode::Ok);
  EXPECT(history.steps("img-1")->size() == 2);
  EXPECT(history.steps("img-2")->size() == 1);
  EXPECT(history.steps("img-2")->at(0).display_value == "10");
  EXPECT(history.record_committed("img-2", document, "color.global", "exposure", "0.25", false) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(history.steps("img-2")->size() == 1);
  EXPECT(history.steps("img-1")->at(1).display_value == "0.25");
}

void test_b29_bad_index_keeps_revision_and_neighbors() {
  pixcake::test::FakeFileSystem files;
  auto document = make_document("img-1");
  pixcake::HistoryLog history;
  commit_exposure(document, files, "-1.50");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "-1.50", false) ==
         pixcake::ResultCode::Ok);
  commit_exposure(document, files, "0.00");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "0.00", false) ==
         pixcake::ResultCode::Ok);
  commit_exposure(document, files, "0.25");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "0.25", false) ==
         pixcake::ResultCode::Ok);

  const auto before = *history.steps("img-1");
  EXPECT(before[0].revision == 1);
  EXPECT(before[1].revision == 2);
  EXPECT(before[2].revision == 3);
  EXPECT(history.select("img-1", document, 9, false) == pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 3);
  EXPECT(document.committed_parameter("color.global", "exposure") == "0.25");
  EXPECT(history.current_index("img-1") == 2);
  EXPECT(history.steps("img-1")->size() == before.size());
  EXPECT(history.steps("img-1")->at(0).revision == before[0].revision);
  EXPECT(history.steps("img-1")->at(1).revision == before[1].revision);
  EXPECT(history.steps("img-1")->at(2).revision == before[2].revision);
  EXPECT(history.steps("img-1")->at(0).display_value == before[0].display_value);
  EXPECT(history.steps("img-1")->at(2).snapshot == before[2].snapshot);

  auto missing = make_document("missing");
  const auto missing_revision = missing.revision();
  EXPECT(history.select("missing", missing, 0, false) == pixcake::ResultCode::StateConflict);
  EXPECT(missing.revision() == missing_revision);
  EXPECT(history.steps("missing") == nullptr);
}

void test_b70_batch_design_rejects_history_commands() {
  pixcake::test::FakeFileSystem files;
  auto document = make_document("img-1");
  pixcake::HistoryLog history;
  commit_exposure(document, files, "-1.50");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "-1.50", true) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(history.steps("img-1") == nullptr);
  EXPECT(document.revision() == 1);

  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "-1.50", false) ==
         pixcake::ResultCode::Ok);
  EXPECT(history.select("img-1", document, 0, true) == pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 1);
  EXPECT(document.committed_parameter("color.global", "exposure") == "-1.50");
  EXPECT(history.current_index("img-1") == 0);
  EXPECT(history.steps("img-1")->size() == 1);
}

void test_b73_older_revision_does_not_append() {
  pixcake::test::FakeFileSystem files;
  auto document = make_document("img-1");
  pixcake::HistoryLog history;
  commit_exposure(document, files, "-1.50");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "-1.50", false) ==
         pixcake::ResultCode::Ok);
  auto first = document;
  commit_exposure(document, files, "0.25");
  EXPECT(history.record_committed("img-1", document, "color.global", "exposure", "0.25", false) ==
         pixcake::ResultCode::Ok);

  EXPECT(history.record_committed("img-1", first, "color.global", "exposure", "-1.50", false) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(history.steps("img-1")->size() == 2);
  EXPECT(history.steps("img-1")->at(0).revision == 1);
  EXPECT(history.steps("img-1")->at(1).revision == 2);
  EXPECT(history.steps("img-1")->at(0).revision != history.steps("img-1")->at(1).revision);
  EXPECT(history.current_index("img-1") == 1);
  EXPECT(document.revision() == 2);
}

void test_history_branch_keeps_monotonic_revision() {
  pixcake::test::FakeFileSystem files;
  auto document = make_document("branch");
  pixcake::HistoryLog history;
  commit_exposure(document, files, "-1.50");
  EXPECT(history.record_committed("branch", document, "color.global", "exposure", "-1.50", false) ==
         pixcake::ResultCode::Ok);
  commit_exposure(document, files, "0.25");
  EXPECT(history.record_committed("branch", document, "color.global", "exposure", "0.25", false) ==
         pixcake::ResultCode::Ok);
  EXPECT(history.select("branch", document, 0, false) == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 2);
  commit_exposure(document, files, "1.00");
  EXPECT(document.revision() == 3);
  EXPECT(history.record_committed("branch", document, "color.global", "exposure", "1.00", false) ==
         pixcake::ResultCode::Ok);
  // The undone step (0.25) cannot be redone any more: the new edit replaces it.
  EXPECT(history.steps("branch")->size() == 2);
  EXPECT(history.steps("branch")->at(1).revision == 3);
  EXPECT(history.steps("branch")->at(1).display_value == "1.00");
  EXPECT(history.current_index("branch") == std::optional<std::size_t>(1));
}


void test_history_sources_do_not_touch_sqlite_or_qt() {
  const std::filesystem::path history_dir =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "kernel" /
      "history";
  EXPECT(std::filesystem::is_directory(history_dir));
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::recursive_directory_iterator(history_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string text = file_text(entry.path());
    EXPECT(text.find("sqlite3.h") == std::string::npos);
    EXPECT(text.find("QWidget") == std::string::npos);
    EXPECT(text.find("file_system.hpp") == std::string::npos);
  }
}

}  // namespace

int main() {
  test_c54_committed_step_restores_without_writing();
  test_b29_bad_index_keeps_revision_and_neighbors();
  test_b70_batch_design_rejects_history_commands();
  test_b73_older_revision_does_not_append();
  test_history_branch_keeps_monotonic_revision();
  test_history_sources_do_not_touch_sqlite_or_qt();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d assertion(s) failed\n", g_failures);
    return 1;
  }
  return 0;
}
