#include "pixcake/preset/preset_library.hpp"

#include "pixcake/capabilities/module_set.hpp"
#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/persist/write_lock.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {
namespace {

const char* module_of(const std::string& capability_id) {
  static const auto registry = CapabilityRegistry::phase_one();
  if (capability_id == "detail") return "色彩";
  if (const auto* descriptor = registry.find(capability_id)) {
    return descriptor->module_label.c_str();
  }
  return nullptr;
}

std::optional<std::vector<char32_t>> code_points(const std::string& text) {
  std::vector<char32_t> points;
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t need = 1;
    char32_t value = lead;
    if (lead < 0x80) {
      need = 1;
    } else if ((lead & 0xE0) == 0xC0) {
      need = 2;
      value = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      need = 3;
      value = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      need = 4;
      value = lead & 0x07;
    } else {
      return std::nullopt;
    }
    if (index + need > text.size()) {
      return std::nullopt;
    }
    for (std::size_t extra = 1; extra < need; ++extra) {
      const auto next = static_cast<unsigned char>(text[index + extra]);
      if ((next & 0xC0) != 0x80) {
        return std::nullopt;
      }
      value = (value << 6) | (next & 0x3F);
    }
    if ((need == 2 && value < 0x80) || (need == 3 && value < 0x800) ||
        (need == 4 && value < 0x10000) || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
      return std::nullopt;
    }
    points.push_back(value);
    index += need;
  }
  return points;
}

bool combining_mark(char32_t point) {
  return (point >= 0x0300 && point <= 0x036F) || (point >= 0x1AB0 && point <= 0x1AFF) ||
         (point >= 0x1DC0 && point <= 0x1DFF) || (point >= 0x20D0 && point <= 0x20FF) ||
         (point >= 0xFE20 && point <= 0xFE2F);
}

std::size_t visible_count(const std::vector<char32_t>& points) {
  std::size_t count = 0;
  for (const auto point : points) {
    if (!combining_mark(point)) ++count;
  }
  return count;
}

bool blank_points(const std::vector<char32_t>& points) {
  if (points.empty()) {
    return true;
  }
  for (char32_t point : points) {
    const bool unicode_space =
        point == 0x09 || point == 0x0A || point == 0x0B || point == 0x0C || point == 0x0D ||
        point == 0x20 || point == 0x85 || point == 0xA0 || point == 0x1680 ||
        (point >= 0x2000 && point <= 0x200A) || point == 0x2028 || point == 0x2029 ||
        point == 0x202F || point == 0x205F || point == 0x3000 || point == 0xFEFF;
    if (!unicode_space) {
      return false;
    }
  }
  return true;
}

const char* kComboModules[] = {"人像", "色彩", "AI 色彩", "图像", "衣物", "裁剪"};

ResultCode validate(const Preset& preset) {
  static const auto registry = CapabilityRegistry::phase_one();
  if (preset.id.empty()) {
    return ResultCode::StateConflict;
  }
  const auto points = code_points(preset.name);
  if (!points || blank_points(*points) || visible_count(*points) == 0 || visible_count(*points) > 20) {
    return ResultCode::StateConflict;
  }
  for (const auto& fragment : preset.fragments) {
    const auto* module = module_of(fragment.capability_id);
    const auto* descriptor = registry.find(fragment.capability_id);
    if (module == nullptr || (descriptor != nullptr && !descriptor->can_use_in_preset)) {
      return ResultCode::StateConflict;
    }
  }
  if (preset.type == kPresetColor || preset.type == kPresetAiColor || preset.type == kPresetPortrait) {
    if (!preset.modules.empty()) {
      return ResultCode::StateConflict;
    }
    for (const auto& fragment : preset.fragments) {
      if (std::string(module_of(fragment.capability_id)) != preset.type) {
        return ResultCode::StateConflict;
      }
    }
    return ResultCode::Ok;
  }
  if (preset.type != kPresetCombo || preset.modules.size() < 2) {
    return ResultCode::StateConflict;
  }
  for (std::size_t i = 0; i < preset.modules.size(); ++i) {
    bool allowed = false;
    for (const char* name : kComboModules) {
      if (preset.modules[i] == name) {
        allowed = true;
      }
    }
    if (!allowed) {
      return ResultCode::StateConflict;
    }
    for (std::size_t j = i + 1; j < preset.modules.size(); ++j) {
      if (preset.modules[i] == preset.modules[j]) {
        return ResultCode::StateConflict;
      }
    }
  }
  for (const auto& fragment : preset.fragments) {
    bool listed = false;
    for (const auto& module : preset.modules) {
      if (module == module_of(fragment.capability_id)) {
        listed = true;
      }
    }
    if (!listed) {
      return ResultCode::StateConflict;
    }
  }
  return ResultCode::Ok;
}

std::vector<std::string> replaced_modules(const Preset& preset) {
  if (preset.type == kPresetCombo) {
    return preset.modules;
  }
  return {preset.type};
}

bool replaces_color(const Preset& preset) {
  for (const auto& module : replaced_modules(preset)) {
    if (module == kPresetColor) {
      return true;
    }
  }
  return false;
}

bool document_has_color(const EditDocument& document) {
  const char* color_ids[] = {"color.global", "color.local", "color.post", "detail"};
  for (const char* id : color_ids) {
    if (document.fragment(id).has_value()) {
      return true;
    }
  }
  return false;
}

}  // namespace

ResultCode PresetLibrary::save(Preset preset, bool overwrite_existing) {
  if (const ResultCode code = validate(preset); code != ResultCode::Ok) {
    return code;
  }
  for (auto& existing : presets_) {
    if (existing.id == preset.id) {
      if (!overwrite_existing) {
        return ResultCode::StateConflict;
      }
      existing = std::move(preset);
      return ResultCode::Ok;
    }
  }
  if (overwrite_existing) {
    return ResultCode::StateConflict;
  }
  presets_.push_back(std::move(preset));
  return ResultCode::Ok;
}

ResultCode PresetLibrary::import_preset(Preset preset, bool source_version_newer, bool source_deleted) {
  if (source_version_newer || source_deleted) {
    return ResultCode::StateConflict;
  }
  return save(std::move(preset), false);
}

PresetCheck PresetLibrary::check_apply(const EditDocument& document, const Preset& preset) const {
  PresetCheck result;
  result.code = validate(preset);
  if (result.code != ResultCode::Ok) {
    return result;
  }
  result.overwrite_color = replaces_color(preset) && document_has_color(document);
  return result;
}

ResultCode PresetLibrary::apply(EditDocument& document, const Preset& preset,
                                std::uint64_t expected_revision, FileSystem& files, const std::string& path,
                                const FastExportLatch& latch) {
  WriteScope scope(files, path, &latch);
  if (!scope) return scope.code();
  if (const ResultCode code = validate(preset); code != ResultCode::Ok) {
    return code;
  }
  const auto modules = replaced_modules(preset);
  std::vector<std::string> remove_ids;
  static const auto registry = CapabilityRegistry::phase_one();
  for (const auto& descriptor : registry.descriptors()) {
    const auto& capability_id = descriptor.id;
    bool selected = false;
    for (const auto& module : modules) {
      if (module == descriptor.module_label) {
        selected = true;
      }
    }
    if (selected && document.fragment(capability_id).has_value()) {
      remove_ids.push_back(capability_id);
    }
  }
  // 每个能力经自己的模块写入（架构 7.2）：来源迁移、提取、取值校验；目标片段比本能力新时保留。
  std::vector<std::string> touched = remove_ids;
  for (const auto& fragment : preset.fragments) {
    if (std::find(touched.begin(), touched.end(), fragment.capability_id) == touched.end()) {
      touched.push_back(fragment.capability_id);
    }
  }
  std::vector<std::string> removals;
  std::vector<FragmentView> replacements;
  for (const auto& capability_id : touched) {
    std::optional<FragmentBytes> source;
    for (const auto& fragment : preset.fragments) {
      if (fragment.capability_id == capability_id) source = FragmentBytes{fragment.version, fragment.inner};
    }
    std::optional<FragmentBytes> target;
    if (const auto existing = document.fragment(capability_id)) target = FragmentBytes{existing->version, existing->inner};
    const FragmentTransfer moved = transfer_fragment(capability_id, source, target);
    if (moved.code != ResultCode::Ok) return moved.code;
    if (moved.keep_target) continue;
    if (target) removals.push_back(capability_id);
    if (moved.fragment) replacements.push_back(FragmentView{capability_id, moved.fragment->version, moved.fragment->inner});
  }
  return document.commit_fragment_replacement(expected_revision, removals, replacements, files, path);
}

const std::vector<Preset>& PresetLibrary::presets() const { return presets_; }

}  // namespace pixcake
