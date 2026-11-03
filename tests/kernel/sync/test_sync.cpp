#include "pixcake/sync/sync_batch.hpp"

#include <cstdio>
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

const pixcake::SyncItem* find_item(const std::vector<pixcake::SyncItem>& tree, const char* id) {
  for (const auto& item : tree) {
    if (item.id == id) {
      return &item;
    }
  }
  return nullptr;
}

void test_tree_reads_attributes_from_owners() {
  const auto tree = pixcake::sync_tree();
  EXPECT(tree.size() == 22);

  const auto* match = find_item(tree, "color.match");
  EXPECT(match != nullptr);
  EXPECT(match->module_label == "AI 色彩");
  EXPECT(match->label == "AI 追色");
  EXPECT(match->capability_id == "color.match");
  EXPECT(!match->enabled);
  EXPECT(match->blocked_reason == "同步时按每张图重新计算，不是把数值原样抄过去");

  const auto* local = find_item(tree, "color.match.local");
  EXPECT(local != nullptr);
  EXPECT(local->module_label == "AI 色彩");
  EXPECT(local->label == "局部追色");
  EXPECT(local->capability_id.empty());
  EXPECT(!local->enabled);
  EXPECT(local->blocked_reason == "局部追色不能批量同步");

  const auto* style = find_item(tree, "color.style");
  EXPECT(style != nullptr);
  EXPECT(!style->enabled);
  EXPECT(style->blocked_reason == match->blocked_reason);

  const auto* detail = find_item(tree, "detail");
  EXPECT(detail != nullptr);
  EXPECT(detail->module_label == "色彩");
  EXPECT(detail->label == "细节");
  EXPECT(detail->enabled);
  EXPECT(detail->capability_id == "detail");

  const auto* brush = find_item(tree, "mask.brush");
  EXPECT(brush != nullptr);
  EXPECT(brush->module_label == "色彩");
  EXPECT(brush->label == "自定义画笔蒙版");
  EXPECT(brush->capability_id.empty());
  EXPECT(!brush->enabled);
  EXPECT(brush->blocked_reason == "自定义里的画笔不能同步");

  const char* enabled[] = {"color.global",    "color.local",     "color.post",       "portrait.blemish",
                           "portrait.skin",    "portrait.reshape", "portrait.feature", "portrait.body",
                           "image.background", "image.idphoto",   "image.sky",        "clothes",
                           "pet",              "crop",            "manual.edge",      "manual.liquify",
                           "manual.heal"};
  for (const char* id : enabled) {
    const auto* item = find_item(tree, id);
    EXPECT(item != nullptr);
    EXPECT(item->enabled);
    EXPECT(item->capability_id == id);
    EXPECT(item->blocked_reason.empty());
  }
  EXPECT(find_item(tree, "manual.edge")->module_label == "手动");
  EXPECT(find_item(tree, "pet")->module_label == "宠物");
}

}  // namespace

int main() {
  test_tree_reads_attributes_from_owners();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
