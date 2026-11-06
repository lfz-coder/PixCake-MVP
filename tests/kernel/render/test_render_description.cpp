#include "pixcake/render/render_description.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

std::vector<pixcake::VersionLimit> limits() {
  return {pixcake::VersionLimit{"color.global", 1}, pixcake::VersionLimit{"color.style", 1}};
}

void test_compile_is_read_only_and_uses_stable_fields() {
  auto document = *pixcake::EditDocument::create("img-1");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.set_preview("color.global", "temperature", "10") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(0, "color.global", "temperature", limits(), files, "/docs/img.pxedit") ==
         pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.style", "amount", "100") == pixcake::ResultCode::Ok);
  EXPECT(document.commit_preview(1, "color.style", "amount", limits(), files, "/docs/img.pxedit") ==
         pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.global", "temperature", "20") == pixcake::ResultCode::Ok);
  document.store_fragment("future.tool", 1, {1, 2, 3});
  const auto before = document.encode();
  const auto revision = document.revision();

  const auto result = pixcake::render::compile(document);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.description.schema_version == pixcake::render::kRenderDescriptionVersion);
  EXPECT(result.description.image_id == "img-1");
  EXPECT(result.description.editing_instance_id == "img-1");
  EXPECT(result.description.document_revision == revision);
  EXPECT(result.description.operations.size() == 2);
  if (result.description.operations.size() == 2) {
    EXPECT(result.description.operations[0].capability_id == "color.style");
    EXPECT(result.description.operations[0].stage == "color");
    EXPECT(result.description.operations[0].program == "pixcake.color.style");
    EXPECT(result.description.operations[0].values.size() == 1);
    EXPECT(result.description.operations[0].values[0].channel == "amount");
    EXPECT(result.description.operations[1].capability_id == "color.global");
    EXPECT(result.description.operations[1].stage == "color");
    EXPECT(result.description.operations[1].program == "pixcake.color.global");
    EXPECT(result.description.operations[1].values[0].channel == "temperature");
    EXPECT(result.description.operations[1].values[0].numeric_value == 20.0);
  }
  EXPECT(!result.description.diagnostics.empty());
  EXPECT(document.encode() == before);
  EXPECT(document.revision() == revision);
}

void test_preview_without_committed_fragment_is_renderable() {
  auto document = *pixcake::EditDocument::create("img-preview");
  EXPECT(document.set_preview("color.global", "temperature", "17") == pixcake::ResultCode::Ok);
  const auto result = pixcake::render::compile(document);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.description.operations.size() == 1);
  if (result.description.operations.size() == 1) {
    EXPECT(result.description.operations[0].capability_id == "color.global");
    EXPECT(result.description.operations[0].values.size() == 1);
    EXPECT(result.description.operations[0].values[0].numeric_value == 17.0);
  }
  EXPECT(document.fragments().empty());
}

void test_c05_profile_choice_is_rendered_as_choice_index() {
  auto document = *pixcake::EditDocument::create("img-profile");
  EXPECT(document.set_preview("color.global", "profile", "monochrome") == pixcake::ResultCode::Ok);
  const auto result = pixcake::render::compile(document);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.description.operations.size() == 1);
  if (result.description.operations.size() == 1) {
    const auto& operation = result.description.operations[0];
    EXPECT(operation.enabled);
    EXPECT(operation.values.size() == 1);
    if (operation.values.size() == 1) {
      EXPECT(operation.values[0].display_value == "monochrome");
      EXPECT(operation.values[0].numeric_value == 2.0);
    }
  }
}

void test_capability_order_is_stable_across_parameters() {
  auto document = *pixcake::EditDocument::create("img-order");
  EXPECT(document.set_preview("color.global", "contrast", "1") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "temperature", "10") == pixcake::ResultCode::Ok);
  const auto result = pixcake::render::compile(document);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.description.operations.size() == 2);
  if (result.description.operations.size() == 2) {
    EXPECT(result.description.operations[0].capability_id == "color.global");
    EXPECT(result.description.operations[1].capability_id == "color.local");
  }
}

void test_too_new_fragment_is_skipped_and_preserved() {
  auto document = *pixcake::EditDocument::create("img-2");
  document.store_fragment("color.global", 2, {9, 9});
  const auto before = document.encode();
  const auto result = pixcake::render::compile(document);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.description.operations.empty());
  EXPECT(result.description.diagnostics.size() == 1);
  EXPECT(document.encode() == before);
  EXPECT(document.fragment("color.global")->version == 2);
  EXPECT(document.fragment("color.global")->inner == std::vector<std::uint8_t>({9, 9}));
}

void test_layer_and_mask_inputs_are_descriptive() {
  auto document = *pixcake::EditDocument::create("img-layer");
  auto layers = *pixcake::LayerDocument::create("img-layer", 0);
  EXPECT(layers.add_group("group", "Group", "") == pixcake::ResultCode::Ok);
  EXPECT(layers.add_layer("subject", "Subject") == pixcake::ResultCode::Ok);
  EXPECT(layers.move_child("subject", "group", 0) == pixcake::ResultCode::Ok);
  EXPECT(layers.set_blend("subject", "正片叠底") == pixcake::ResultCode::Ok);
  EXPECT(layers.set_opacity("subject", 80) == pixcake::ResultCode::Ok);
  EXPECT(layers.add_mask("subject") == pixcake::ResultCode::Ok);
  EXPECT(layers.set_mask_density("subject", 75) == pixcake::ResultCode::Ok);
  EXPECT(layers.set_mask_feather("subject", 5) == pixcake::ResultCode::Ok);
  EXPECT(layers.set_selection(true, 12) == pixcake::ResultCode::Ok);

  const auto result = pixcake::render::compile(document, layers);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  EXPECT(result.description.layers.size() == 3);
  EXPECT(result.description.selection.present);
  EXPECT(result.description.selection.inverted);
  EXPECT(result.description.selection.feather == 12);
  if (result.description.layers.size() == 3) {
    const auto& layer = result.description.layers[2];
    EXPECT(layer.id == "subject");
    EXPECT(layer.parent_id == "group");
    EXPECT(layer.blend_mode == "multiply");
    EXPECT(layer.opacity == 80);
    EXPECT(layer.mask_enabled);
    EXPECT(layer.mask_density == 75);
    EXPECT(layer.mask_feather == 5);
  }
}

// 图层蒙版羽化按参数模式 0 到 1000 检查，不再写死 100（Q-06）。
void test_q06_layer_mask_feather_above_100_renders() {
  auto document = *pixcake::EditDocument::create("img-layer");
  auto layers = *pixcake::LayerDocument::create("img-layer", 0);
  EXPECT(layers.add_layer("subject", "Subject") == pixcake::ResultCode::Ok);
  EXPECT(layers.add_mask("subject") == pixcake::ResultCode::Ok);
  EXPECT(layers.set_mask_feather("subject", 1000) == pixcake::ResultCode::Ok);
  const auto result = pixcake::render::compile(document, layers);
  bool found = false;
  for (const auto& layer : result.description.layers) {
    if (layer.id != "subject") continue;
    found = true;
    EXPECT(layer.enabled);
    EXPECT(layer.mask_feather == 1000);
  }
  EXPECT(found);
}

}  // namespace

// C-31：渲染把编译交给能力模块。黑白配置文件预览时鲜艳度不画（color.global 模块），
// 颗粒按 detail 模块的阶段出一条绘制描述。
void test_c31_render_compiles_through_capability_modules() {
  auto document = *pixcake::EditDocument::create("img-modules");
  EXPECT(document.set_preview("color.global", "vibrance", "10") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.global", "profile", "monochrome") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("detail", "grain_amount", "25") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("detail", "grain_size", "20") == pixcake::ResultCode::Ok);
  const auto result = pixcake::render::compile(document);
  EXPECT(result.code == pixcake::ResultCode::Ok);
  bool saw_global = false;
  bool saw_grain = false;
  for (const auto& operation : result.description.operations) {
    if (operation.capability_id == "color.global") {
      saw_global = true;
      for (const auto& value : operation.values) EXPECT(value.parameter_id != "vibrance");
    }
    if (operation.capability_id == "detail") {
      saw_grain = operation.stage == "detail.grain" && operation.values.size() == 2;
    }
  }
  EXPECT(saw_global);
  EXPECT(saw_grain);
}

int main() {
  test_q06_layer_mask_feather_above_100_renders();
  test_c31_render_compiles_through_capability_modules();
  test_compile_is_read_only_and_uses_stable_fields();
  test_preview_without_committed_fragment_is_renderable();
  test_c05_profile_choice_is_rendered_as_choice_index();
  test_capability_order_is_stable_across_parameters();
  test_too_new_fragment_is_skipped_and_preserved();
  test_layer_and_mask_inputs_are_descriptive();
  return g_failures == 0 ? 0 : 1;
}
