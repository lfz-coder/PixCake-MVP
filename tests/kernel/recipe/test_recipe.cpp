#include "pixcake/recipe/recipe.hpp"

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

pixcake::recipe::RecipeNode node(const char* id, const char* tool, bool editable) {
  pixcake::recipe::RecipeNode value;
  value.node_id = id;
  value.tool_id = tool;
  value.parameters = {1, 2, 3};
  value.editable = editable;
  return value;
}

void test_ordered_node_editing() {
  using pixcake::ResultCode;
  auto recipe = pixcake::recipe::Recipe::create("recipe-1", "我的配方");
  EXPECT(recipe.has_value());
  EXPECT(recipe->append_node(node("n1", "tool-a", true)) == ResultCode::Ok);
  EXPECT(recipe->append_node(node("n2", "tool-b", false)) == ResultCode::Ok);
  EXPECT(recipe->append_node(node("n1", "tool-c", true)) == ResultCode::StateConflict);
  EXPECT(recipe->move_node("n2", 0) == ResultCode::Ok);
  EXPECT(recipe->nodes().front().node_id == "n2");
  EXPECT(recipe->nodes().front().tool_id == "tool-b");
  EXPECT(recipe->remove_node("n2") == ResultCode::Ok);
  EXPECT(recipe->remove_node("missing") == ResultCode::StateConflict);
  EXPECT(recipe->nodes().size() == 1);
  EXPECT(recipe->nodes().front().parameters.size() == 3);
}

void test_empty_recipe_and_duplicate_batch_targets_rejected() {
  using pixcake::ResultCode;
  auto empty = pixcake::recipe::Recipe::create("recipe-empty", "空配方");
  pixcake::recipe::RecipeRuntime runtime;
  pixcake::recipe::RecipeRun run;
  EXPECT(runtime.begin(*empty, {"image-1"}, run) == ResultCode::StateConflict);
  EXPECT(!run.active());

  auto recipe = pixcake::recipe::Recipe::create("recipe-1", "批量");
  EXPECT(recipe->append_node(node("n1", "tool-a", true)) == ResultCode::Ok);
  EXPECT(runtime.begin(*recipe, {"image-1", "image-1"}, run) == ResultCode::StateConflict);
  EXPECT(runtime.begin(*recipe, {"image-1", ""}, run) == ResultCode::StateConflict);
  EXPECT(runtime.begin(*recipe, {"image-1", "image-2"}, run) == ResultCode::Ok);
  EXPECT(run.active());
  EXPECT(run.occupies("image-1"));
  EXPECT(run.occupies("image-2"));
  EXPECT(!run.occupies("image-3"));
  EXPECT(run.image_ids().size() == 2);
}

void test_complete_and_cancel_release_occupancy() {
  using pixcake::ResultCode;
  auto recipe = pixcake::recipe::Recipe::create("recipe-1", "批量");
  EXPECT(recipe->append_node(node("n1", "tool-a", true)) == ResultCode::Ok);
  pixcake::recipe::RecipeRuntime runtime;
  pixcake::recipe::RecipeRun completed;
  EXPECT(runtime.begin(*recipe, {"image-1"}, completed) == ResultCode::Ok);
  EXPECT(runtime.occupies("image-1"));
  EXPECT(runtime.complete(completed) == ResultCode::Ok);
  EXPECT(!completed.active());
  EXPECT(!runtime.occupies("image-1"));
  EXPECT(runtime.complete(completed) == ResultCode::StateConflict);

  pixcake::recipe::RecipeRun cancelled;
  EXPECT(runtime.begin(*recipe, {"image-2"}, cancelled) == ResultCode::Ok);
  EXPECT(runtime.cancel(cancelled) == ResultCode::Ok);
  EXPECT(!cancelled.active());
  EXPECT(!runtime.occupies("image-2"));
  EXPECT(runtime.cancel(cancelled) == ResultCode::StateConflict);
}

}  // namespace

int main() {
  test_ordered_node_editing();
  test_empty_recipe_and_duplicate_batch_targets_rejected();
  test_complete_and_cancel_release_occupancy();
  return g_failures == 0 ? 0 : 1;
}
