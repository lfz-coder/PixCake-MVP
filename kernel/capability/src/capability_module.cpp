#include "pixcake/capability/capability_module.hpp"
#include "pixcake/mask/combined_mask.hpp"
#include "pixcake/mask/gradient_mask.hpp"
#include "pixcake/mask/range_mask.hpp"
#include "pixcake/mask/semantic_parts.hpp"

#include "pixcake/capability/fragment_codec.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <set>
#include <utility>

namespace pixcake {
namespace {

bool parse_number(const std::string& text, double& value) {
  if (text.empty()) return false;
  char* end = nullptr;
  errno = 0;
  value = std::strtod(text.c_str(), &end);
  return errno == 0 && end == text.c_str() + text.size() && std::isfinite(value);
}

DrawStep make_step(const ParameterMode& mode) {
  DrawStep step;
  step.capability_id = mode.capability_id;
  step.stage = mode.render_stage;
  step.program = mode.render_program;
  const auto pipeline_order = phase_one_capability_order(mode.capability_id);
  step.order = pipeline_order ? *pipeline_order : mode.order;
  return step;
}

}  // namespace

ParameterBackedModule::ParameterBackedModule(CapabilityDescriptor descriptor, ModuleTraits traits)
    : descriptor_(std::move(descriptor)), traits_(std::move(traits)) {}

const std::string& ParameterBackedModule::id() const { return descriptor_.id; }

const std::string& ParameterBackedModule::module_label() const { return descriptor_.module_label; }

std::vector<const ParameterMode*> ParameterBackedModule::parameter_modes() const {
  std::vector<const ParameterMode*> out;
  for (const auto& mode : phase_one_parameter_modes()) {
    if (mode.capability_id == descriptor_.id) out.push_back(&mode);
  }
  return out;
}

SyncTraits ParameterBackedModule::sync_traits() const {
  return SyncTraits{descriptor_.can_use_in_preset, descriptor_.can_batch_sync, descriptor_.batch_sync_blocked_reason};
}

std::uint32_t ParameterBackedModule::current_version() const {
  std::uint32_t version = 1;
  for (const auto* mode : parameter_modes()) version = std::max(version, mode->schema_version);
  return version;
}

ExtractResult ParameterBackedModule::extract(const FragmentBytes& fragment) const {
  ExtractResult result;
  result.fragment = fragment;
  if (fragment.version > current_version()) {
    result.code = ResultCode::CapabilityTooNew;
    return result;
  }
  std::vector<FragmentRecord> records;
  if (!decode_fragment_records(fragment.inner, records)) {
    result.code = ResultCode::DocumentCorrupt;
    return result;
  }
  for (const auto& record : records) {
    if (record.type != kFragmentParameterRecord) continue;
    ParameterValue value;
    if (!decode_parameter_record(record.payload, value.parameter_id, value.display_value)) {
      result.code = ResultCode::DocumentCorrupt;
      result.values.clear();
      return result;
    }
    if (find_parameter_mode(descriptor_.id, value.parameter_id) != nullptr) result.values.push_back(std::move(value));
  }
  return result;
}

FragmentResult ParameterBackedModule::write_back(const std::optional<FragmentBytes>& existing,
                                                 const std::vector<ParameterValue>& values, WriteMode mode) const {
  FragmentResult result;
  for (const auto& value : values) {
    if (find_parameter_mode(descriptor_.id, value.parameter_id) == nullptr ||
        !validate_parameter_display_value(descriptor_.id, value.parameter_id, value.display_value)) {
      result.code = ResultCode::StateConflict;
      return result;
    }
  }
  std::vector<FragmentRecord> kept;
  result.fragment.version = current_version();
  if (existing) {
    if (existing->version > current_version()) {
      result.code = ResultCode::CapabilityTooNew;
      return result;
    }
    result.fragment.version = std::max(existing->version, current_version());  // 旧版本片段写入时升级
    std::vector<FragmentRecord> records;
    if (!decode_fragment_records(existing->inner, records)) {
      result.code = ResultCode::DocumentCorrupt;
      return result;
    }
    for (auto& record : records) {
      if (mode == WriteMode::MatchWriteback) {
        if (record.type == kFragmentMatchWritebackRecord) {
          std::string id;
          std::string display;
          if (!decode_parameter_record(record.payload, id, display)) {
            result.code = ResultCode::DocumentCorrupt;
            return result;
          }
          const bool given = std::any_of(values.begin(), values.end(),
                                         [&id](const ParameterValue& value) { return value.parameter_id == id; });
          if (given) continue;
        }
        kept.push_back(std::move(record));
        continue;
      }
      if (record.type == kFragmentParameterRecord) {
        std::string id;
        std::string display;
        if (!decode_parameter_record(record.payload, id, display)) {
          result.code = ResultCode::DocumentCorrupt;
          return result;
        }
        const bool known = find_parameter_mode(descriptor_.id, id) != nullptr;
        const bool given = std::any_of(values.begin(), values.end(),
                                       [&id](const ParameterValue& value) { return value.parameter_id == id; });
        // 替换去掉已登记的参数；叠加只去掉这次给出的参数。未登记的参数是未知字段，原样保留。
        if ((mode == WriteMode::Replace && known) || given) continue;
      }
      kept.push_back(std::move(record));
    }
  }
  const std::uint32_t written_type =
      mode == WriteMode::MatchWriteback ? kFragmentMatchWritebackRecord : kFragmentParameterRecord;
  for (const auto& value : values) {
    kept.push_back({written_type, encode_parameter_record(value.parameter_id, value.display_value)});
  }
  result.fragment.inner = encode_fragment_records(kept);
  return result;
}

bool ParameterBackedModule::draws_parameter(const ParameterReader& reader, const ParameterMode& mode) const {
  return parameter_disabled_reason(reader, mode.parameter_id).empty();
}

namespace {

// 颜色范围、亮度范围子项的蒙版编号：取样值读不到或不合格就用默认值。region 不是这两种时返回空。
std::string range_mask_id_of(const ParameterReader& reader, const std::string& capability, const std::string& prefix,
                             const std::string& region) {
  if (region != "color_range" && region != "luminance_range") return {};
  const auto number = [&](const char* name, int fallback) {
    const auto text = reader.value(capability, prefix + name);
    double parsed = 0.0;
    return text && parse_number(*text, parsed) && validate_parameter_display_value(capability, prefix + name, *text)
               ? static_cast<int>(std::lround(parsed))
               : fallback;
  };
  if (region == "color_range") {
    mask::ColorRangeMask range;
    range.r = number("range_r", range.r);
    range.g = number("range_g", range.g);
    range.b = number("range_b", range.b);
    range.adjust = number("range_adjust", range.adjust);
    return mask::color_range_mask_id(range);
  }
  mask::LuminanceRangeMask range;
  range.a = number("lum_a", range.a);
  range.b = number("lum_b", range.b);
  range.c = number("lum_c", range.c);
  range.d = number("lum_d", range.d);
  // 手柄次序不对（a≤b≤c≤d）时按次序夹住，保证编号合法。
  range.b = std::max(range.b, range.a);
  range.c = std::max(range.c, range.b);
  range.d = std::max(range.d, range.c);
  return mask::luminance_range_mask_id(range);
}

}  // namespace

ModuleCompile ParameterBackedModule::compile_modes(const ParameterReader& reader,
                                                   const std::vector<const ParameterMode*>& modes,
                                                   const std::string& only, int instance) const {
  ModuleCompile result;
  const std::string prefix = local_instance_prefix(instance);
  const std::string unavailable = disabled_reason(reader.conditions());
  if (!unavailable.empty()) {
    result.diagnostics.push_back({descriptor_.id, unavailable + "，保留片段但跳过绘制", false});
    return result;
  }
  const std::uint32_t version = reader.fragment_version(descriptor_.id);
  for (const auto* mode : modes) {
    if (version > mode->schema_version) {
      result.diagnostics.push_back({descriptor_.id, "能力片段版本过新，保留片段但跳过绘制", false});
      return result;
    }
  }
  DrawStep step;
  bool created = false;
  std::string region_mask;  // 局部调色的作用部位
  mask::GradientMask gradient;
  bool gradient_region = false;
  std::string range_region;  // color_range 或 luminance_range
  std::set<std::string> channels;
  for (const auto* mode : modes) {
    if (!only.empty() && mode->parameter_id != only) continue;
    if (mode->text) continue;  // 文本参数（蒙版名称）不是绘制通道
    const auto value = reader.value(descriptor_.id, prefix + mode->parameter_id);
    if (!value) continue;
    double numeric = 0.0;
    bool numeric_ok = false;
    if (!mode->choices.empty()) {
      // 枚举参数：数值是可选值在参数模式里的序号，显示值保留稳定标识。
      const auto found = std::find(mode->choices.begin(), mode->choices.end(), *value);
      numeric_ok = found != mode->choices.end();
      numeric = static_cast<double>(found - mode->choices.begin());
    } else {
      numeric_ok = parse_number(*value, numeric);
    }
    if (!numeric_ok || !validate_parameter_display_value(descriptor_.id, mode->parameter_id, *value)) {
      step = make_step(*mode);
      step.enabled = false;
      step.disabled_reason = "参数值不符合参数模式";
      result.diagnostics.push_back({descriptor_.id, step.disabled_reason, true});
      result.steps.push_back(std::move(step));
      return result;
    }
    if (!draws_parameter(reader, *mode)) continue;
    if (descriptor_.id == "color.local" && mode->parameter_id == "region") {
      if (*value == "linear" || *value == "radial") {
        gradient.radial = *value == "radial";
        gradient_region = true;
      } else if (*value == "color_range" || *value == "luminance_range") {
        range_region = *value;
      } else if (*value != "all") {
        region_mask = mask::semantic_mask_id(*value);
      }
      continue;
    }
    if (descriptor_.id == "color.local" &&
        (mode->parameter_id.rfind("range_", 0) == 0 || mode->parameter_id.rfind("lum_", 0) == 0)) {
      continue;  // 取样值不是绘制通道
    }
    if (descriptor_.id == "color.local" && mode->parameter_id == "mode") continue;  // 子项叠加方式，子项 0 没有“上一步”
    if (descriptor_.id == "color.local" && mode->parameter_id.rfind("gradient_", 0) == 0) {
      const int whole = static_cast<int>(std::lround(numeric));
      if (mode->parameter_id == "gradient_x") gradient.x = whole;
      if (mode->parameter_id == "gradient_y") gradient.y = whole;
      if (mode->parameter_id == "gradient_angle") gradient.angle = whole;
      if (mode->parameter_id == "gradient_size") gradient.size = whole;
      if (mode->parameter_id == "gradient_feather") gradient.feather = whole;
      if (mode->parameter_id == "gradient_ratio") gradient.ratio = whole;
      continue;  // 位置参数不是绘制通道
    }
    if (!created) {
      step = make_step(*mode);
      created = true;
    }
    if (!channels.insert(mode->render_channel).second) continue;
    step.values.push_back(DrawValue{prefix + mode->parameter_id, mode->render_channel, *value, numeric});
  }
  if (created) {
    if (step.values.empty()) {
      result.diagnostics.push_back({descriptor_.id, "片段没有可编译的参数值", false});
    } else {
      // 蒙版：子项 0（实例前缀下的 region 和 gradient_*）加子项 1..N（s<K>: 前缀）；只有一个“添加”子项时直接用它的编号，
      // 多个时合成一个组合编号（像素由 gpu 在子项蒙版都有之后生成）。整个画面的子项不参与。
      std::vector<mask::CombinedPart> parts;
      if (!region_mask.empty()) parts.push_back({false, region_mask});
      if (gradient_region) parts.push_back({false, mask::gradient_mask_id(gradient)});
      if (!range_region.empty()) parts.push_back({false, range_mask_id_of(reader, descriptor_.id, prefix, range_region)});
      if (descriptor_.id == "color.local") {
        for (int item = 1; item <= kMaxLocalItems; ++item) {
          const std::string item_prefix = prefix + local_item_prefix(item);
          const auto region = reader.value(descriptor_.id, item_prefix + "region");
          if (!region || *region == "all" || !validate_parameter_display_value(descriptor_.id, item_prefix + "region", *region)) {
            continue;
          }
          std::string id;
          if (*region == "linear" || *region == "radial") {
            mask::GradientMask g;
            g.radial = *region == "radial";
            const auto number = [&](const char* name, int fallback) {
              const auto text = reader.value(descriptor_.id, item_prefix + name);
              double parsed = 0.0;
              return text && parse_number(*text, parsed) &&
                             validate_parameter_display_value(descriptor_.id, item_prefix + name, *text)
                         ? static_cast<int>(std::lround(parsed))
                         : fallback;
            };
            g.x = number("gradient_x", g.x);
            g.y = number("gradient_y", g.y);
            g.angle = number("gradient_angle", g.angle);
            g.size = number("gradient_size", g.size);
            g.feather = number("gradient_feather", g.feather);
            g.ratio = number("gradient_ratio", g.ratio);
            id = mask::gradient_mask_id(g);
          } else if (*region == "color_range" || *region == "luminance_range") {
            id = range_mask_id_of(reader, descriptor_.id, item_prefix, *region);
          } else {
            id = mask::semantic_mask_id(*region);
          }
          const auto how = reader.value(descriptor_.id, item_prefix + "mode");
          parts.push_back({how == std::optional<std::string>("subtract"), std::move(id)});
        }
      }
      if (parts.size() == 1 && !parts[0].subtract) {
        step.mask_ids.push_back(parts[0].id);
      } else if (!parts.empty()) {
        step.mask_ids.push_back(mask::combined_mask_id(parts));
      }
      result.steps.push_back(std::move(step));
    }
  }
  return result;
}

ModuleCompile ParameterBackedModule::compile(const ParameterReader& reader) const {
  ModuleCompile result = compile_modes(reader, parameter_modes(), "");
  if (descriptor_.id != "color.local") return result;
  // 第 1..N 个蒙版：只要有任何一个参数就编译成一个绘制步骤，按实例编号先后排在实例 0 之后。
  const auto modes = parameter_modes();
  for (int instance = 1; instance <= kMaxLocalInstances; ++instance) {
    const std::string prefix = local_instance_prefix(instance);
    const bool present = std::any_of(modes.begin(), modes.end(), [&](const ParameterMode* mode) {
      return reader.value(descriptor_.id, prefix + mode->parameter_id).has_value();
    });
    if (!present) continue;
    ModuleCompile one = compile_modes(reader, modes, "", instance);
    for (auto& step : one.steps) result.steps.push_back(std::move(step));
    for (auto& diagnostic : one.diagnostics) result.diagnostics.push_back(std::move(diagnostic));
  }
  return result;
}

ModuleCompile ParameterBackedModule::compile_interactive(const ParameterReader& reader,
                                                         const std::string& parameter_id) const {
  if (parameter_id.empty()) return {};
  int instance = 0;
  std::string_view base;
  if (descriptor_.id == "color.local" && split_local_parameter(parameter_id, instance, base) && instance > 0) {
    return compile_modes(reader, parameter_modes(), std::string(base), instance);
  }
  return compile_modes(reader, parameter_modes(), parameter_id);
}

std::optional<DisplayScale> ParameterBackedModule::display_scale(const std::string& parameter_id) const {
  const ParameterMode* mode = find_parameter_mode(descriptor_.id, parameter_id);
  if (mode == nullptr || !mode->choices.empty() || mode->text) return std::nullopt;
  DisplayScale scale;
  scale.minimum = mode->minimum;
  scale.maximum = mode->maximum;
  scale.step = mode->step;
  scale.default_value = mode->default_value;
  scale.step_confirmed = mode->step_confirmed;
  scale.default_confirmed = mode->default_confirmed;
  scale.center_line = mode->minimum < 0.0 && mode->maximum > 0.0;
  return scale;
}

std::string ParameterBackedModule::disabled_reason(const ImageConditions& conditions) const {
  if (traits_.may_break_image && conditions.broken) return "坏片";
  if (traits_.needs_detection && conditions.detection_pending) return "检测未完成";
  if (traits_.needs_assets && conditions.assets_missing) return "缺少素材";
  return "";
}

std::string ParameterBackedModule::parameter_disabled_reason(const ParameterReader& reader,
                                                             const std::string& parameter_id) const {
  const auto reason = pixcake::parameter_disabled_reason(
      descriptor_.id, parameter_id, [&reader](std::string_view capability, std::string_view parameter) {
        return reader.value(std::string(capability), std::string(parameter));
      });
  if (reason) return *reason;
  const auto& solid = traits_.solid_background_parameters;
  if (reader.conditions().not_solid_color && std::find(solid.begin(), solid.end(), parameter_id) != solid.end()) {
    return "非纯色背景，无效果";
  }
  return "";
}

FragmentResult ParameterBackedModule::migrate(const FragmentBytes& fragment) const {
  FragmentResult result;
  result.fragment = fragment;
  if (fragment.version == current_version()) return result;
  // 局部调色版本 1 到 2：数据布局不变（2 只是多了带前缀的蒙版参数），直接升级版本号。
  if (descriptor_.id == "color.local" && fragment.version == 1) {
    result.fragment.version = current_version();
    return result;
  }
  // 其余能力只有版本 1，没有可升级的旧版本；更低的版本号不是本能力写出的片段。
  result.code = fragment.version > current_version() ? ResultCode::CapabilityTooNew : ResultCode::StateConflict;
  return result;
}

std::vector<GenerationEntry> ParameterBackedModule::generation_entries() const { return traits_.generation_entries; }

std::optional<GenerationJump> ParameterBackedModule::compile_jump(const std::string& tool_id,
                                                                   const JumpSource& source) const {
  const auto& entries = traits_.generation_entries;
  const bool declared = std::any_of(entries.begin(), entries.end(),
                                    [&tool_id](const GenerationEntry& entry) { return entry.tool_id == tool_id; });
  if (!declared || source.image_id.empty()) return std::nullopt;
  return GenerationJump{descriptor_.id, tool_id, source.image_id, source.revision, source.suggested_mask_ids};
}

}  // namespace pixcake
