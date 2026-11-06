#pragma once

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

// 架构第 7.2 节的能力接口。签名只用内核类型：能力不链接渲染、文档或 GPU（架构第 196 行规则 4）。

// 一个能力片段：版本加正文（编码见 fragment_codec.hpp）。
struct FragmentBytes {
  std::uint32_t version = 1;
  std::vector<std::uint8_t> inner;

  bool operator==(const FragmentBytes& other) const { return version == other.version && inner == other.inner; }
};

struct ParameterValue {
  std::string parameter_id;
  std::string display_value;

  bool operator==(const ParameterValue& other) const {
    return parameter_id == other.parameter_id && display_value == other.display_value;
  }
};

// 能力可用性取决于的图片条件（架构 7.2 “禁用原因”）。哪个能力看哪一项见架构 7.2 的禁用条件表。
struct ImageConditions {
  bool broken = false;             // 全身美型判断会坏片
  bool not_solid_color = false;    // 背景不是纯色
  bool assets_missing = false;     // 所需素材不在本机
  bool detection_pending = false;  // 人脸、人物检测还没完成
};

// 编译时读取参数：拖动中的预览值优先，其次是已提交值。
class ParameterReader {
 public:
  virtual ~ParameterReader() = default;
  virtual std::optional<std::string> value(const std::string& capability_id, const std::string& parameter_id) const = 0;
  // 没有片段时为 1。
  virtual std::uint32_t fragment_version(const std::string& capability_id) const = 0;
  // 读取方不知道的条件按“满足”处理（全为假）。
  virtual ImageConditions conditions() const { return {}; }
};

// 从精修跳到创意的入口（架构 7.5）。能力只声明和编译跳转，不实现生成，也不扣点。
struct GenerationEntry {
  std::string tool_id;
  std::string label;
};

struct JumpSource {
  std::string image_id;
  std::uint64_t revision = 0;
  std::vector<std::string> suggested_mask_ids;
};

// 生成式入口的编译结果：工具编号、当前图片、当前修订、建议选区（架构 7.2）。
struct GenerationJump {
  std::string capability_id;
  std::string tool_id;
  std::string image_id;
  std::uint64_t revision = 0;
  std::vector<std::string> suggested_mask_ids;
};

// 一个能力的声明。写在该能力自己的目录里（kernel/capabilities/<能力>/），由 ParameterBackedModule 执行。
struct ModuleTraits {
  bool needs_detection = false;   // 检测未完成时不可用
  bool needs_assets = false;      // 缺少素材时不可用
  bool may_break_image = false;   // 坏片时不可用
  std::vector<std::string> solid_background_parameters;  // 只对纯色背景生效的参数
  std::vector<GenerationEntry> generation_entries;
};

// 绘制描述是数据：通道编号、程序名、从哪段参数取值、使用哪块蒙版、混合方式、顺序。没有 OpenGL 对象。
struct DrawValue {
  std::string parameter_id;
  std::string channel;
  std::string display_value;
  double numeric_value = 0.0;
};

struct DrawStep {
  std::string capability_id;
  std::string stage;
  std::string program;
  std::uint32_t order = 0;
  std::vector<DrawValue> values;
  std::vector<std::string> mask_ids;
  std::string blend_mode = "normal";
  bool enabled = true;
  std::string disabled_reason;
};

struct ModuleDiagnostic {
  std::string capability_id;
  std::string message;
  bool blocking = false;
};

struct ModuleCompile {
  std::vector<DrawStep> steps;
  std::vector<ModuleDiagnostic> diagnostics;
};

struct SyncTraits {
  bool presettable = true;
  bool batch_syncable = true;
  std::string blocked_reason;
};

// 显示刻度。轨道两端颜色在需求和实测里都没有读数时为空，colors_confirmed 为假，界面标“待确认”。
struct DisplayScale {
  double minimum = 0.0;
  double maximum = 0.0;
  double step = 1.0;
  double default_value = 0.0;
  bool step_confirmed = true;
  bool default_confirmed = true;
  std::string left_color;
  std::string right_color;
  bool colors_confirmed = false;
  bool center_line = false;
};

enum class WriteMode { Replace, Overlay };

struct ExtractResult {
  ResultCode code = ResultCode::Ok;
  std::vector<ParameterValue> values;  // 本能力参数模式里登记过的值
  FragmentBytes fragment;              // 原样的片段，含未知记录，供预设和复制
};

struct FragmentResult {
  ResultCode code = ResultCode::Ok;
  FragmentBytes fragment;
};

class CapabilityModule {
 public:
  virtual ~CapabilityModule() = default;

  // 标识与模块标签
  virtual const std::string& id() const = 0;
  virtual const std::string& module_label() const = 0;
  // 参数模式
  virtual std::vector<const ParameterMode*> parameter_modes() const = 0;
  // 同步属性
  virtual SyncTraits sync_traits() const = 0;
  // 当前片段版本（迁移目标）。
  virtual std::uint32_t current_version() const = 0;
  // 提取片段。版本过新返回 CapabilityTooNew，fragment 仍原样带回。
  virtual ExtractResult extract(const FragmentBytes& fragment) const = 0;
  // 写回片段。只写本能力的参数；替换时去掉已登记的参数，叠加时只改给出的参数；未知记录原样保留。
  // 值不符合参数模式或不属于本能力返回 StateConflict；已有片段过新返回 CapabilityTooNew。
  virtual FragmentResult write_back(const std::optional<FragmentBytes>& existing,
                                    const std::vector<ParameterValue>& values, WriteMode mode) const = 0;
  // 编译。只读，不改任何东西。
  virtual ModuleCompile compile(const ParameterReader& reader) const = 0;
  // 拖动中的交互绘制只带被改的那一个值。
  virtual ModuleCompile compile_interactive(const ParameterReader& reader, const std::string& parameter_id) const = 0;
  // 显示刻度。枚举参数和未登记参数没有刻度。
  virtual std::optional<DisplayScale> display_scale(const std::string& parameter_id) const = 0;
  // 禁用原因，空表示可用。
  virtual std::string disabled_reason(const ImageConditions& conditions) const = 0;
  // 某个参数在当前取值下的禁用原因，空表示可编辑。
  virtual std::string parameter_disabled_reason(const ParameterReader& reader, const std::string& parameter_id) const = 0;
  // 迁移到当前版本。失败时 fragment 是原片段，不变。
  virtual FragmentResult migrate(const FragmentBytes& fragment) const = 0;
  // 本能力声明的生成入口；tool_id 不是本能力声明的入口时没有跳转。
  virtual std::vector<GenerationEntry> generation_entries() const = 0;
  virtual std::optional<GenerationJump> compile_jump(const std::string& tool_id, const JumpSource& source) const = 0;
};

// 按描述表、参数模式和能力声明实现全部操作。每个能力在自己的目录里给出声明；
// 行为与此不同的能力（例如 detail 按阶段编译）在自己的目录里派生。
class ParameterBackedModule : public CapabilityModule {
 public:
  ParameterBackedModule(CapabilityDescriptor descriptor, ModuleTraits traits);

  const std::string& id() const override;
  const std::string& module_label() const override;
  std::vector<const ParameterMode*> parameter_modes() const override;
  SyncTraits sync_traits() const override;
  std::uint32_t current_version() const override;
  ExtractResult extract(const FragmentBytes& fragment) const override;
  FragmentResult write_back(const std::optional<FragmentBytes>& existing, const std::vector<ParameterValue>& values,
                            WriteMode mode) const override;
  ModuleCompile compile(const ParameterReader& reader) const override;
  ModuleCompile compile_interactive(const ParameterReader& reader, const std::string& parameter_id) const override;
  std::optional<DisplayScale> display_scale(const std::string& parameter_id) const override;
  std::string disabled_reason(const ImageConditions& conditions) const override;
  std::string parameter_disabled_reason(const ParameterReader& reader, const std::string& parameter_id) const override;
  FragmentResult migrate(const FragmentBytes& fragment) const override;
  std::vector<GenerationEntry> generation_entries() const override;
  std::optional<GenerationJump> compile_jump(const std::string& tool_id, const JumpSource& source) const override;

 protected:
  // 某个参数这一帧是否参与绘制。默认：当前不可编辑的参数不画（黑白配置文件下的 HSL、
  // 颗粒效果为 0 时的其余颗粒参数、非纯色背景下的纯色参数），数据不删。
  virtual bool draws_parameter(const ParameterReader& reader, const ParameterMode& mode) const;
  // 按参数模式把读到的值编成一条绘制描述；only 非空时只取这一个参数。
  ModuleCompile compile_modes(const ParameterReader& reader, const std::vector<const ParameterMode*>& modes,
                              const std::string& only) const;

 private:
  CapabilityDescriptor descriptor_;
  ModuleTraits traits_;
};

}  // namespace pixcake
