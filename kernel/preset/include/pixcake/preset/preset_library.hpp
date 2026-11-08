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
  // 管理（需求 §9）：重命名（名称同样受 20 字限制）、删除；找不到返回状态冲突。
  ResultCode rename(const std::string& id, const std::string& name);
  ResultCode remove(const std::string& id);
  const Preset* find(const std::string& id) const;
  // 启动时用存盘里的预设替换库里的全部（每一条重新校验，有一条不合格就整体拒绝，库不变）。
  ResultCode load(std::vector<Preset> presets);
  const std::vector<Preset>& presets() const;
  // 一个能力属于哪个预设模块（色彩、AI 色彩、人像、图像、衣物、裁剪…）；不能进预设的能力返回空串。
  static std::string module_of_capability(const std::string& capability_id);
  // 按已用的能力推断类型：都在色彩、AI 色彩或人像里就是那一类，否则是组合，modules 填涉及的模块；
  // 没有可存的能力返回空串。
  static std::string infer_type(const std::vector<std::string>& capability_ids, std::vector<std::string>& modules);

 private:
  std::vector<Preset> presets_;
};

}  // namespace pixcake
