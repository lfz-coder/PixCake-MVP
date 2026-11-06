#include "pixcake/capabilities/module_set.hpp"
#include "pixcake/capability/capability_module.hpp"
#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/capability/fragment_codec.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <map>
#include <optional>
#include <string>
#include <utility>
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

class MapReader : public pixcake::ParameterReader {
 public:
  std::map<std::pair<std::string, std::string>, std::string> values;
  std::map<std::string, std::uint32_t> versions;
  pixcake::ImageConditions image;

  std::optional<std::string> value(const std::string& capability_id, const std::string& parameter_id) const override {
    const auto found = values.find({capability_id, parameter_id});
    if (found == values.end()) return std::nullopt;
    return found->second;
  }
  std::uint32_t fragment_version(const std::string& capability_id) const override {
    const auto found = versions.find(capability_id);
    return found == versions.end() ? 1u : found->second;
  }
  pixcake::ImageConditions conditions() const override { return image; }
};

pixcake::FragmentBytes fragment_of(std::vector<pixcake::ParameterValue> parameters,
                                   std::vector<pixcake::FragmentRecord> extra = {}, std::uint32_t version = 1) {
  std::vector<pixcake::FragmentRecord> records;
  for (const auto& parameter : parameters) {
    records.push_back({pixcake::kFragmentParameterRecord,
                       pixcake::encode_parameter_record(parameter.parameter_id, parameter.display_value)});
  }
  records.insert(records.end(), extra.begin(), extra.end());
  return {version, pixcake::encode_fragment_records(records)};
}

std::map<std::string, std::string> parameters_in(const pixcake::FragmentBytes& fragment,
                                                 std::vector<pixcake::FragmentRecord>* others = nullptr) {
  std::vector<pixcake::FragmentRecord> records;
  EXPECT(pixcake::decode_fragment_records(fragment.inner, records));
  std::map<std::string, std::string> out;
  for (const auto& record : records) {
    if (record.type != pixcake::kFragmentParameterRecord) {
      if (others != nullptr) others->push_back(record);
      continue;
    }
    std::string id;
    std::string value;
    EXPECT(pixcake::decode_parameter_record(record.payload, id, value));
    out[id] = value;
  }
  return out;
}

const pixcake::CapabilityModule& module(const char* id) {
  const auto* found = pixcake::find_capability_module(id);
  if (found == nullptr) {
    std::fprintf(stderr, "no module for %s\n", id);
    std::abort();
  }
  return *found;
}

std::vector<std::string> value_ids(const pixcake::DrawStep& step) {
  std::vector<std::string> ids;
  for (const auto& value : step.values) ids.push_back(value.parameter_id);
  return ids;
}

bool has(const std::vector<std::string>& ids, const char* id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// C-31：契约 §4 的每个能力都有模块，标识、模块标签、同步属性与描述表一致，参数模式只含自己的。
void test_c31_every_registered_capability_has_a_module() {
  const auto registry = pixcake::CapabilityRegistry::phase_one();
  EXPECT(pixcake::phase_one_capability_modules().size() == registry.descriptors().size());
  for (const auto& descriptor : registry.descriptors()) {
    const auto* found = pixcake::find_capability_module(descriptor.id);
    EXPECT(found != nullptr);
    if (found == nullptr) continue;
    EXPECT(found->id() == descriptor.id);
    EXPECT(found->module_label() == descriptor.module_label);
    EXPECT(found->sync_traits().presettable == descriptor.can_use_in_preset);
    EXPECT(found->sync_traits().batch_syncable == descriptor.can_batch_sync);
    EXPECT(found->sync_traits().blocked_reason == descriptor.batch_sync_blocked_reason);
    for (const auto* mode : found->parameter_modes()) EXPECT(mode->capability_id == descriptor.id);
  }
  EXPECT(pixcake::find_capability_module("layer") == nullptr);
  EXPECT(pixcake::find_capability_module("grain") == nullptr);
  EXPECT(module("detail").parameter_modes().size() == 7);
}

// C-31：写回只写本能力的参数；未知参数与未知记录原样带上（架构 7.2“未知字段留在片段里”）。
void test_c31_write_back_replaces_or_overlays_and_keeps_unknown_fields() {
  const auto& global = module("color.global");
  const pixcake::FragmentRecord future{9, {1, 2, 3}};
  const auto existing = fragment_of({{"vibrance", "10"}, {"future_param", "x"}}, {future});

  const auto overlay = global.write_back(existing, {{"saturation", "5"}}, pixcake::WriteMode::Overlay);
  EXPECT(overlay.code == pixcake::ResultCode::Ok);
  std::vector<pixcake::FragmentRecord> others;
  auto params = parameters_in(overlay.fragment, &others);
  EXPECT(params.size() == 3 && params["vibrance"] == "10" && params["saturation"] == "5" && params["future_param"] == "x");
  EXPECT(others.size() == 1 && others[0].type == 9 && others[0].payload == future.payload);

  const auto replace = global.write_back(existing, {{"saturation", "5"}}, pixcake::WriteMode::Replace);
  EXPECT(replace.code == pixcake::ResultCode::Ok);
  others.clear();
  params = parameters_in(replace.fragment, &others);
  EXPECT(params.size() == 2 && params.count("vibrance") == 0 && params["future_param"] == "x");
  EXPECT(others.size() == 1);

  const auto fresh = global.write_back(std::nullopt, {{"vibrance", "-20"}}, pixcake::WriteMode::Overlay);
  EXPECT(fresh.code == pixcake::ResultCode::Ok && fresh.fragment.version == 1);
  EXPECT(parameters_in(fresh.fragment)["vibrance"] == "-20");

  EXPECT(global.write_back(existing, {{"vibrance", "101"}}, pixcake::WriteMode::Overlay).code ==
         pixcake::ResultCode::StateConflict);
  EXPECT(global.write_back(existing, {{"grain_amount", "5"}}, pixcake::WriteMode::Overlay).code ==
         pixcake::ResultCode::StateConflict);
  EXPECT(global.write_back(fragment_of({{"vibrance", "1"}}, {}, 2), {{"vibrance", "2"}}, pixcake::WriteMode::Overlay)
             .code == pixcake::ResultCode::CapabilityTooNew);
  pixcake::FragmentBytes corrupt{1, {1, 0, 0}};
  EXPECT(global.write_back(corrupt, {{"vibrance", "2"}}, pixcake::WriteMode::Overlay).code ==
         pixcake::ResultCode::DocumentCorrupt);
}

void test_c31_extract_returns_known_values_and_the_fragment() {
  const auto& global = module("color.global");
  const auto fragment = fragment_of({{"vibrance", "10"}, {"future_param", "x"}}, {{9, {7}}});
  const auto extracted = global.extract(fragment);
  EXPECT(extracted.code == pixcake::ResultCode::Ok);
  EXPECT(extracted.values == std::vector<pixcake::ParameterValue>({{"vibrance", "10"}}));
  EXPECT(extracted.fragment == fragment);
  const auto newer = fragment_of({{"vibrance", "10"}}, {}, 2);
  const auto too_new = global.extract(newer);
  EXPECT(too_new.code == pixcake::ResultCode::CapabilityTooNew && too_new.fragment == newer && too_new.values.empty());
}

void test_c31_display_scale_comes_from_parameter_modes() {
  const auto vibrance = module("color.global").display_scale("vibrance");
  EXPECT(vibrance && vibrance->minimum == -100 && vibrance->maximum == 100 && vibrance->step == 1 &&
         vibrance->default_value == 0 && vibrance->center_line);
  EXPECT(vibrance && !vibrance->colors_confirmed && vibrance->left_color.empty());
  const auto amount = module("detail").display_scale("grain_amount");
  EXPECT(amount && amount->minimum == 0 && amount->maximum == 100 && !amount->center_line);
  const auto gray = module("color.global").display_scale("levels_rgb_input_gray");
  EXPECT(gray && !gray->step_confirmed);
  EXPECT(!module("color.global").display_scale("profile"));
  EXPECT(!module("color.global").display_scale("grain_amount"));
}

// 架构第 623 行：坏片只让全身美型不可用。其余能力不因坏片禁用。
void test_c31_only_body_reports_broken_image() {
  pixcake::ImageConditions ok;
  pixcake::ImageConditions broken;
  broken.broken = true;
  for (const auto* found : pixcake::phase_one_capability_modules()) {
    EXPECT(found->disabled_reason(ok).empty());
    EXPECT(found->disabled_reason(broken) == (found->id() == "portrait.body" ? "坏片" : ""));
  }
}

// 架构第 6.7 节：人像能力在检测未完成时不可用，不编造几何。
void test_c31_portrait_needs_detection() {
  const std::set<std::string> portrait = {"portrait.blemish", "portrait.skin", "portrait.reshape", "portrait.feature",
                                          "portrait.body"};
  pixcake::ImageConditions pending;
  pending.detection_pending = true;
  for (const auto* found : pixcake::phase_one_capability_modules()) {
    EXPECT(found->disabled_reason(pending) == (portrait.count(found->id()) ? "检测未完成" : ""));
  }
  MapReader reader;
  reader.values[{"portrait.feature", "eye_glint_vertical"}] = "10";
  EXPECT(module("portrait.feature").compile(reader).steps.size() == 1);
  reader.image = pending;
  const auto skipped = module("portrait.feature").compile(reader);
  EXPECT(skipped.steps.empty());
  EXPECT(skipped.diagnostics.size() == 1 && !skipped.diagnostics[0].blocking &&
         skipped.diagnostics[0].message.find("检测未完成") != std::string::npos);
}

// 需求第 12.3 节：换天空用内置素材或上传的素材；没有素材时不可用。
void test_c31_sky_needs_assets() {
  pixcake::ImageConditions missing;
  missing.assets_missing = true;
  for (const auto* found : pixcake::phase_one_capability_modules()) {
    EXPECT(found->disabled_reason(missing) == (found->id() == "image.sky" ? "缺少素材" : ""));
  }
}

// 需求第 590、905 行：纯色背景功能用在非纯色图上无效果，不报错；能力本身仍可用。
void test_c31_solid_background_parameter_has_no_effect_on_other_backgrounds() {
  const auto& background = module("image.background");
  MapReader reader;
  reader.values[{"image.background", "light_uniform_brightness"}] = "20";
  EXPECT(background.compile(reader).steps.size() == 1);
  reader.image.not_solid_color = true;
  EXPECT(background.disabled_reason(reader.image).empty());
  EXPECT(!background.parameter_disabled_reason(reader, "light_uniform_brightness").empty());
  const auto compiled = background.compile(reader);
  EXPECT(compiled.steps.empty());
  for (const auto& diagnostic : compiled.diagnostics) EXPECT(!diagnostic.blocking);
}

// 架构第 7.5 节：跳到创意的入口声明在能力上，编译成跳转；能力不实现生成。
void test_c31_generation_entries_compile_to_jumps() {
  const std::map<std::string, std::vector<std::string>> expected = {
      {"portrait.skin", {"creative.face_redraw"}},
      {"portrait.body", {"creative.slim_belly", "creative.add_abs"}},
      {"image.background",
       {"creative.remove_passerby", "creative.fill_grass", "creative.backdrop", "creative.winter_snow"}},
      {"image.idphoto", {"creative.idphoto_clothes"}},
      {"crop", {"creative.outpaint"}},
  };
  for (const auto* found : pixcake::phase_one_capability_modules()) {
    std::vector<std::string> tools;
    for (const auto& entry : found->generation_entries()) {
      tools.push_back(entry.tool_id);
      EXPECT(!entry.label.empty());
    }
    const auto wanted = expected.find(found->id());
    EXPECT(tools == (wanted == expected.end() ? std::vector<std::string>{} : wanted->second));
  }
  const auto jump =
      module("image.background").compile_jump("creative.remove_passerby", {"img-1", 7, {"mask-1"}});
  EXPECT(jump && jump->capability_id == "image.background" && jump->tool_id == "creative.remove_passerby" &&
         jump->image_id == "img-1" && jump->revision == 7 &&
         jump->suggested_mask_ids == std::vector<std::string>({"mask-1"}));
  EXPECT(!module("image.background").compile_jump("creative.face_redraw", {"img-1", 7, {}}));
  EXPECT(!module("color.global").compile_jump("creative.outpaint", {"img-1", 7, {}}));
}

// 架构第 154 行：每个精修能力一个子目录，目录里有自己的模块工厂。
void test_c31_every_capability_has_its_own_directory() {
  namespace fs = std::filesystem;
  const fs::path root = fs::path(PIXCAKE_SOURCE_DIR) / "kernel" / "capabilities";
  std::set<std::string> wanted;
  const auto registry = pixcake::CapabilityRegistry::phase_one();
  for (const auto& descriptor : registry.descriptors()) {
    std::string dir = descriptor.id;
    std::replace(dir.begin(), dir.end(), '.', '_');
    wanted.insert(dir);
    EXPECT(fs::exists(root / dir / "src" / (dir + "_module.cpp")));
    EXPECT(fs::exists(root / dir / "include" / "pixcake" / "capabilities" / dir / (dir + "_module.hpp")));
    EXPECT(fs::exists(root / dir / "CMakeLists.txt"));
  }
  std::set<std::string> present;
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.is_directory() && entry.path().filename() != "include") present.insert(entry.path().filename().string());
  }
  for (const auto& name : present) {
    if (wanted.count(name) == 0) std::fprintf(stderr, "extra capability directory: %s\n", name.c_str());
  }
  for (const auto& name : wanted) {
    if (present.count(name) == 0) std::fprintf(stderr, "missing capability directory: %s\n", name.c_str());
  }
  EXPECT(present == wanted);
}

void test_c31_migrate_keeps_fragment_on_failure() {
  const auto& detail = module("detail");
  const auto current = fragment_of({{"grain_amount", "10"}});
  const auto same = detail.migrate(current);
  EXPECT(same.code == pixcake::ResultCode::Ok && same.fragment == current);
  const auto zero = fragment_of({{"grain_amount", "10"}}, {}, 0);
  const auto failed = detail.migrate(zero);
  EXPECT(failed.code == pixcake::ResultCode::StateConflict && failed.fragment == zero);
  const auto newer = fragment_of({{"grain_amount", "10"}}, {}, 2);
  const auto too_new = detail.migrate(newer);
  EXPECT(too_new.code == pixcake::ResultCode::CapabilityTooNew && too_new.fragment == newer);
}

// 2026-11-05 用户裁定：颗粒效果为 0 时，下面各项不可调节，也不参与绘制。
void test_c31_detail_draws_grain_only_when_amount_is_nonzero() {
  const auto& detail = module("detail");
  MapReader reader;
  reader.values[{"detail", "grain_size"}] = "20";
  EXPECT(detail.compile(reader).steps.empty());
  EXPECT(!detail.parameter_disabled_reason(reader, "grain_size").empty());
  EXPECT(detail.parameter_disabled_reason(reader, "grain_amount").empty());

  reader.values[{"detail", "grain_amount"}] = "25";
  const auto compiled = detail.compile(reader);
  EXPECT(compiled.steps.size() == 1);
  if (compiled.steps.size() == 1) {
    const auto& step = compiled.steps[0];
    EXPECT(step.capability_id == "detail" && step.stage == "detail.grain" && step.program == "pixcake.detail");
    EXPECT(value_ids(step) == std::vector<std::string>({"grain_amount", "grain_size"}));
  }
  EXPECT(detail.parameter_disabled_reason(reader, "grain_size").empty());
}

// C-05：黑白配置文件下 HSL、鲜艳度、饱和度不参与绘制，黑白组参与；其他配置文件相反。数据都不删。
void test_c31_color_global_draws_by_profile() {
  const auto& global = module("color.global");
  MapReader reader;
  reader.values[{"color.global", "vibrance"}] = "10";
  reader.values[{"color.global", "hsl_hue_red"}] = "5";
  reader.values[{"color.global", "bw_mix_red"}] = "7";
  auto compiled = global.compile(reader);
  EXPECT(compiled.steps.size() == 1);
  if (compiled.steps.size() == 1) {
    const auto ids = value_ids(compiled.steps[0]);
    EXPECT(has(ids, "vibrance") && has(ids, "hsl_hue_red") && !has(ids, "bw_mix_red"));
  }
  reader.values[{"color.global", "profile"}] = "monochrome";
  compiled = global.compile(reader);
  EXPECT(compiled.steps.size() == 1);
  if (compiled.steps.size() == 1) {
    const auto ids = value_ids(compiled.steps[0]);
    EXPECT(has(ids, "profile") && has(ids, "bw_mix_red") && !has(ids, "vibrance") && !has(ids, "hsl_hue_red"));
  }
}

void test_c31_interactive_compile_carries_only_the_changed_value() {
  const auto& global = module("color.global");
  MapReader reader;
  reader.values[{"color.global", "vibrance"}] = "10";
  reader.values[{"color.global", "saturation"}] = "5";
  const auto compiled = global.compile_interactive(reader, "saturation");
  EXPECT(compiled.steps.size() == 1);
  if (compiled.steps.size() == 1) EXPECT(value_ids(compiled.steps[0]) == std::vector<std::string>({"saturation"}));
}

void test_c31_invalid_or_too_new_values_are_reported() {
  const auto& global = module("color.global");
  MapReader reader;
  reader.values[{"color.global", "vibrance"}] = "500";
  auto compiled = global.compile(reader);
  EXPECT(compiled.steps.size() == 1 && !compiled.steps[0].enabled);
  EXPECT(compiled.diagnostics.size() == 1 && compiled.diagnostics[0].blocking);
  reader.values[{"color.global", "vibrance"}] = "5";
  reader.versions["color.global"] = 2;
  compiled = global.compile(reader);
  EXPECT(compiled.steps.empty());
  EXPECT(compiled.diagnostics.size() == 1 && !compiled.diagnostics[0].blocking);
}

// C-31：预设、同步、粘贴经模块把来源片段写到目标（迁移 + 提取 + 取值校验）。
void test_c31_transfer_replaces_with_validated_source() {
  const auto source = fragment_of({{"vibrance", "10"}, {"future_param", "x"}}, {{9, {1}}});
  const auto target = fragment_of({{"saturation", "5"}});
  const auto moved = pixcake::transfer_fragment("color.global", source, target);
  EXPECT(moved.code == pixcake::ResultCode::Ok && !moved.keep_target);
  EXPECT(moved.fragment && *moved.fragment == source);
  const auto cleared = pixcake::transfer_fragment("color.global", std::nullopt, target);
  EXPECT(cleared.code == pixcake::ResultCode::Ok && !cleared.keep_target && !cleared.fragment);
  // 没有登记模块的标识：原样复制，保留片段。
  const auto unknown = pixcake::transfer_fragment("future.capability", source, std::nullopt);
  EXPECT(unknown.code == pixcake::ResultCode::Ok && unknown.fragment && *unknown.fragment == source);
}

void test_c31_transfer_copies_too_new_source() {
  const auto newer = fragment_of({{"vibrance", "999"}}, {}, 2);
  const auto moved = pixcake::transfer_fragment("color.global", newer, std::nullopt);
  EXPECT(moved.code == pixcake::ResultCode::Ok && moved.fragment && *moved.fragment == newer);
}

void test_c31_transfer_leaves_too_new_target() {
  const auto moved =
      pixcake::transfer_fragment("color.global", fragment_of({{"vibrance", "1"}}), fragment_of({}, {}, 2));
  EXPECT(moved.code == pixcake::ResultCode::Ok && moved.keep_target);
}

void test_c31_transfer_rejects_corrupt_or_invalid_source() {
  const pixcake::FragmentBytes corrupt{1, {1, 0, 0}};
  EXPECT(pixcake::transfer_fragment("color.global", corrupt, std::nullopt).code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(pixcake::transfer_fragment("color.global", fragment_of({{"vibrance", "500"}}), std::nullopt).code ==
         pixcake::ResultCode::StateConflict);
  EXPECT(pixcake::transfer_fragment("color.global", fragment_of({{"vibrance", "1"}}, {}, 0), std::nullopt).code ==
         pixcake::ResultCode::StateConflict);
}

}  // namespace

int main() {
  test_c31_transfer_replaces_with_validated_source();
  test_c31_transfer_copies_too_new_source();
  test_c31_transfer_leaves_too_new_target();
  test_c31_transfer_rejects_corrupt_or_invalid_source();
  test_c31_every_registered_capability_has_a_module();
  test_c31_write_back_replaces_or_overlays_and_keeps_unknown_fields();
  test_c31_extract_returns_known_values_and_the_fragment();
  test_c31_display_scale_comes_from_parameter_modes();
  test_c31_only_body_reports_broken_image();
  test_c31_portrait_needs_detection();
  test_c31_sky_needs_assets();
  test_c31_solid_background_parameter_has_no_effect_on_other_backgrounds();
  test_c31_generation_entries_compile_to_jumps();
  test_c31_every_capability_has_its_own_directory();
  test_c31_migrate_keeps_fragment_on_failure();
  test_c31_detail_draws_grain_only_when_amount_is_nonzero();
  test_c31_color_global_draws_by_profile();
  test_c31_interactive_compile_carries_only_the_changed_value();
  test_c31_invalid_or_too_new_values_are_reported();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
