#include "pixcake/cull/cull_session.hpp"

#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

void test_rule_values_and_limit() {
  using namespace pixcake::cull;
  CullOptions options;
  options.scene = Scene::Wedding;
  options.sensitivity = Sensitivity::Strong;
  options.mark_target = MarkTarget::Color;
  options.selection_limit = 1;
  options.preserve_existing_labels = true;
  auto session = CullSession::begin(options);
  EXPECT(session.has_value());
  EXPECT(session->record_decision({"image-1", true, "group-1", {false, false, false}}) == pixcake::ResultCode::Ok);
  EXPECT(session->record_decision({"image-2", true, "group-1", {true, false, false}}) == pixcake::ResultCode::Ok);
  EXPECT(session->record_decision({"image-3", false, "", {false, false, false}}) == pixcake::ResultCode::Ok);
  CullResult result;
  EXPECT(session->finish(result) == pixcake::ResultCode::Ok);
  EXPECT(result.options.scene == Scene::Wedding);
  EXPECT(result.actions.size() == 3);
  EXPECT(result.actions[0].applied);
  EXPECT(!result.actions[1].applied);
  EXPECT(!result.actions[1].skipped_existing);
  EXPECT(!result.actions[2].applied);
}

void test_preserve_existing_and_duplicate_rejection() {
  using namespace pixcake::cull;
  CullOptions options;
  options.mark_target = MarkTarget::Star;
  options.preserve_existing_labels = true;
  auto session = CullSession::begin(options);
  EXPECT(session->record_decision({"image-1", true, "", {true, false, false}}) == pixcake::ResultCode::Ok);
  EXPECT(session->record_decision({"image-1", true, "", {false, false, false}}) == pixcake::ResultCode::StateConflict);
  CullResult result;
  EXPECT(session->finish(result) == pixcake::ResultCode::Ok);
  EXPECT(!result.actions.front().applied);
  EXPECT(result.actions.front().skipped_existing);
  EXPECT(session->record_decision({"late", true, "", {false, false, false}}) == pixcake::ResultCode::StateConflict);
}

void test_cancel_drops_unfinished_decisions() {
  using namespace pixcake::cull;
  auto session = CullSession::begin({});
  EXPECT(session.has_value());
  EXPECT(session->record_decision({"image-1", true, "", {false, false, false}}) == pixcake::ResultCode::Ok);
  EXPECT(session->cancel() == pixcake::ResultCode::Ok);
  EXPECT(!session->active());
  EXPECT(session->decisions().empty());
  CullResult result;
  EXPECT(session->finish(result) == pixcake::ResultCode::StateConflict);
}

}  // namespace

int main() {
  test_rule_values_and_limit();
  test_preserve_existing_and_duplicate_rejection();
  test_cancel_drops_unfinished_decisions();
  return g_failures == 0 ? 0 : 1;
}
