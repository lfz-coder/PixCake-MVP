#include "pixcake/cloud_transport/http_cloud_gateway.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <string_view>
#include <utility>

namespace pixcake::cloud_transport {
namespace {
using Milliseconds = std::chrono::milliseconds;

class SteadyClock final : public RequestClock {
 public:
  std::chrono::steady_clock::time_point now() const override {
    return std::chrono::steady_clock::now();
  }
};

const RequestClock& system_clock() {
  static const SteadyClock clock;
  return clock;
}

class Budget {
 public:
  Budget(const RequestClock& clock, Milliseconds timeout)
      : clock_(clock), start_(clock.now()), last_(start_), timeout_(timeout) {}

  Milliseconds remaining() {
    const auto now = clock_.now();
    if (now < last_) return Milliseconds{0};
    last_ = now;
    const auto elapsed = now - start_;
    if (elapsed >= timeout_) return Milliseconds{0};
    // Round down: never offer more time to I/O than the overall budget.
    return std::chrono::duration_cast<Milliseconds>(timeout_ - elapsed);
  }

 private:
  const RequestClock& clock_;
  std::chrono::steady_clock::time_point start_;
  std::chrono::steady_clock::time_point last_;
  Milliseconds timeout_;
};

bool no_controls(std::string_view value) {
  return std::none_of(value.begin(), value.end(), [](unsigned char byte) {
    return byte < 0x20 || byte == 0x7f;
  });
}

bool valid_text(std::string_view value, std::size_t limit) {
  return !value.empty() && value.size() <= limit && no_controls(value);
}

bool valid_identity(const cloud::CloudIdentity& identity) {
  return valid_text(identity.account_id, cloud::kMaxCloudIdBytes) &&
         valid_text(identity.session_handle, cloud::kMaxCloudSessionBytes);
}

bool valid_limits(const CloudHttpLimits& limits) {
  constexpr CloudHttpLimits upper;
  const auto bounded = [](std::size_t value, std::size_t maximum) {
    return value > 0 && value <= maximum;
  };
  return bounded(limits.max_pages, upper.max_pages) &&
         bounded(limits.max_cursor_bytes, upper.max_cursor_bytes) &&
         bounded(limits.max_page_body_bytes, upper.max_page_body_bytes) &&
         bounded(limits.max_total_body_bytes, upper.max_total_body_bytes) &&
         bounded(limits.max_header_bytes, upper.max_header_bytes) &&
         bounded(limits.max_headers, upper.max_headers) &&
         bounded(limits.max_request_body_bytes, upper.max_request_body_bytes);
}

bool headers_fit(const std::vector<net::Header>& headers, std::size_t count,
                 std::size_t bytes) {
  if (headers.size() > count) return false;
  for (const auto& header : headers) {
    if (header.first.size() > bytes) return false;
    bytes -= header.first.size();
    if (header.second.size() > bytes) return false;
    bytes -= header.second.size();
  }
  return true;
}

bool valid_request(const net::HttpRequest& request, const CloudHttpLimits& limits) {
  // Only a policy check. Production codecs/transports still validate complete
  // URL/header syntax, configured origins, TLS and provider-specific auth.
  constexpr std::size_t max_url_bytes = 16 * 1024;
  constexpr CloudHttpLimits upper;
  if ((request.method != "GET" && request.method != "POST") ||
      !valid_text(request.url, max_url_bytes) || request.url.size() <= 8 ||
      request.url.compare(0, 8, "https://") != 0 ||
      request.url.find(' ') != std::string::npos ||
      request.body.size() > limits.max_request_body_bytes ||
      !headers_fit(request.headers, upper.max_headers, upper.max_header_bytes)) {
    return false;
  }
  return std::all_of(request.headers.begin(), request.headers.end(), [](const auto& header) {
    return !header.first.empty() && no_controls(header.first) && no_controls(header.second);
  });
}

template <typename Item>
struct DirectoryReply {
  ResultCode code = ResultCode::Ok;
  std::vector<Item> items;
};

template <typename Page, typename Item, typename Encode, typename Decode>
DirectoryReply<Item> fetch_directory(net::HttpTransport& transport, const RequestClock& clock,
                                     const CloudHttpLimits& limits, Milliseconds timeout,
                                     std::size_t item_limit, Encode encode, Decode decode) {
  if (!valid_limits(limits) || timeout.count() <= 0 || timeout > cloud::kMaxCloudRequestTimeout) {
    return {ResultCode::StateConflict, {}};
  }
  Budget budget(clock, timeout);
  std::vector<Item> items;
  std::string cursor;
  std::set<std::string> visited;
  std::size_t total_bytes = 0;
  for (std::size_t index = 0; index < limits.max_pages; ++index) {
    if (budget.remaining().count() <= 0 || total_bytes == limits.max_total_body_bytes) {
      return {ResultCode::NetworkFailed, {}};
    }
    net::HttpRequest request;
    const auto encoded = encode(cursor, request);
    if (budget.remaining().count() <= 0) return {ResultCode::NetworkFailed, {}};
    if (encoded != ResultCode::Ok) return {encoded, {}};
    if (!valid_request(request, limits)) return {ResultCode::StateConflict, {}};
    request.follow_redirects = false;
    request.response_limits = {
        std::min(limits.max_page_body_bytes, limits.max_total_body_bytes - total_bytes),
        limits.max_header_bytes, limits.max_headers};
    const auto remaining = budget.remaining();
    if (remaining.count() <= 0) return {ResultCode::NetworkFailed, {}};
    auto response = transport.send(request, remaining);
    if (response.transport_code != ResultCode::Ok) return {response.transport_code, {}};
    if (budget.remaining().count() <= 0) return {ResultCode::NetworkFailed, {}};
    if (response.status_code == 401 || response.status_code == 403) {
      return {ResultCode::PermissionDenied, {}};
    }
    if (response.status_code < 200 || response.status_code >= 300 ||
        response.body.size() > request.response_limits.body_bytes ||
        !headers_fit(response.headers, limits.max_headers, limits.max_header_bytes)) {
      return {ResultCode::NetworkFailed, {}};
    }
    total_bytes += response.body.size();
    Page page;
    const auto decoded = decode(response, page);
    if (budget.remaining().count() <= 0) return {ResultCode::NetworkFailed, {}};
    if (decoded != ResultCode::Ok) return {decoded, {}};
    if (page.items.size() > item_limit - items.size() ||
        page.next_cursor.size() > limits.max_cursor_bytes) {
      return {ResultCode::NetworkFailed, {}};
    }
    if (!page.next_cursor.empty() && !visited.insert(page.next_cursor).second) {
      return {ResultCode::NetworkFailed, {}};
    }
    items.insert(items.end(), std::make_move_iterator(page.items.begin()),
                 std::make_move_iterator(page.items.end()));
    if (budget.remaining().count() <= 0) return {ResultCode::NetworkFailed, {}};
    if (page.next_cursor.empty()) return {ResultCode::Ok, std::move(items)};
    cursor = std::move(page.next_cursor);
  }
  return {ResultCode::NetworkFailed, {}};
}
}  // namespace

HttpCloudGateway::HttpCloudGateway(net::HttpTransport& transport, const CloudWireCodec& codec)
    : HttpCloudGateway(transport, codec, system_clock()) {}

HttpCloudGateway::HttpCloudGateway(net::HttpTransport& transport, const CloudWireCodec& codec,
                                   const RequestClock& clock, CloudHttpLimits limits)
    : transport_(transport), codec_(codec), clock_(clock), limits_(limits) {}

cloud::CloudSpacesReply HttpCloudGateway::list_spaces(const cloud::CloudIdentity& identity,
                                                      Milliseconds timeout) {
  if (!valid_identity(identity)) return {ResultCode::StateConflict, {}};
  auto reply = fetch_directory<SpacesPage, cloud::CloudSpace>(
      transport_, clock_, limits_, timeout, cloud::kMaxCloudSpaces,
      [&](const auto& cursor, auto& request) { return codec_.encode_spaces(identity, cursor, request); },
      [&](const auto& response, auto& page) { return codec_.decode_spaces(response, page); });
  return {reply.code, std::move(reply.items)};
}

cloud::CloudProjectsReply HttpCloudGateway::list_projects(const cloud::CloudIdentity& identity,
                                                          const std::string& space_id,
                                                          Milliseconds timeout) {
  if (!valid_identity(identity) || !valid_text(space_id, cloud::kMaxCloudIdBytes)) {
    return {ResultCode::StateConflict, {}};
  }
  auto reply = fetch_directory<ProjectsPage, cloud::CloudProjectSummary>(
      transport_, clock_, limits_, timeout, cloud::kMaxCloudProjects,
      [&](const auto& cursor, auto& request) { return codec_.encode_projects(identity, space_id, cursor, request); },
      [&](const auto& response, auto& page) { return codec_.decode_projects(response, page); });
  return {reply.code, std::move(reply.items)};
}

}  // namespace pixcake::cloud_transport
