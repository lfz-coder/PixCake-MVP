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

void test_cancel_and_frame_header_reject_bad_values() {
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
         export_out.total == 10 && export_out.code == pixcake::ResultCode::DiskFull);

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

int main() {
  test_c30_preview_messages_use_the_document_record_codec();
  test_c30_process_messages_round_trip();
  test_c30_truncated_newer_or_extended_messages();
  test_request_round_trip_and_bounds();
  test_cancel_and_frame_header_reject_bad_values();
  return g_failures == 0 ? 0 : 1;
}
