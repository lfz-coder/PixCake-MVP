#include "pixcake/generation_session/generation_history.hpp"

#include "pixcake/generate/generation_tool_catalog.hpp"
#include "support/temp_dir.hpp"

#include <cstdio>
#include <memory>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::generate::GenerationTask task() {
  pixcake::generate::GenerationTask value;
  value.task_id = "history-task";
  value.image_id = "image-1";
  value.source_revision = 7;
  value.tool.tool_id = pixcake::generate::kDevelopmentFixtureToolId;
  value.tool.parameter_mode_id = "fixture-default";
  value.tool.billing_rule_id = "fixture-points";
  value.tool.output_count = 1;
  value.parameter_payload = {1, 2, 3};
  value.mask_ids = {"mask-1"};
  value.reference_image_ids = {"reference-1"};
  value.idempotency_key = "history-key";
  return value;
}

void test_round_trip_and_status_update() {
  pixcake::test::TempDir directory;
  std::unique_ptr<pixcake::generation_session::GenerationHistoryStore> store;
  std::string diagnostic;
  EXPECT(pixcake::generation_session::GenerationHistoryStore::open(
             directory.path() + "/history.pxgh", store, diagnostic) == pixcake::ResultCode::Ok);
  EXPECT(store != nullptr);
  EXPECT(store->record_task(task(), "排队中") == pixcake::ResultCode::Ok);
  pixcake::generation_session::GenerationHistoryResult result;
  result.ordinal = 0;
  result.result_id = "history-task-1";
  result.pixel_file = directory.path() + "/result.rgba";
  result.checksum = "0123456789abcdef";
  result.width = 1;
  result.height = 1;
  result.continuation_parameters = {9, 8};
  EXPECT(store->update_status("history-task", "成功", {result}) == pixcake::ResultCode::Ok);
  store.reset();

  EXPECT(pixcake::generation_session::GenerationHistoryStore::open(
             directory.path() + "/history.pxgh", store, diagnostic) == pixcake::ResultCode::Ok);
  EXPECT(store != nullptr && store->entries().size() == 1);
  const auto* found = store == nullptr ? nullptr : store->find("history-task");
  EXPECT(found != nullptr && found->status == "成功" && found->tool_id == pixcake::generate::kDevelopmentFixtureToolId);
  EXPECT(found != nullptr && found->parameter_payload == std::vector<std::uint8_t>({1, 2, 3}) &&
         found->mask_ids == std::vector<std::string>({"mask-1"}) &&
         found->reference_image_ids == std::vector<std::string>({"reference-1"}));
  EXPECT(found != nullptr && found->results.size() == 1 &&
         found->results.front().continuation_parameters == std::vector<std::uint8_t>({9, 8}));
}

void test_corrupt_history_is_rejected() {
  pixcake::test::TempDir directory;
  pixcake::PosixFileSystem files;
  EXPECT(files.write_new(directory.path() + "/history.pxgh", {'b', 'a', 'd'}).ok);
  std::unique_ptr<pixcake::generation_session::GenerationHistoryStore> store;
  std::string diagnostic;
  EXPECT(pixcake::generation_session::GenerationHistoryStore::open(
             directory.path() + "/history.pxgh", store, diagnostic) == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(store == nullptr);
}

}  // namespace

int main() {
  test_round_trip_and_status_update();
  test_corrupt_history_is_rejected();
  return g_failures == 0 ? 0 : 1;
}
