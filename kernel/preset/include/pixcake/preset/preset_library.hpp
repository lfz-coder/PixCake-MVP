#pragma once

#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/result_code.hpp"
#include "pixcake/support/fast_export_latch.hpp"

#include <string>
#include <vector>

namespace pixcake {

inline constexpr char kPresetColor[] = "色彩";
inline constexpr char kPresetAiColor[] = "AI 色彩";
inline constexpr char kPresetPortrait[] = "人像";
inline constexpr char kPresetCombo[] = "组合";

struct PresetFragment {
  std::string capability_id;
  std::uint32_t version = 1;
  std::vector<std::uint8_t> inner;
};

struct Preset {
  std::string id;
  std::string name;
  std::string category;
  std::string type;
  std::vector<std::string> modules;
  std::vector<PresetFragment> fragments;
};

struct PresetCheck {
  ResultCode code = ResultCode::Ok;
  bool overwrite_color = false;
};

class PresetLibrary {
 public:
  ResultCode save(Preset preset, bool overwrite_existing);
  ResultCode import_preset(Preset preset, bool source_version_newer, bool source_deleted);
  PresetCheck check_apply(const EditDocument& document, const Preset& preset) const;
  ResultCode apply(EditDocument& document, const Preset& preset, std::uint64_t expected_revision,
                   FileSystem& files, const std::string& path, const FastExportLatch& latch);
  const std::vector<Preset>& presets() const;

 private:
  std::vector<Preset> presets_;
};

}  // namespace pixcake
