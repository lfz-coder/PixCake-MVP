#include "pixcake/ipc/preview_protocol.hpp"

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

int main() {
  test_request_round_trip_and_bounds();
  test_cancel_and_frame_header_reject_bad_values();
  return g_failures == 0 ? 0 : 1;
}
