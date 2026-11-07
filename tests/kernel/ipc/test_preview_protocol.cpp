#include "pixcake/capability/fragment_codec.hpp"
#include "pixcake/ipc/preview_protocol.hpp"
#include "pixcake/ipc/process_messages.hpp"

#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

pixcake::ipc::PreviewRequestMessage make_request() {
  pixcake::ipc::PreviewRequestMessage message;
  message.client_sequence = 9;
  message.request.image_id = "img-1";
  message.request.editing_instance_id = "edit-1";
  message.request.base_revision = 4;
  message.request.capability_id = "color.global";
  message.request.parameter_id = "temperature";
  message.request.display_value = "20";
  message.request.mode = pixcake::preview::PreviewMode::ColorOnly;
  message.request.max_edge = 4000;
  return message;
}

void test_request_round_trip_and_bounds() {
  const auto input = make_request();
  std::vector<std::uint8_t> bytes;
  EXPECT(pixcake::ipc::encode_preview_request(input, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::PreviewRequestMessage output;
  EXPECT(pixcake::ipc::decode_preview_request(bytes, output) == pixcake::ResultCode::Ok);
  EXPECT(output.client_sequence == 9);
  EXPECT(output.request.display_value == "20");
  EXPECT(output.request.mode == pixcake::preview::PreviewMode::ColorOnly);
  bytes.push_back(0);
  EXPECT(pixcake::ipc::decode_preview_request(bytes, output) == pixcake::ResultCode::StateConflict);
  auto oversized = input;
  oversized.request.display_value.assign(257, 'x');
  EXPECT(pixcake::ipc::encode_preview_request(oversized, bytes) == pixcake::ResultCode::StateConflict);
  auto unknown = input;
  unknown.protocol_version = 2;
  EXPECT(pixcake::ipc::encode_preview_request(unknown, bytes) == pixcake::ResultCode::StateConflict);
}

void test_b48_cancel_and_frame_header_reject_bad_values() {
  pixcake::ipc::PreviewCancelMessage cancel;
  cancel.client_sequence = 3;
  cancel.image_id = "img-1";
  cancel.editing_instance_id = "edit-1";
  std::vector<std::uint8_t> bytes;
  EXPECT(pixcake::ipc::encode_preview_cancel(cancel, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::PreviewCancelMessage decoded_cancel;
  EXPECT(pixcake::ipc::decode_preview_cancel(bytes, decoded_cancel) == pixcake::ResultCode::Ok);

  pixcake::ipc::PreviewFrameHeader header;
  header.sequence = 8;
  header.document_revision = 4;
  header.width = 4000;
  header.height = 2000;
  header.stride = 16000;
  EXPECT(pixcake::ipc::encode_preview_frame_header(header, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::PreviewFrameHeader decoded_header;
  EXPECT(pixcake::ipc::decode_preview_frame_header(bytes, decoded_header) == pixcake::ResultCode::Ok);
  EXPECT(decoded_header.stride == 16000);
  auto invalid = header;
  invalid.stride = 1;
  EXPECT(pixcake::ipc::encode_preview_frame_header(invalid, bytes) == pixcake::ResultCode::StateConflict);
  bytes.resize(3);
  EXPECT(pixcake::ipc::decode_preview_frame_header(bytes, decoded_header) == pixcake::ResultCode::StateConflict);
}

}  // namespace

// C-30、Q-25：进程间消息只在 kernel/ipc 定义一次，消息体用和磁盘文档相同的记录编码（架构第 354 行）。
void test_c30_preview_messages_use_the_document_record_codec() {
  std::vector<std::uint8_t> bytes;
  EXPECT(pixcake::ipc::encode_preview_request(make_request(), bytes) == pixcake::ResultCode::Ok);
  std::vector<pixcake::FragmentRecord> records;
  EXPECT(pixcake::decode_fragment_records(bytes, records));
  EXPECT(!records.empty() && records.front().type == pixcake::ipc::kEnvelopeRecord);
  pixcake::ipc::PreviewCancelMessage cancel;
  cancel.client_sequence = 3;
  cancel.image_id = "img-1";
  cancel.editing_instance_id = "edit-1";
  cancel.base_revision = 4;
  EXPECT(pixcake::ipc::encode_preview_cancel(cancel, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::PreviewCancelMessage decoded;
  EXPECT(pixcake::ipc::decode_preview_cancel(bytes, decoded) == pixcake::ResultCode::Ok);
  EXPECT(decoded.base_revision == 4 && decoded.image_id == "img-1");
}

void test_c30_process_messages_round_trip() {
  std::vector<std::uint8_t> bytes;
  pixcake::ipc::ExportProgressMessage exported;
  exported.export_id = "exp-1";
  exported.status = "导出中";
  exported.completed = 3;
  exported.total = 10;
  exported.code = pixcake::ResultCode::DiskFull;
  EXPECT(pixcake::ipc::encode_export_progress(exported, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::ExportProgressMessage export_out;
  EXPECT(pixcake::ipc::decode_export_progress(bytes, export_out) == pixcake::ResultCode::Ok);
  EXPECT(export_out.export_id == "exp-1" && export_out.status == "导出中" && export_out.completed == 3 &&
         export_out.total == 10 && export_out.code == pixcake::ResultCode::DiskFull && export_out.reason.empty());
  EXPECT(pixcake::ipc::is_export_progress(bytes) && !pixcake::ipc::is_export_start(bytes));
  exported.reason = "磁盘已满";
  EXPECT(pixcake::ipc::encode_export_progress(exported, bytes) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::ipc::decode_export_progress(bytes, export_out) == pixcake::ResultCode::Ok);
  EXPECT(export_out.reason == "磁盘已满");

  // Shell -> exportd: start a queued task with the resolved original path.
  pixcake::ipc::ExportStartMessage start;
  start.export_id = "exp-2";
  start.source_path = "/照片/a.jpg";
  EXPECT(pixcake::ipc::encode_export_start(start, bytes) == pixcake::ResultCode::Ok);
  EXPECT(pixcake::ipc::is_export_start(bytes) && !pixcake::ipc::is_task_cancel(bytes));
  pixcake::ipc::ExportStartMessage start_out;
  EXPECT(pixcake::ipc::decode_export_start(bytes, start_out) == pixcake::ResultCode::Ok);
  EXPECT(start_out.export_id == "exp-2" && start_out.source_path == "/照片/a.jpg");
  EXPECT(pixcake::ipc::decode_export_progress(bytes, export_out) != pixcake::ResultCode::Ok);
  start.source_path.clear();
  EXPECT(pixcake::ipc::encode_export_start(start, bytes) == pixcake::ResultCode::StateConflict);

  pixcake::ipc::GenerationProgressMessage generated;
  generated.task_id = "task-1";
  generated.state = pixcake::generate::TaskState::Succeeded;
  generated.percent = 100;
  generated.result_id = "res-1";
  generated.pixel_file = "/creative/task-1.result";
  generated.checksum = "abc";
  EXPECT(pixcake::ipc::encode_generation_progress(generated, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::GenerationProgressMessage generation_out;
  EXPECT(pixcake::ipc::decode_generation_progress(bytes, generation_out) == pixcake::ResultCode::Ok);
  EXPECT(generation_out.state == pixcake::generate::TaskState::Succeeded && generation_out.result_id == "res-1" &&
         generation_out.pixel_file == "/creative/task-1.result" && generation_out.checksum == "abc");

  pixcake::ipc::TaskCancelMessage cancel;
  cancel.kind = pixcake::ipc::TaskKind::Generation;
  cancel.task_id = "task-1";
  EXPECT(pixcake::ipc::encode_task_cancel(cancel, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::TaskCancelMessage cancel_out;
  EXPECT(pixcake::ipc::decode_task_cancel(bytes, cancel_out) == pixcake::ResultCode::Ok);
  EXPECT(cancel_out.kind == pixcake::ipc::TaskKind::Generation && cancel_out.task_id == "task-1");

  pixcake::ipc::HeartbeatMessage beat;
  beat.process = "generated";
  beat.sequence = 12;
  beat.sent_at_ms = 99;
  EXPECT(pixcake::ipc::encode_heartbeat(beat, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::HeartbeatMessage beat_out;
  EXPECT(pixcake::ipc::decode_heartbeat(bytes, beat_out) == pixcake::ResultCode::Ok);
  EXPECT(beat_out.process == "generated" && beat_out.sequence == 12 && beat_out.sent_at_ms == 99);
  // 一种消息不能当另一种解码。
  EXPECT(pixcake::ipc::decode_task_cancel(bytes, cancel_out) == pixcake::ResultCode::StateConflict);
}

void test_c30_truncated_newer_or_extended_messages() {
  pixcake::ipc::HeartbeatMessage beat;
  beat.process = "renderd";
  beat.sequence = 1;
  std::vector<std::uint8_t> bytes;
  EXPECT(pixcake::ipc::encode_heartbeat(beat, bytes) == pixcake::ResultCode::Ok);
  pixcake::ipc::HeartbeatMessage out;
  auto truncated = bytes;
  truncated.pop_back();
  EXPECT(pixcake::ipc::decode_heartbeat(truncated, out) == pixcake::ResultCode::StateConflict);
  // 追加一个未知字段记录：旧版本读得懂已知字段，忽略未知字段。
  std::vector<pixcake::FragmentRecord> records;
  EXPECT(pixcake::decode_fragment_records(bytes, records));
  records.push_back({999, {1, 2, 3}});
  EXPECT(pixcake::ipc::decode_heartbeat(pixcake::encode_fragment_records(records), out) == pixcake::ResultCode::Ok);
  EXPECT(out.process == "renderd");
  // 信封里的协议版本比本进程新：拒绝，结果码是“需要更新”。
  for (auto& record : records) {
    if (record.type == pixcake::ipc::kEnvelopeRecord && record.payload.size() >= 8) record.payload[4] = 9;
  }
  EXPECT(pixcake::ipc::decode_heartbeat(pixcake::encode_fragment_records(records), out) ==
         pixcake::ResultCode::CapabilityTooNew);
}

// 证据 2026-11-06-renderd-transport-and-decoder.md §3.2：预览源（8）与预览结果（9）。
void test_source_and_result_messages() {
  using namespace pixcake;
  using namespace pixcake::ipc;
  PreviewSourceMessage source;
  source.image_id = "image-1";
  source.editing_instance_id = "edit-1";
  source.document_revision = 7;
  source.source_path = "/Users/a/Pictures/a.jpg";
  source.document = {0, 1, 2, 0, 255, 0};  // binary, NUL bytes included
  std::vector<std::uint8_t> bytes;
  EXPECT(encode_preview_source(source, bytes) == ResultCode::Ok);
  PreviewMessageKind kind{};
  EXPECT(peek_preview_message_kind(bytes, kind) == ResultCode::Ok && kind == PreviewMessageKind::Source);
  PreviewSourceMessage decoded;
  EXPECT(decode_preview_source(bytes, decoded) == ResultCode::Ok);
  EXPECT(decoded.image_id == "image-1" && decoded.editing_instance_id == "edit-1");
  EXPECT(decoded.document_revision == 7 && decoded.source_path == source.source_path);
  EXPECT(decoded.document == source.document);
  // A source message is not a request, and vice versa.
  PreviewRequestMessage request;
  EXPECT(decode_preview_request(bytes, request) == ResultCode::StateConflict);

  auto bad = source;
  bad.document.clear();
  EXPECT(encode_preview_source(bad, bytes) == ResultCode::StateConflict && bytes.empty());
  bad = source;
  bad.source_path.clear();
  EXPECT(encode_preview_source(bad, bytes) == ResultCode::StateConflict);
  bad = source;
  bad.document.assign(kMaxPreviewSourceBytes + 1, 1);
  EXPECT(encode_preview_source(bad, bytes) == ResultCode::StateConflict);
  bad = source;
  bad.document.assign(1024 * 1024, 7);  // a large document fits
  EXPECT(encode_preview_source(bad, bytes) == ResultCode::Ok);
  EXPECT(decode_preview_source(bytes, decoded) == ResultCode::Ok && decoded.document.size() == 1024 * 1024);

  PreviewResultMessage result;
  result.client_sequence = 5;
  result.code = ResultCode::Ok;
  result.header.sequence = 5;
  result.header.document_revision = 7;
  result.header.width = 3;
  result.header.height = 2;
  result.header.stride = 12;
  result.diagnostics = {{"color.local", "牙齿不可用（person.teeth）：模型没有该部位"}, {"", "frame note"}};
  EXPECT(encode_preview_result(result, bytes) == ResultCode::Ok);
  EXPECT(peek_preview_message_kind(bytes, kind) == ResultCode::Ok && kind == PreviewMessageKind::Result);
  PreviewResultMessage back;
  EXPECT(decode_preview_result(bytes, back) == ResultCode::Ok);
  EXPECT(back.client_sequence == 5 && back.code == ResultCode::Ok && back.header.width == 3 &&
         back.header.stride == 12 && back.header.document_revision == 7);
  EXPECT(back.diagnostics.size() == 2 && back.diagnostics[0].capability_id == "color.local" &&
         back.diagnostics[0].reason == result.diagnostics[0].reason && back.diagnostics[1].capability_id.empty());

  // Failure: no header, a reason instead.
  PreviewResultMessage failed;
  failed.client_sequence = 6;
  failed.code = ResultCode::ImageCorrupt;
  failed.diagnostic = "ImageIO cannot read the image";
  EXPECT(encode_preview_result(failed, bytes) == ResultCode::Ok);
  EXPECT(decode_preview_result(bytes, back) == ResultCode::Ok && back.code == ResultCode::ImageCorrupt &&
         back.diagnostic == failed.diagnostic && back.header.width == 0);

  // Ok needs a valid header for the same sequence; reasons cannot carry line breaks.
  auto wrong = result;
  wrong.header.sequence = 4;
  EXPECT(encode_preview_result(wrong, bytes) == ResultCode::StateConflict);
  wrong = result;
  wrong.header.width = 0;
  EXPECT(encode_preview_result(wrong, bytes) == ResultCode::StateConflict);
  wrong = result;
  wrong.diagnostics = {{"color.local", "two\nlines"}};
  EXPECT(encode_preview_result(wrong, bytes) == ResultCode::StateConflict);
  wrong = result;
  wrong.client_sequence = 0;
  EXPECT(encode_preview_result(wrong, bytes) == ResultCode::StateConflict);

  // Garbage is not a message.
  EXPECT(peek_preview_message_kind({1, 2, 3}, kind) == ResultCode::StateConflict);
}

// 缩略图请求（10）与结果（11）：排队、每个都回；结果与预览结果同字段但种类不同，不能互相解码。
void test_thumbnail_messages() {
  using namespace pixcake;
  using namespace pixcake::ipc;
  ThumbnailRequestMessage request;
  request.request_id = 3;
  request.image_id = "photo-3";
  request.source_path = "/Users/a/b.jpg";
  request.max_edge = 320;
  std::vector<std::uint8_t> bytes;
  EXPECT(encode_thumbnail_request(request, bytes) == ResultCode::Ok);  // no document: the original
  PreviewMessageKind kind{};
  EXPECT(peek_preview_message_kind(bytes, kind) == ResultCode::Ok && kind == PreviewMessageKind::ThumbnailRequest);
  ThumbnailRequestMessage back;
  EXPECT(decode_thumbnail_request(bytes, back) == ResultCode::Ok && back.request_id == 3 && back.max_edge == 320 &&
         back.document.empty() && back.source_path == request.source_path);
  request.document = {0, 7, 0};
  EXPECT(encode_thumbnail_request(request, bytes) == ResultCode::Ok);
  EXPECT(decode_thumbnail_request(bytes, back) == ResultCode::Ok && back.document == request.document);
  auto bad = request;
  bad.max_edge = kMaxThumbnailEdge + 1;
  EXPECT(encode_thumbnail_request(bad, bytes) == ResultCode::StateConflict);
  bad = request;
  bad.request_id = 0;
  EXPECT(encode_thumbnail_request(bad, bytes) == ResultCode::StateConflict);

  PreviewResultMessage result;
  result.client_sequence = 3;
  result.header.sequence = 3;
  result.header.width = 4;
  result.header.height = 2;
  result.header.stride = 16;
  EXPECT(encode_thumbnail_result(result, bytes) == ResultCode::Ok);
  EXPECT(peek_preview_message_kind(bytes, kind) == ResultCode::Ok && kind == PreviewMessageKind::ThumbnailResult);
  PreviewResultMessage decoded;
  EXPECT(decode_thumbnail_result(bytes, decoded) == ResultCode::Ok && decoded.header.width == 4);
  EXPECT(decode_preview_result(bytes, decoded) == ResultCode::StateConflict);  // a thumbnail is not a preview frame
  EXPECT(encode_preview_result(result, bytes) == ResultCode::Ok);
  EXPECT(decode_thumbnail_result(bytes, decoded) == ResultCode::StateConflict);
}

int main() {
  test_c30_preview_messages_use_the_document_record_codec();
  test_c30_process_messages_round_trip();
  test_c30_truncated_newer_or_extended_messages();
  test_request_round_trip_and_bounds();
  test_b48_cancel_and_frame_header_reject_bad_values();
  test_source_and_result_messages();
  test_thumbnail_messages();
  return g_failures == 0 ? 0 : 1;
}
