#include "pixcake/command/photo_status.hpp"

#include <string>
#include <utility>

namespace pixcake {

PhotoEditRecorder::PhotoEditRecorder(ProjectLibrary& library, std::int64_t photo_id, const Clock& clock, LogSink* log)
    : library_(library), photo_id_(photo_id), clock_(clock), log_(log) {}

void PhotoEditRecorder::committed(const std::string& image_id, std::uint64_t revision) {
  const ProjectResult marked = library_.mark_photo_edited(photo_id_, clock_.unix_seconds());
  if (marked.code == ResultCode::Ok || log_ == nullptr) return;
  LogRecord record;
  record.module = "command";
  record.event = "project.status.failed";
  record.code = marked.code;
  record.revision = revision;
  // document_id 约定为精修文档的正式路径，这里不知道路径，原图和图片编号写进说明。
  record.message = "提交已保存，修图状态没有记上（原图 " + image_id + "，图片 " + std::to_string(photo_id_) + "）";
  log_->write(record);
}

ResultCode record_export_statuses(const std::vector<ExportProductFact>& products, ProjectLibrary& library,
                                  const PhotoResolver& photo_of) {
  ResultCode first_failure = ResultCode::Ok;
  for (const auto& product : products) {
    if (!product.product_exists) continue;
    const auto photo = photo_of ? photo_of(product.original_id) : std::nullopt;
    if (!photo) continue;
    const ProjectResult marked = library.mark_photo_exported(*photo);
    if (marked.code != ResultCode::Ok && first_failure == ResultCode::Ok) first_failure = marked.code;
  }
  return first_failure;
}

}  // namespace pixcake
