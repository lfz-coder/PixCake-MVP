#include "pixcake/generated/http_remote_tool.hpp"

namespace pixcake::generated {

HttpRemoteTool::HttpRemoteTool(net::HttpTransport& transport, const GenerationWireCodec& codec,
                               std::chrono::milliseconds timeout)
    : transport_(transport), codec_(codec), timeout_(timeout) {}

RemoteReply HttpRemoteTool::run(const generate::GenerationTask& task) {
  RemoteReply reply;
  net::HttpRequest request;
  if (const ResultCode code = codec_.encode_request(task, request); code != ResultCode::Ok) {
    reply.code = code;
    return reply;
  }

  const net::HttpResponse response = transport_.send(request, timeout_);
  if (response.transport_code != ResultCode::Ok) {
    reply.code = response.transport_code;
    return reply;
  }

  if (const ResultCode code = codec_.decode_response(response, reply); code != ResultCode::Ok) {
    reply.code = code;
  }
  return reply;
}

}  // namespace pixcake::generated
