#include "pixcake/net/curl_http_transport.hpp"

#include <curl/curl.h>
#include <curl/urlapi.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <set>
#include <string_view>
#include <utility>

namespace pixcake::net {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kMiB = 1024 * 1024;
using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
using Easy = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using Headers = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

HttpResponse failure(ResultCode code) { return {code, 0, {}, {}}; }
std::string lower(std::string value) {
  for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  return value;
}
bool ascii_url(const std::string& value) {
  return !value.empty() && value.size() <= 16 * 1024 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) { return c > 32 && c < 127 && c != '\\'; });
}
bool token(std::string_view value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string_view::npos;
  });
}
bool header_value(std::string_view value) {
  return std::none_of(value.begin(), value.end(), [](unsigned char c) { return (c < 32 && c != '\t') || c == 127; });
}
std::string_view trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == std::string_view::npos) return {};
  const auto last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}
bool grow(std::size_t& current, std::size_t amount, std::size_t limit) {
  if (current > limit || amount > limit - current) return false;
  current += amount;
  return true;
}
bool valid_headers(const std::vector<Header>& headers) {
  if (headers.size() > 128) return false;
  std::size_t bytes = 0;
  std::set<std::string> names;
  for (const auto& header : headers) {
    if (!grow(bytes, header.first.size(), 32 * 1024) || !grow(bytes, header.second.size(), 32 * 1024) ||
        !token(header.first) || !header_value(header.second)) return false;
    auto name = lower(header.first);
    if (!names.insert(name).second || name == "host" || name == "content-length" || name == "transfer-encoding" ||
        name == "connection" || name == "proxy-authorization" || name == "proxy-connection" ||
        name == "accept-encoding" || name == "expect" || name == "trailer" || name == "te" || name == "upgrade") return false;
  }
  return true;
}
bool limits(const HttpResponseLimits& requested, HttpResponseLimits& effective) {
  effective = {requested.body_bytes ? requested.body_bytes : 8 * kMiB,
               requested.header_bytes ? requested.header_bytes : 32 * 1024,
               requested.header_count ? requested.header_count : 128};
  return effective.body_bytes <= 64 * kMiB && effective.header_bytes <= 64 * 1024 && effective.header_count <= 256;
}

struct CurlRuntime {
  CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
  ~CurlRuntime() { if (code == CURLE_OK) curl_global_cleanup(); }
};
bool runtime_ready() {
  static const CurlRuntime runtime;
  if (runtime.code != CURLE_OK) return false;
  const auto* info = curl_version_info(CURLVERSION_NOW);
  return info && info->version_num >= 0x080701 && (info->features & CURL_VERSION_SSL) &&
         (info->features & CURL_VERSION_ASYNCHDNS);
}
bool part(CURLU* url, CURLUPart which, std::string& value, unsigned int flags = 0) {
  char* raw = nullptr;
  const auto code = curl_url_get(url, which, &raw, flags);
  const std::unique_ptr<char, decltype(&curl_free)> owned(raw, &curl_free);
  if (code != CURLUE_OK) return false;
  value = raw;
  return true;
}
bool absent(CURLU* url, CURLUPart which, CURLUcode expected) {
  char* raw = nullptr;
  const auto code = curl_url_get(url, which, &raw, 0);
  curl_free(raw);
  return code == expected;
}
bool parse_url(const std::string& value, CURLU* url) {
  if (!ascii_url(value) || value.find('#') != std::string::npos ||
      curl_url_set(url, CURLUPART_URL, value.c_str(), CURLU_DISALLOW_USER) != CURLUE_OK) return false;
  std::string scheme;
  return part(url, CURLUPART_SCHEME, scheme) && lower(scheme) == "https" &&
         absent(url, CURLUPART_USER, CURLUE_NO_USER) && absent(url, CURLUPART_PASSWORD, CURLUE_NO_PASSWORD) &&
         absent(url, CURLUPART_ZONEID, CURLUE_NO_ZONEID);
}
bool same_origin(const std::string& origin, const std::string& requested, CURLU* request_url) {
  Url base(curl_url(), &curl_url_cleanup);
  if (!base || !parse_url(origin, base.get()) || !parse_url(requested, request_url) ||
      origin.find('?') != std::string::npos) return false;
  std::string path, host, other_host, port, other_port;
  return part(base.get(), CURLUPART_PATH, path) && path == "/" &&
         part(base.get(), CURLUPART_HOST, host) && part(request_url, CURLUPART_HOST, other_host) &&
         part(base.get(), CURLUPART_PORT, port, CURLU_DEFAULT_PORT) &&
         part(request_url, CURLUPART_PORT, other_port, CURLU_DEFAULT_PORT) &&
         port != "0" && lower(host) == lower(other_host) && port == other_port;
}

struct Receive {
  HttpResponse response;
  HttpResponseLimits limits;
  Clock::time_point deadline;
  std::size_t header_bytes = 0;
  std::size_t header_count = 0;
};
std::size_t body_callback(char* bytes, std::size_t size, std::size_t count, void* opaque) noexcept {
  try {
    auto& state = *static_cast<Receive*>(opaque);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return 0;
    const auto length = size * count;
    auto current = state.response.body.size();
    if (Clock::now() >= state.deadline || !grow(current, length, state.limits.body_bytes)) return 0;
    state.response.body.insert(state.response.body.end(), bytes, bytes + length);
    return length;
  } catch (...) { return 0; } // No C++ exception may cross the C callback boundary.
}
std::size_t header_callback(char* bytes, std::size_t size, std::size_t count, void* opaque) noexcept {
  try {
    auto& state = *static_cast<Receive*>(opaque);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return 0;
    const auto length = size * count;
    if (Clock::now() >= state.deadline || length < 2) return 0;
    std::string_view line(bytes, length);
    if (line.substr(line.size() - 2) != "\r\n") return 0;
    line.remove_suffix(2);
    if (line.empty()) return length;
    if (line.substr(0, 5) == "HTTP/") {
      // libcurl parses status. Keep only final headers, but never reset the
      // cumulative budgets when an interim response precedes the final one.
      state.response.headers.clear();
      return length;
    }
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) return 0;
    const auto name = line.substr(0, colon);
    const auto value = trim(line.substr(colon + 1));
    if (!token(name) || !header_value(value) ||
        !grow(state.header_bytes, name.size(), state.limits.header_bytes) ||
        !grow(state.header_bytes, value.size(), state.limits.header_bytes) ||
        !grow(state.header_count, 1, state.limits.header_count)) return 0;
    state.response.headers.emplace_back(name, value);
    return length;
  } catch (...) { return 0; }
}
}  // namespace

CurlHttpTransport::CurlHttpTransport(std::string https_origin, std::string ca_file)
    : origin_(std::move(https_origin)), ca_file_(std::move(ca_file)) {}

HttpResponse CurlHttpTransport::send(const HttpRequest& request, std::chrono::milliseconds timeout) {
  const auto started = Clock::now();
  try {
    HttpResponseLimits effective;
    if (timeout.count() <= 0 || timeout.count() > 60000 || request.follow_redirects ||
        (request.method != "GET" && request.method != "POST" && request.method != "DELETE") ||
        (request.method != "POST" && !request.body.empty()) || request.body.size() > 8 * kMiB ||
        !valid_headers(request.headers) || !limits(request.response_limits, effective) ||
        ca_file_.size() > 4096 || ca_file_.find('\0') != std::string::npos) return failure(ResultCode::StateConflict);
    if (!runtime_ready()) return failure(ResultCode::NetworkFailed);
    Url url(curl_url(), &curl_url_cleanup);
    if (!url) return failure(ResultCode::NetworkFailed);
    if (!same_origin(origin_, request.url, url.get())) return failure(ResultCode::StateConflict);
    Headers headers(nullptr, &curl_slist_free_all);
    for (const auto& header : request.headers) {
      // A semicolon asks libcurl to send an explicitly empty header value.
      const auto line = header.first + (header.second.empty() ? ";" : ": " + header.second);
      auto* appended = curl_slist_append(headers.get(), line.c_str());
      if (!appended) return failure(ResultCode::NetworkFailed);
      headers.release();
      headers.reset(appended);
    }
    Receive receive{{}, effective, started + timeout};
    // Destroy the easy handle before the header list and callback state it
    // references, including on setup failures and early returns.
    Easy handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) return failure(ResultCode::NetworkFailed);
    const auto option = [&](CURLoption key, auto value) { return curl_easy_setopt(handle.get(), key, value) == CURLE_OK; };
    if (!option(CURLOPT_CURLU, url.get()) || !option(CURLOPT_PROTOCOLS_STR, "https") ||
        !option(CURLOPT_REDIR_PROTOCOLS_STR, "https") || !option(CURLOPT_FOLLOWLOCATION, 0L) ||
        !option(CURLOPT_SSL_VERIFYPEER, 1L) || !option(CURLOPT_SSL_VERIFYHOST, 2L) ||
        !option(CURLOPT_NOSIGNAL, 1L) || !option(CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED)) ||
        !option(CURLOPT_PROXY, "") || !option(CURLOPT_HTTPHEADER, headers.get()) ||
        !option(CURLOPT_ACCEPT_ENCODING, "gzip, deflate") || !option(CURLOPT_WRITEFUNCTION, &body_callback) ||
        !option(CURLOPT_WRITEDATA, &receive) || !option(CURLOPT_HEADERFUNCTION, &header_callback) ||
        !option(CURLOPT_HEADERDATA, &receive) ||
        (!ca_file_.empty() && !option(CURLOPT_CAINFO, ca_file_.c_str()))) return failure(ResultCode::NetworkFailed);
    if (request.method == "POST") {
      if (!option(CURLOPT_POST, 1L) || !option(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size())) ||
          !option(CURLOPT_POSTFIELDS, request.body.empty() ? "" : reinterpret_cast<const char*>(request.body.data())))
        return failure(ResultCode::NetworkFailed);
    } else if (request.method == "DELETE" && !option(CURLOPT_CUSTOMREQUEST, "DELETE")) return failure(ResultCode::NetworkFailed);

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(receive.deadline - Clock::now()).count();
    if (remaining <= 0) return failure(ResultCode::NetworkFailed);
    if (!option(CURLOPT_TIMEOUT_MS, static_cast<long>(remaining)) ||
        !option(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(std::min<std::int64_t>(remaining, 10000)))) return failure(ResultCode::NetworkFailed);
    const auto result = curl_easy_perform(handle.get());
    long status = 0;
    if (result != CURLE_OK || Clock::now() >= receive.deadline ||
        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK || status < 100 || status > 599)
      return failure(ResultCode::NetworkFailed);
    receive.response.status_code = static_cast<int>(status);
    return std::move(receive.response);
  } catch (const std::exception&) { return failure(ResultCode::NetworkFailed); }
}
}  // namespace pixcake::net
