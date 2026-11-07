#include "pixcake/command/business_commands.hpp"

#include <utility>

namespace pixcake {

BusinessCommands::BusinessCommands(const SessionState& session) : session_(session) {}

ProjectResult BusinessCommands::create_project(ProjectLibrary& library, std::string name, std::string origin,
                                               bool cloud_marked) {
  if (!session_.signed_in()) {
    ProjectResult refused;
    refused.code = ResultCode::StateConflict;
    return refused;
  }
  return library.create_project(std::move(name), std::move(origin), cloud_marked);
}

ProjectResult BusinessCommands::create_virtual_copy(ProjectLibrary& library, std::int64_t photo_id,
                                                    std::string document_id) {
  if (!session_.signed_in()) {
    ProjectResult refused;
    refused.code = ResultCode::StateConflict;
    return refused;
  }
  return library.create_virtual_copy(photo_id, std::move(document_id));
}

image_import::ImportBatchResult BusinessCommands::import_into(
    image_import::ImportCoordinator& importer, std::int64_t project_id,
    const std::vector<image_import::ImportCandidate>& candidates) {
  if (!session_.signed_in()) {
    image_import::ImportBatchResult refused;
    refused.overall_code = ResultCode::StateConflict;
    return refused;
  }
  return importer.import_into(project_id, candidates);
}

ResultCode BusinessCommands::enqueue_export(ExportQueue& queue, const ExportJob& job) {
  return session_.signed_in() ? queue.enqueue(job) : ResultCode::StateConflict;
}

ResultCode BusinessCommands::quote_generation(LedgerBook& ledger, const TaskQuote& quote) {
  return session_.signed_in() ? ledger.quote(quote) : ResultCode::StateConflict;
}

ResultCode BusinessCommands::enqueue_generation(LedgerBook& ledger, const std::string& task_id) {
  return session_.signed_in() ? ledger.enqueue(task_id) : ResultCode::StateConflict;
}

ResultCode BusinessCommands::cancel_generation(LedgerBook& ledger, const std::string& task_id) {
  return session_.signed_in() ? ledger.cancel(task_id) : ResultCode::StateConflict;
}

ResultCode BusinessCommands::settle_exports(LedgerBook& ledger, const std::vector<ExportProduct>& products) {
  return session_.signed_in() ? ledger.settle_exports(products) : ResultCode::StateConflict;
}

}  // namespace pixcake
