#include "pixcake/ui/shell/color_adjustment_panel.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++failures;
  }
}

#define EXPECT(value) expect(static_cast<bool>(value), #value, __LINE__)

std::vector<pixcake::ui::shell::ColorAdjustmentGroup> provider_groups() {
  using pixcake::ui::shell::ColorAdjustmentGroup;
  using pixcake::ui::shell::ColorAdjustmentParameter;
  return {
      {"white-balance", "白平衡",
       {{"color.global", "temperature", "色温", -100.0, 100.0, 1.0, 0.0, 0.0, true, ""},
        {"color.global", "tint", "色调", -100.0, 100.0, 1.0, 0.0, 0.0, true, ""}},
       true, true, ""},
      {"tone", "影调",
       {{"color.global", "exposure", "曝光", -5.0, 5.0, 0.05, 0.0, 0.0, true, ""}},
       true, true, ""},
      {"levels", "色阶", {}, false, false, "当前阶段未注册绘制"},
  };
}

void test_provider_is_source_of_truth() {
  using namespace pixcake::ui::shell;
  std::vector<ColorAdjustmentPreviewUpdate> updates;
  ColorAdjustmentPanelModel model(provider_groups, [&](const ColorAdjustmentPreviewUpdate& update) {
    updates.push_back(update);
  });

  EXPECT(model.groups().size() == 3);
  const auto* exposure = model.find_parameter("color.global", "exposure");
  EXPECT(exposure != nullptr);
  if (exposure != nullptr) {
    EXPECT(exposure->minimum == -5.0);
    EXPECT(exposure->step == 0.05);
  }
  EXPECT(model.find_group("levels") != nullptr);
  EXPECT(!model.find_group("levels")->registered_for_drawing);

  EXPECT(model.set_preview_value("color.global", "exposure", 1.0, 42));
  EXPECT(!model.set_preview_value("color.global", "exposure", 1.001, 42));
  EXPECT(!model.set_preview_value("color.global", "exposure", 6.0, 42));
  EXPECT(!model.set_preview_value("color.global", "missing", 0.0, 42));
  EXPECT(updates.size() == 1);
  if (!updates.empty()) {
    EXPECT(updates[0].capability_id == "color.global");
    EXPECT(updates[0].parameter_id == "exposure");
    EXPECT(std::abs(updates[0].value - 1.0) < 1e-9);
    EXPECT(updates[0].base_revision == 42);
  }
}

void test_visibility_is_transient() {
  using namespace pixcake::ui::shell;
  int updates = 0;
  ColorAdjustmentPanelModel model(provider_groups, [&](const ColorAdjustmentPreviewUpdate&) { ++updates; });
  EXPECT(model.group_visible("tone"));
  EXPECT(model.set_group_visible("tone", false));
  EXPECT(!model.group_visible("tone"));
  EXPECT(!model.set_group_visible("missing", false));
  EXPECT(model.set_preview_value("color.global", "exposure", 0.0, 7));
  EXPECT(updates == 1);
  model.refresh();
  EXPECT(model.group_visible("tone"));
}
}  // namespace

int main() {
  test_provider_is_source_of_truth();
  test_visibility_is_transient();
  return failures == 0 ? 0 : 1;
}
