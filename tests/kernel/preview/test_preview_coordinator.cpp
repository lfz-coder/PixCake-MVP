#include "pixcake/preview/preview_coordinator.hpp"

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

void test_latest_value_wins_without_writing() {
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

int main() {
  test_latest_value_wins_without_writing();
  test_rejects_stale_and_fast_export_requests();
  return g_failures == 0 ? 0 : 1;
}
