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

namespace {
ProjectResult refused_without_session() {
  ProjectResult refused;
  refused.code = ResultCode::StateConflict;
  return refused;
}
}  // namespace

ProjectResult BusinessCommands::set_photo_ratings(ProjectLibrary& library, std::vector<std::int64_t> photo_ids,
                                                  int rating) {
  return session_.signed_in() ? library.set_photo_ratings(std::move(photo_ids), rating) : refused_without_session();
}

ProjectResult BusinessCommands::set_photo_flags(ProjectLibrary& library, std::vector<std::int64_t> photo_ids,
                                                std::string flag) {
  return session_.signed_in() ? library.set_photo_flags(std::move(photo_ids), std::move(flag))
                              : refused_without_session();
}

ProjectResult BusinessCommands::set_photo_colors(ProjectLibrary& library, std::vector<std::int64_t> photo_ids,
                                                 std::string color) {
  return session_.signed_in() ? library.set_photo_colors(std::move(photo_ids), std::move(color))
                              : refused_without_session();
}

ProjectResult BusinessCommands::trash_project(ProjectLibrary& library, std::int64_t project_id) {
  return session_.signed_in() ? library.trash_project(project_id) : refused_without_session();
}

ProjectResult BusinessCommands::trash_photo(ProjectLibrary& library, std::int64_t photo_id) {
  return session_.signed_in() ? library.trash_photo(photo_id) : refused_without_session();
}

ProjectResult BusinessCommands::trash_virtual_copy(ProjectLibrary& library, std::int64_t copy_id) {
  return session_.signed_in() ? library.trash_virtual_copy(copy_id) : refused_without_session();
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
