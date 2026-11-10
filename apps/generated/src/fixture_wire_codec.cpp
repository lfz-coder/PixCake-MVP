#include "pixcake/generated/fixture_wire_codec.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace pixcake::generated {
namespace {

constexpr std::array<std::uint8_t, 8> kRequestMagic = {'P', 'C', 'F', 'X', 'R', 'E', 'Q', '1'};
constexpr std::array<std::uint8_t, 8> kResponseMagic = {'P', 'C', 'F', 'X', 'R', 'S', 'P', '1'};
constexpr std::array<std::uint8_t, 8> kResponseMagicV2 = {'P', 'C', 'F', 'X', 'R', 'S', 'P', '2'};

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (unsigned int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

bool read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& out) {
  if (bytes.size() - offset < 4) return false;
  out = static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
  offset += 4;
  return true;
}

bool read_u64(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint64_t& out) {
  if (bytes.size() - offset < 8) return false;
  out = 0;
  for (unsigned int shift = 0; shift < 64; shift += 8) {
    out |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
  }
  return true;
}

void append_string(std::vector<std::uint8_t>& out, const std::string& value) {
  append_u32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}

void append_string_list(std::vector<std::uint8_t>& out, const std::vector<std::string>& values) {
  append_u32(out, static_cast<std::uint32_t>(values.size()));
  for (const auto& value : values) append_string(out, value);
}

bool has_magic(const std::vector<std::uint8_t>& bytes,
               const std::array<std::uint8_t, 8>& magic) {
  return bytes.size() >= magic.size() && std::memcmp(bytes.data(), magic.data(), magic.size()) == 0;
}

bool skip_string(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
  std::uint32_t size = 0;
  if (!read_u32(bytes, offset, size) || size > bytes.size() - offset) return false;
  offset += size;
  return true;
}

}  // namespace

ResultCode FixtureWireCodec::encode_request(const generate::GenerationTask& task,
                                            net::HttpRequest& out) const {
  if (const ResultCode validation = generate::validate_task(task); validation != ResultCode::Ok) return validation;
  out.method = "POST";
  out.url = "fixture://generation/v1/tasks";
  out.headers = {{"Content-Type", "application/x-pixcake-fixture"},
                 {"X-PixCake-Protocol", "fixture/1"}};
  out.body.clear();
  out.body.insert(out.body.end(), kRequestMagic.begin(), kRequestMagic.end());
  append_string(out.body, task.task_id);
  append_string(out.body, task.image_id);
  append_string(out.body, task.tool.tool_id);
  append_u64(out.body, task.source_revision);
  append_u32(out.body, task.tool.output_count);
  append_u32(out.body, task.parameter_schema_version);
  append_u32(out.body, static_cast<std::uint32_t>(task.parameter_payload.size()));
  out.body.insert(out.body.end(), task.parameter_payload.begin(), task.parameter_payload.end());
  append_string_list(out.body, task.mask_ids);
  append_string_list(out.body, task.reference_image_ids);
  append_string(out.body, task.idempotency_key.empty() ? task.task_id : task.idempotency_key);
  return ResultCode::Ok;
}

ResultCode FixtureWireCodec::decode_response(const net::HttpResponse& response,
                                            RemoteReply& out) const {
  out = {};
  if (response.status_code < 200 || response.status_code >= 300) return ResultCode::NetworkFailed;
  const bool v1 = has_magic(response.body, kResponseMagic);
  const bool v2 = has_magic(response.body, kResponseMagicV2);
  if (!v1 && !v2) return ResultCode::StateConflict;

  std::size_t offset = v1 ? kResponseMagic.size() : kResponseMagicV2.size();
  std::uint32_t code_value = 0;
  if (!read_u32(response.body, offset, code_value)) return ResultCode::StateConflict;
  if (code_value > static_cast<std::uint32_t>(ResultCode::IoFailed)) return ResultCode::StateConflict;
  out.code = static_cast<ResultCode>(code_value);
  if (v1) {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t image_size = 0;
    if (!read_u32(response.body, offset, width) || !read_u32(response.body, offset, height) ||
        !read_u64(response.body, offset, image_size) ||
        image_size > static_cast<std::uint64_t>(response.body.size() - offset) ||
        image_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      return ResultCode::StateConflict;
    }
    RemoteResult result;
    result.width = width;
    result.height = height;
    result.image.assign(response.body.begin() + static_cast<std::ptrdiff_t>(offset),
                        response.body.begin() + static_cast<std::ptrdiff_t>(offset + image_size));
    out.results.push_back(std::move(result));
    offset += static_cast<std::size_t>(image_size);
  } else {
    std::uint32_t count = 0;
    if (!read_u32(response.body, offset, count) || count > 32 || (out.code == ResultCode::Ok && count == 0)) {
      return ResultCode::StateConflict;
    }
    out.results.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      RemoteResult result;
      std::uint64_t image_size = 0;
      std::uint32_t continuation_size = 0;
      if (!read_u32(response.body, offset, result.width) || !read_u32(response.body, offset, result.height) ||
          !read_u64(response.body, offset, image_size) ||
          image_size > static_cast<std::uint64_t>(response.body.size() - offset) ||
          image_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return ResultCode::StateConflict;
      }
      result.image.assign(response.body.begin() + static_cast<std::ptrdiff_t>(offset),
                          response.body.begin() + static_cast<std::ptrdiff_t>(offset + image_size));
      offset += static_cast<std::size_t>(image_size);
      if (!read_u32(response.body, offset, continuation_size) ||
          continuation_size > kMaxContinuationParameters ||
          response.body.size() - offset < continuation_size) {
        return ResultCode::StateConflict;
      }
      result.continuation_parameters.assign(
          response.body.begin() + static_cast<std::ptrdiff_t>(offset),
          response.body.begin() + static_cast<std::ptrdiff_t>(offset + continuation_size));
      offset += continuation_size;
      out.results.push_back(std::move(result));
    }
  }
  if (offset != response.body.size()) return ResultCode::StateConflict;
  return out.code;
}

std::vector<std::uint8_t> FixtureWireCodec::encode_response(const RemoteReply& reply,
                                                            int /*status_code*/) {
  if (reply.results.size() > 32 || (reply.code == ResultCode::Ok && reply.results.empty())) return {};
  std::vector<std::uint8_t> body;
  body.insert(body.end(), kResponseMagicV2.begin(), kResponseMagicV2.end());
  append_u32(body, static_cast<std::uint32_t>(reply.code));
  append_u32(body, static_cast<std::uint32_t>(reply.results.size()));
  for (const auto& result : reply.results) {
    if (result.continuation_parameters.size() > kMaxContinuationParameters) return {};
    append_u32(body, result.width);
    append_u32(body, result.height);
    append_u64(body, static_cast<std::uint64_t>(result.image.size()));
    body.insert(body.end(), result.image.begin(), result.image.end());
    append_u32(body, static_cast<std::uint32_t>(result.continuation_parameters.size()));
    body.insert(body.end(), result.continuation_parameters.begin(), result.continuation_parameters.end());
  }
  return body;
}

bool FixtureWireCodec::request_output_count(const net::HttpRequest& request, std::uint32_t& out) {
  out = 0;
  if (!has_magic(request.body, kRequestMagic)) return false;
  std::size_t offset = kRequestMagic.size();
  // task_id, image_id, and tool_id
  if (!skip_string(request.body, offset) || !skip_string(request.body, offset) ||
      !skip_string(request.body, offset)) {
    return false;
  }
  std::uint64_t source_revision = 0;
  if (!read_u64(request.body, offset, source_revision) || !read_u32(request.body, offset, out)) return false;
  (void)source_revision;
  return out != 0 && out <= generate::kMaxGenerationOutputs;
}

FixtureHttpTransport::FixtureHttpTransport(RemoteReply response) : response_(std::move(response)) {}

net::HttpResponse FixtureHttpTransport::send(const net::HttpRequest& request,
                                             std::chrono::milliseconds /*timeout*/) {
  net::HttpResponse response;
  response.transport_code = ResultCode::Ok;
  response.status_code = 200;
  response.headers = {{"Content-Type", "application/x-pixcake-fixture"}};
  RemoteReply reply = response_;
  std::uint32_t count = 0;
  if (reply.code == ResultCode::Ok && FixtureWireCodec::request_output_count(request, count) &&
      !reply.results.empty()) {
    const RemoteResult prototype = reply.results.front();
    reply.results.assign(count, prototype);
  }
  response.body = FixtureWireCodec::encode_response(reply);
  return response;
}

}  // namespace pixcake::generated
