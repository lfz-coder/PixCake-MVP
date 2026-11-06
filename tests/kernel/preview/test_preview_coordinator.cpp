#include "pixcake/preview/preview_coordinator.hpp"
#include "pixcake/support/counting_log_sink.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

pixcake::preview::PreviewRequest request(const pixcake::EditDocument& document, const char* value) {
  pixcake::preview::PreviewRequest result;
  result.image_id = document.image_id();
  result.editing_instance_id = document.editing_instance_id();
  result.base_revision = document.revision();
  result.capability_id = "color.global";
  result.parameter_id = "temperature";
  result.display_value = value;
  result.mode = pixcake::preview::PreviewMode::ColorOnly;
  result.max_edge = 4000;
  return result;
}

void test_b74_latest_value_wins_without_writing() {
  auto document = *pixcake::EditDocument::create("img-preview");
  pixcake::preview::PreviewCoordinator coordinator;
  const auto before = document.encode();
  auto first = request(document, "10");
  auto second = request(document, "20");
  EXPECT(coordinator.submit(document, first) == pixcake::ResultCode::Ok);
  EXPECT(coordinator.submit(document, second) == pixcake::ResultCode::Ok);
  EXPECT(coordinator.latest_sequence() == 2);
  const auto result = coordinator.take_latest(document);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.snapshot.has_value());
  if (result.snapshot) {
    EXPECT(result.snapshot->sequence == 2);
    EXPECT(result.snapshot->request.display_value == "20");
    EXPECT(result.snapshot->description.operations.size() == 1);
    EXPECT(result.snapshot->description.operations[0].values[0].numeric_value == 20.0);
  }
  EXPECT(document.revision() == 0);
  EXPECT(document.encode() == before);
  EXPECT(!coordinator.take_latest(document).snapshot.has_value());
}

void test_rejects_stale_and_fast_export_requests() {
  auto document = *pixcake::EditDocument::create("img-stale");
  pixcake::preview::PreviewCoordinator coordinator;
  auto stale = request(document, "10");
  stale.base_revision = 1;
  EXPECT(coordinator.submit(document, stale) == pixcake::ResultCode::StateConflict);

  class BusyLatch final : public pixcake::FastExportLatch {
   public:
    bool enabled() const override { return true; }
  } busy;
  EXPECT(coordinator.submit(document, request(document, "10"), &busy) ==
         pixcake::ResultCode::FastExportBusy);
  auto invalid = request(document, "10.5");
  EXPECT(coordinator.submit(document, invalid) == pixcake::ResultCode::StateConflict);
  auto too_large = request(document, "10");
  too_large.max_edge = 4001;
  EXPECT(coordinator.submit(document, too_large) == pixcake::ResultCode::StateConflict);
}

}  // namespace

void test_o06_replaced_preview_values_are_counted() {
  auto document = *pixcake::EditDocument::create("img-merge");
  pixcake::CountingLogSink log;
  pixcake::preview::PreviewCoordinator coordinator;
  coordinator.set_log(&log);
  EXPECT(coordinator.submit(document, request(document, "10")) == pixcake::ResultCode::Ok);
  EXPECT(coordinator.submit(document, request(document, "20")) == pixcake::ResultCode::Ok);
  EXPECT(coordinator.submit(document, request(document, "30")) == pixcake::ResultCode::Ok);
  EXPECT(coordinator.take_latest(document).code == pixcake::ResultCode::Ok);
  EXPECT(log.count("preview.merged") == 2);
  EXPECT(log.count("preview.taken", pixcake::ResultCode::Ok) == 1);
  EXPECT(coordinator.submit(document, request(document, "40")) == pixcake::ResultCode::Ok);
  EXPECT(log.count("preview.merged") == 2);
}

void test_o06_stale_take_is_logged_with_code() {
  pixcake::test::FakeFileSystem files;
  auto document = *pixcake::EditDocument::create("img-stale-take");
  pixcake::CountingLogSink log;
  pixcake::preview::PreviewCoordinator coordinator;
  coordinator.set_log(&log);
  EXPECT(coordinator.submit(document, request(document, "10")) == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.global", "temperature", "5") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(document.revision(), "color.global", "temperature",
                                 {pixcake::VersionLimit{"color.global", 1}}, files, "/docs/a.pcd") ==
         pixcake::ResultCode::Ok);
  EXPECT(coordinator.take_latest(document).code == pixcake::ResultCode::StateConflict);
  EXPECT(log.count("preview.taken", pixcake::ResultCode::StateConflict) == 1);
}

int main() {
  test_o06_replaced_preview_values_are_counted();
  test_o06_stale_take_is_logged_with_code();
  test_b74_latest_value_wins_without_writing();
  test_rejects_stale_and_fast_export_requests();
  return g_failures == 0 ? 0 : 1;
}
