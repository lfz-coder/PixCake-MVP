#include "pixcake/render/render_description.hpp"
#include "pixcake/capability/fragment_codec.hpp"
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

// 局部调色的作用部位变成绘制步骤的语义蒙版；“整个画面”没有蒙版；部位不在表里的值被拒绝。
void test_local_region_becomes_a_semantic_mask() {
  auto document = *pixcake::EditDocument::create("img-region");
  EXPECT(document.set_preview("color.local", "exposure", "0.5") == pixcake::ResultCode::Ok);
  auto result = pixcake::render::compile(document);
  EXPECT(result.description.operations.size() == 1 && result.description.operations[0].mask_ids.empty());
  EXPECT(document.set_preview("color.local", "region", "person.face_skin") == pixcake::ResultCode::Ok);
  result = pixcake::render::compile(document);
  EXPECT(result.description.operations.size() == 1);
  if (result.description.operations.size() == 1) {
    const auto& operation = result.description.operations[0];
    EXPECT(operation.mask_ids == std::vector<std::string>{"semantic:person.face_skin"});
    for (const auto& value : operation.values) EXPECT(value.channel != "region");  // not a draw channel
  }
  EXPECT(!pixcake::validate_parameter_display_value("color.local", "region", "person.unknown"));
  EXPECT(pixcake::validate_parameter_display_value("color.local", "region", "person.hair"));
  // Gradients: the position parameters become part of the mask id, not draw channels.
  EXPECT(document.set_preview("color.local", "region", "radial") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "gradient_x", "30") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "gradient_size", "20") == pixcake::ResultCode::Ok);
  result = pixcake::render::compile(document);
  EXPECT(result.description.operations.size() == 1);
  if (result.description.operations.size() == 1) {
    const auto& operation = result.description.operations[0];
    EXPECT(operation.mask_ids == std::vector<std::string>{"gradient:radial:30:50:0:20:50:100"});
    for (const auto& value : operation.values) EXPECT(value.channel.rfind("gradient_", 0) != 0 && value.channel != "region");
  }
  EXPECT(document.set_preview("color.local", "region", "all") == pixcake::ResultCode::Ok);
  result = pixcake::render::compile(document);
  EXPECT(result.description.operations.size() == 1 && result.description.operations[0].mask_ids.empty());
}

// 多个蒙版（实例）：带 m<N>: 前缀的参数登记到基础参数；前缀不合法被拒；每个实例一个绘制步骤，实例 0 在前。
void test_local_instances_compile_in_order() {
  using pixcake::find_parameter_mode;
  int instance = -1;
  std::string_view base;
  EXPECT(pixcake::split_local_parameter("exposure", instance, base) && instance == 0 && base == "exposure");
  EXPECT(pixcake::split_local_parameter("m2:exposure", instance, base) && instance == 2 && base == "exposure");
  EXPECT(pixcake::split_local_parameter("m32:gradient_x", instance, base) && instance == 32 && base == "gradient_x");
  for (const char* bad : {"m0:exposure", "m33:exposure", "m02:exposure", "mx:exposure", "m:exposure", "m2:"}) {
    EXPECT(!pixcake::split_local_parameter(bad, instance, base));
    EXPECT(find_parameter_mode("color.local", bad) == nullptr);
  }
  EXPECT(find_parameter_mode("color.local", "m3:exposure") == find_parameter_mode("color.local", "exposure"));
  EXPECT(find_parameter_mode("color.global", "m3:exposure") == nullptr);  // only local colour has instances
  EXPECT(pixcake::validate_parameter_display_value("color.local", "m3:exposure", "1.5"));
  EXPECT(!pixcake::validate_parameter_display_value("color.local", "m3:exposure", "9"));
  EXPECT(pixcake::validate_parameter_display_value("color.local", "m2:region", "person.hair"));

  auto document = *pixcake::EditDocument::create("img-instances");
  // Only previews, only instances 2 and 1 (set out of order): both must compile, instance 1 before 2.
  EXPECT(document.set_preview("color.local", "m2:exposure", "1") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "m2:region", "person.hair") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "m1:exposure", "-1") == pixcake::ResultCode::Ok);
  EXPECT(document.set_preview("color.local", "exposure", "0.5") == pixcake::ResultCode::Ok);
  auto result = pixcake::render::compile(document);
  EXPECT(result.description.operations.size() == 3);
  if (result.description.operations.size() == 3) {
    const auto& ops = result.description.operations;
    EXPECT(ops[0].values.size() == 1 && ops[0].values[0].parameter_id == "exposure");
    EXPECT(ops[1].values.size() == 1 && ops[1].values[0].parameter_id == "m1:exposure" && ops[1].mask_ids.empty());
    EXPECT(ops[2].values.size() == 1 && ops[2].values[0].parameter_id == "m2:exposure");
    EXPECT(ops[2].mask_ids == std::vector<std::string>{"semantic:person.hair"});
    for (const auto& op : ops) EXPECT(op.values[0].channel == "exposure");
  }
  // Without instance 0 the others still draw.
  auto only_two = *pixcake::EditDocument::create("img-instances-2");
  EXPECT(only_two.set_preview("color.local", "m5:contrast", "10") == pixcake::ResultCode::Ok);
  result = pixcake::render::compile(only_two);
  EXPECT(result.description.operations.size() == 1);
  // Sub-items of one mask: part grammar, validation, and the combined mask id.
  int item = -1;
  EXPECT(pixcake::split_local_item("region", item, base) && item == 0 && base == "region");
  EXPECT(pixcake::split_local_item("s3:gradient_x", item, base) && item == 3 && base == "gradient_x");
  EXPECT(!pixcake::split_local_item("s17:region", item, base) && !pixcake::split_local_item("s0:region", item, base));
  EXPECT(find_parameter_mode("color.local", "m2:s3:region") == find_parameter_mode("color.local", "region"));
  EXPECT(find_parameter_mode("color.local", "s2:mode") == find_parameter_mode("color.local", "mode"));
  EXPECT(find_parameter_mode("color.local", "s2:exposure") == nullptr);  // tone belongs to the whole mask
  EXPECT(pixcake::validate_parameter_display_value("color.local", "s2:mode", "subtract"));
  EXPECT(!pixcake::validate_parameter_display_value("color.local", "s2:mode", "xor"));
  auto items = *pixcake::EditDocument::create("img-items");
  EXPECT(items.set_preview("color.local", "m1:exposure", "1") == pixcake::ResultCode::Ok);
  EXPECT(items.set_preview("color.local", "m1:region", "person.skin.all") == pixcake::ResultCode::Ok);
  EXPECT(items.set_preview("color.local", "m1:s1:region", "person.lips") == pixcake::ResultCode::Ok);
  EXPECT(items.set_preview("color.local", "m1:s1:mode", "subtract") == pixcake::ResultCode::Ok);
  EXPECT(items.set_preview("color.local", "m1:s2:region", "linear") == pixcake::ResultCode::Ok);
  EXPECT(items.set_preview("color.local", "m1:s2:gradient_x", "30") == pixcake::ResultCode::Ok);
  result = pixcake::render::compile(items);
  EXPECT(result.description.operations.size() == 1);
  if (result.description.operations.size() == 1) {
    EXPECT(result.description.operations[0].mask_ids ==
           std::vector<std::string>{"combined:+semantic:person.skin.all|-semantic:person.lips|+gradient:linear:30:50:0:50:50:100"});
    for (const auto& value : result.description.operations[0].values) EXPECT(value.channel != "mode");
  }
  // One "add" item alone keeps the plain id; a lone "subtract" item is a combination (it removes from nothing).
  auto one = *pixcake::EditDocument::create("img-one-item");
  EXPECT(one.set_preview("color.local", "exposure", "1") == pixcake::ResultCode::Ok);
  EXPECT(one.set_preview("color.local", "s1:region", "person.hair") == pixcake::ResultCode::Ok);
  result = pixcake::render::compile(one);
  EXPECT(result.description.operations.size() == 1 &&
         result.description.operations[0].mask_ids == std::vector<std::string>{"semantic:person.hair"});
  // Local colour fragments are version 2 now; the readable limit follows the registry.
  bool found = false;
  for (const auto& limit : pixcake::phase_one_version_limits()) {
    if (limit.capability_id == "color.local") { found = true; EXPECT(limit.max_readable_version == 2); }
    if (limit.capability_id == "color.global") EXPECT(limit.max_readable_version == 1);
  }
  EXPECT(found);
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

// C-33：同一帧追色只有一个来源（架构“写回的存放与切换”）。
std::vector<std::uint8_t> records(std::vector<std::pair<std::uint32_t, std::pair<std::string, std::string>>> items) {
  std::vector<pixcake::FragmentRecord> out;
  for (const auto& item : items) {
    out.push_back({item.first, pixcake::encode_parameter_record(item.second.first, item.second.second)});
  }
  return pixcake::encode_fragment_records(out);
}

pixcake::EditDocument matched_document(const std::string& precise) {
  auto document = *pixcake::EditDocument::create("img-match");
  document.store_fragment("color.global", 1,
                          records({{pixcake::kFragmentParameterRecord, {"exposure", "0.50"}},
                                   {pixcake::kFragmentMatchWritebackRecord, {"exposure", "1.00"}}}));
  document.store_fragment("color.match", 1,
                          records({{pixcake::kFragmentParameterRecord, {"amount", "100"}},
                                   {pixcake::kFragmentParameterRecord, {"precise", precise}}}));
  return document;
}

const pixcake::render::RenderOperation* operation_for(const pixcake::render::RenderDescription& description,
                                                     const std::string& capability_id) {
  for (const auto& operation : description.operations) {
    if (operation.capability_id == capability_id) return &operation;
  }
  return nullptr;
}

std::string value_of(const pixcake::render::RenderOperation* operation, const std::string& parameter_id) {
  if (operation == nullptr) return "";
  for (const auto& value : operation->values) {
    if (value.parameter_id == parameter_id) return value.display_value;
  }
  return "";
}

void test_c33_precise_off_draws_amount_and_user_values() {
  const auto result = pixcake::render::compile(matched_document("off"));
  EXPECT(value_of(operation_for(result.description, "color.global"), "exposure") == "0.50");
  EXPECT(value_of(operation_for(result.description, "color.match"), "amount") == "100");
  EXPECT(value_of(operation_for(result.description, "color.match"), "precise").empty());
}

void test_c33_precise_on_draws_written_back_values_only() {
  const auto result = pixcake::render::compile(matched_document("on"));
  EXPECT(value_of(operation_for(result.description, "color.global"), "exposure") == "1.00");
  EXPECT(operation_for(result.description, "color.match") == nullptr);
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
  test_c33_precise_off_draws_amount_and_user_values();
  test_c33_precise_on_draws_written_back_values_only();
  test_q06_layer_mask_feather_above_100_renders();
  test_c31_render_compiles_through_capability_modules();
  test_compile_is_read_only_and_uses_stable_fields();
  test_preview_without_committed_fragment_is_renderable();
  test_c05_profile_choice_is_rendered_as_choice_index();
  test_capability_order_is_stable_across_parameters();
  test_local_region_becomes_a_semantic_mask();
  test_local_instances_compile_in_order();
  test_too_new_fragment_is_skipped_and_preserved();
  test_layer_and_mask_inputs_are_descriptive();
  return g_failures == 0 ? 0 : 1;
}
