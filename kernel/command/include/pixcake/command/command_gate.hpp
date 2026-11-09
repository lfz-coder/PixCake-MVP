#pragma once

#include "pixcake/capability/capability_module.hpp"
#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/history/history_log.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/persist/write_lock.hpp"
#include "pixcake/preset/preset_library.hpp"
#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/log_sink.hpp"
#include "pixcake/support/result_code.hpp"
#include "pixcake/support/session_state.hpp"
#include "pixcake/sync/sync_batch.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

struct UserCommand {
  std::optional<std::string> name;
  std::optional<std::string> target_id;
  std::optional<std::uint64_t> revision;
  std::string capability_id;
  std::string parameter_id;
  std::string display_value;
};

// 追色精准控制的写回指令：写进哪个色彩能力、写哪些值（契约 C-33，架构“写回的存放与切换”）。
struct MatchWrite {
  std::string capability_id;
  std::vector<ParameterValue> values;
};

struct CommandResult {
  ResultCode code = ResultCode::Ok;
  std::uint64_t revision = 0;
};

// 提交落盘成功后的通知。命令入口把“已编辑”这类派生状态交给所属模块（架构 6.2：交给所属模块，
// 自己不写 SQL）。通知在提交成功之后发出，不能改变这次提交的结果。
class CommitListener {
 public:
  virtual ~CommitListener() = default;
  virtual void committed(const std::string& image_id, std::uint64_t revision) = 0;
};

class CommandGate {
 public:
  // pipeline 是开始编辑前要检查的能力管线，默认是阶段一管线。管线有环或重复时，所有命令都返回
  // StateConflict，并写一条 event 为 "command.pipeline.invalid" 的日志（契约 C-35、运维 O-04）。
  CommandGate(EditDocument& document, const FastExportLatch& latch, FileSystem& files, std::string path,
              std::vector<VersionLimit> limits, LogSink* log,
              const std::vector<PipelineStep>& pipeline = phase_one_pipeline());
  ~CommandGate();

  CommandResult submit(const UserCommand& command);
  // 整段替换类命令（架构 6.2：用户发起的修改都从命令入口进入）。和 submit 一样检查管线、极速导出、
  // 写锁与修订号；成功产生新修订时通知 CommitListener。实际修改交给所属模块。
  CommandResult apply_preset(PresetLibrary& library, const Preset& preset, std::uint64_t revision);
  CommandResult paste(const EffectClipboard& clipboard, std::uint64_t revision);
  // 局部调色删除第 instance 个蒙版（去掉它带 "m<N>:" 前缀的全部参数；实例 0 是没有前缀的那一组）。
  CommandResult remove_local_mask(int instance, std::uint64_t revision);
  // 回到历史里的某一步（撤销、重做、点历史）：文档换成那一步的内容并写盘，修订号不倒退。
  // 写盘失败时文档还原成原来的内容。
  CommandResult select_history(HistoryLog& history, std::size_t index, std::uint64_t revision);
  // 批量同步：每张目标图各有一个命令入口，逐个调用。
  CommandResult sync_from(const EditDocument& source, const std::string& source_path,
                          const std::vector<std::string>& checked_ids, std::uint64_t revision);
  // 创意“应用至精修”：把图层结果烘焙回精修文档。
  CommandResult apply_layers(const LayerDocument& layers, std::uint64_t revision);
  // 追色精准控制写回：只写 match_writeback_switch().targets 里的能力，经该能力模块按参数模式校验。
  CommandResult write_match_result(const std::vector<MatchWrite>& writes, std::uint64_t revision);
  // 可以为空。不由命令入口持有。
  void set_commit_listener(CommitListener* listener) { listener_ = listener; }
  // 登录态（安全清单 S-25）。主进程总是设置；设置后未登录的命令一律返回状态冲突，不碰文档。
  void set_session(const SessionState* session) { session_ = session; }

 private:
  CommandResult run_document_command(const char* name, std::uint64_t revision,
                                     const std::function<ResultCode()>& change);

  EditDocument& document_;
  const FastExportLatch& latch_;
  FileSystem& files_;
  std::string path_;
  std::vector<VersionLimit> limits_;
  LogSink* log_ = nullptr;
  std::string pipeline_error_;
  CommitListener* listener_ = nullptr;
  const SessionState* session_ = nullptr;
};

}  // namespace pixcake
