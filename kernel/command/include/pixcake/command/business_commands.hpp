#pragma once

#include "pixcake/exportq/export_queue.hpp"
#include "pixcake/image_import/import_coordinator.hpp"
#include "pixcake/ledger/ledger_book.hpp"
#include "pixcake/project/project_library.hpp"
#include "pixcake/support/session_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake {

// 文档以外的用户命令（架构 6.2：项目、创建任务、创建导出也从命令入口进入）。每个命令先看会话：
// 未登录返回状态冲突，不碰项目库、导出队列和账本（安全清单 S-25）。登录后交给所属模块。
class BusinessCommands {
 public:
  explicit BusinessCommands(const SessionState& session);

  ProjectResult create_project(ProjectLibrary& library, std::string name, std::string origin, bool cloud_marked);
  ProjectResult create_virtual_copy(ProjectLibrary& library, std::int64_t photo_id, std::string document_id);
  image_import::ImportBatchResult import_into(image_import::ImportCoordinator& importer, std::int64_t project_id,
                                              const std::vector<image_import::ImportCandidate>& candidates);
  ResultCode enqueue_export(ExportQueue& queue, const ExportJob& job);
  ResultCode quote_generation(LedgerBook& ledger, const TaskQuote& quote);
  ResultCode enqueue_generation(LedgerBook& ledger, const std::string& task_id);
  ResultCode cancel_generation(LedgerBook& ledger, const std::string& task_id);
  ResultCode settle_exports(LedgerBook& ledger, const std::vector<ExportProduct>& products);

 private:
  const SessionState& session_;
};

}  // namespace pixcake
