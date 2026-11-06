#pragma once

#include "pixcake/command/command_gate.hpp"
#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/project/project_library.hpp"
#include "pixcake/support/clock.hpp"
#include "pixcake/support/log_sink.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

// 图库的修图状态与导出状态是派生状态（需求 §6.2、§6.4）。主人：修图状态归命令入口的提交，
// 导出状态归导出事件（性能清单第 55 行）；项目库里的两列是它们的投影。

// 一份精修文档对应一张图片：打开文档时由调用方绑定图片编号，提交成功后把图片记为“已编辑”。
// 记录失败不影响已落盘的提交，写一条 module=command、event=project.status.failed 的日志。
class PhotoEditRecorder final : public CommitListener {
 public:
  PhotoEditRecorder(ProjectLibrary& library, std::int64_t photo_id, const Clock& clock, LogSink* log);
  void committed(const std::string& image_id, std::uint64_t revision) override;

 private:
  ProjectLibrary& library_;
  std::int64_t photo_id_;
  const Clock& clock_;
  LogSink* log_;
};

// 计费原图编号（导出任务的 original_id）到图片编号。项目里没有这张原图时返回空。
using PhotoResolver = std::function<std::optional<std::int64_t>(const std::string& original_id)>;

// 把导出队列里成品存在的任务投影到项目库，记为“已导出”。找不到图片的任务跳过。
// 全部处理完，返回第一个写入失败的结果码。
ResultCode record_export_statuses(const std::vector<ExportProductFact>& products, ProjectLibrary& library,
                                  const PhotoResolver& photo_of);

}  // namespace pixcake
