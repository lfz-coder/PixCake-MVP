#include "pixcake/preset/preset_library.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {
namespace {

struct ModuleRow {
  const char* capability_id;
  const char* module;
};

const ModuleRow kModules[] = {
    {"color.match", "AI 色彩"},   {"color.style", "AI 色彩"},  {"color.global", "色彩"},
    {"color.local", "色彩"},      {"color.post", "色彩"},      {"detail", "色彩"},
    {"portrait.blemish", "人像"}, {"portrait.skin", "人像"},   {"portrait.reshape", "人像"},
    {"portrait.feature", "人像"}, {"portrait.body", "人像"},   {"image.background", "图像"},
    {"image.idphoto", "图像"},    {"image.sky", "图像"},       {"clothes", "衣物"},
    {"crop", "裁剪"},
};

const char* module_of(const std::string& capability_id) {
  for (const auto& row : kModules) {
    if (capability_id == row.capability_id) {
      return row.module;
    }
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
    points.push_back(value);
    index += need;
  }
  return points;
}

bool blank_points(const std::vector<char32_t>& points) {
  if (points.empty()) {
    return true;
  }
  for (char32_t point : points) {
    if (point != 0x09 && point != 0x0A && point != 0x0D && point != 0x20) {
      return false;
    }
  }
  return true;
}

const char* kComboModules[] = {"人像", "色彩", "AI 色彩", "图像", "衣物", "裁剪"};

ResultCode validate(const Preset& preset) {
  if (preset.id.empty()) {
    return ResultCode::StateConflict;
  }
  const auto points = code_points(preset.name);
  if (!points || blank_points(*points) || points->size() > 20) {
    return ResultCode::StateConflict;
  }
  for (const auto& fragment : preset.fragments) {
    if (module_of(fragment.capability_id) == nullptr) {
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
                                std::uint64_t expected_revision, FileSystem& files, const std::string& path) {
  if (const ResultCode code = validate(preset); code != ResultCode::Ok) {
    return code;
  }
  const auto modules = replaced_modules(preset);
  std::vector<std::string> remove_ids;
  for (const auto& row : kModules) {
    bool selected = false;
    for (const auto& module : modules) {
      if (module == row.module) {
        selected = true;
      }
    }
    if (selected && document.fragment(row.capability_id).has_value()) {
      remove_ids.push_back(row.capability_id);
    }
  }
  std::vector<FragmentView> replacements;
  for (const auto& fragment : preset.fragments) {
    FragmentView view;
    view.capability_id = fragment.capability_id;
    view.version = fragment.version;
    view.inner = fragment.inner;
    replacements.push_back(std::move(view));
  }
  return document.commit_fragment_replacement(expected_revision, remove_ids, replacements, files, path);
}

const std::vector<Preset>& PresetLibrary::presets() const { return presets_; }

}  // namespace pixcake
