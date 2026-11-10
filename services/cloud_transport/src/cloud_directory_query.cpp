#include "pixcake/cloud_transport/cloud_directory_query.hpp"

#include "pixcake/cloud/cloud_service.hpp"
#include "pixcake/cloud_transport/http_cloud_gateway.hpp"

#include <chrono>
#include <exception>
#include <utility>

namespace pixcake::cloud_transport {
CloudDirectoryReply query_directory(net::HttpTransport& transport, const BackendV1Codec& codec,
                                     const cloud::CloudIdentity& identity, const std::string& space_id,
                                     std::chrono::milliseconds timeout) {
  const auto failed = [](ResultCode code) { CloudDirectoryReply reply; reply.code = code; return reply; };
  if (timeout.count() <= 0 || timeout.count() > 5000) return failed(ResultCode::StateConflict);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  const auto remaining = [&] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
  };
  try {
    net::HttpRequest request;
    auto code = codec.encode_account(identity.session_handle, request);
    if (code != ResultCode::Ok) return failed(code);
    request.response_limits = {16 * 1024, 32 * 1024, 128};
    auto budget = remaining();
    if (budget.count() <= 0) return failed(ResultCode::NetworkFailed);
    const auto response = transport.send(request, budget);
    if (response.transport_code != ResultCode::Ok) return failed(response.transport_code);
    if (remaining().count() <= 0 || response.body.size() > request.response_limits.body_bytes)
      return failed(ResultCode::NetworkFailed);
    CloudDirectoryReply candidate;
    code = codec.decode_account(response, candidate.account);
    if (code != ResultCode::Ok) return failed(code);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (candidate.account.session_expires_at <= now ||
        (!identity.account_id.empty() && identity.account_id != candidate.account.account_id))
      return failed(ResultCode::PermissionDenied);
    account::AccountSession account;
    account::AccountSnapshot snapshot;
    snapshot.account_id = candidate.account.account_id;
    snapshot.session_handle = identity.session_handle;
    code = account.sign_in(std::move(snapshot));
    if (code != ResultCode::Ok) return failed(code);
    cloud::CloudSession session(account);
    session.open();
    HttpCloudGateway gateway(transport, codec);
    cloud::CloudService service(session, gateway);
    budget = remaining();
    if (budget.count() <= 0) return failed(ResultCode::NetworkFailed);
    code = service.refresh_spaces(budget);
    if (code != ResultCode::Ok) return failed(code);
    candidate.spaces = session.spaces();
    if (!space_id.empty()) {
      budget = remaining();
      if (budget.count() <= 0) return failed(ResultCode::NetworkFailed);
      auto projects = service.list_projects(space_id, budget);
      if (projects.code != ResultCode::Ok) return failed(projects.code);
      candidate.selected_space = space_id;
      candidate.projects = std::move(projects.projects);
    }
    if (remaining().count() <= 0) return failed(ResultCode::NetworkFailed);
    return candidate;
  } catch (const std::exception&) { return failed(ResultCode::NetworkFailed); }
}
}  // namespace pixcake::cloud_transport
