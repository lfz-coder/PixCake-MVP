#include "pixcake/cloud_transport/backend_v1_codec.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace pixcake::cloud_transport {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kBodyBytes = 2 * 1024 * 1024;

bool url_safe(std::string_view value, bool cursor = false) {
  return std::all_of(value.begin(), value.end(), [cursor](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || (cursor && c == '.');
  });
}
bool identifier(std::string_view value, std::string_view prefix) {
  return value.size() == prefix.size() + 32 && value.substr(0, prefix.size()) == prefix &&
         std::all_of(value.begin() + static_cast<std::ptrdiff_t>(prefix.size()), value.end(), [](char c) {
           return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
         });
}
bool valid_cursor(std::string_view value) { return value.size() <= 4096 && url_safe(value, true); }
bool origin_shape(const std::string& origin) {
  // This is only an encoder shape restriction. libcurl owns URL parsing and
  // canonical origin comparison; do not duplicate a general URL parser here.
  return origin.size() > 8 && origin.size() <= 2048 && origin.compare(0, 8, "https://") == 0 &&
         origin.find_first_of("/?#@\\%", 8) == std::string::npos &&
         std::all_of(origin.begin(), origin.end(), [](unsigned char c) { return c > 32 && c < 127; });
}
std::string lower(std::string value) {
  for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  return value;
}
bool json_content_type(const net::HttpResponse& response) {
  if (response.headers.size() > 128) return false;
  std::size_t remaining = 32 * 1024;
  bool found = false;
  for (const auto& header : response.headers) {
    if (header.first.size() > remaining) return false;
    remaining -= header.first.size();
    if (header.second.size() > remaining) return false;
    remaining -= header.second.size();
    if (lower(header.first) != "content-type") continue;
    if (found) return false;
    found = true;
    auto type = lower(header.second.substr(0, header.second.find(';')));
    const auto first = type.find_first_not_of(" \t");
    const auto last = type.find_last_not_of(" \t");
    if (first == std::string::npos || type.substr(first, last - first + 1) != "application/json") return false;
  }
  return found;
}

// The library parses syntax, UTF-8 and numeric overflow. Its public SAX API
// lets this adapter bound the DOM and reject duplicate keys before insertion.
// The lexer may temporarily allocate a string, bounded by the raw body limit.
class BoundedJson final : public nlohmann::json_sax<Json> {
 public:
  Json root;
  bool null() override { return put(nullptr) != nullptr; }
  bool boolean(bool value) override { return put(value) != nullptr; }
  bool number_integer(number_integer_t value) override { return put(value) != nullptr; }
  bool number_unsigned(number_unsigned_t value) override { return put(value) != nullptr; }
  bool number_float(number_float_t, const string_t&) override { return false; }
  bool string(string_t& value) override { return value.size() <= 4096 && put(std::move(value)); }
  bool binary(binary_t&) override { return false; }
  bool start_object(std::size_t) override { return start(Json::object()); }
  bool start_array(std::size_t) override { return start(Json::array()); }
  bool key(string_t& value) override {
    if (stack_.empty() || value.size() > 64) return false;
    auto& frame = stack_.back();
    if (frame.keys.size() >= 32 || !frame.keys.insert(value).second) return false;
    frame.key = std::move(value);
    return true;
  }
  bool end_object() override { stack_.pop_back(); return true; }
  bool end_array() override { stack_.pop_back(); return true; }
  bool parse_error(std::size_t, const std::string&, const Json::exception&) override { return false; }

 private:
  struct Frame { Json* node; std::set<std::string> keys; std::string key; };
  std::vector<Frame> stack_;
  std::size_t nodes_ = 0;
  Json* put(Json value) {
    if (++nodes_ > 4096) return nullptr;
    if (stack_.empty()) { root = std::move(value); return &root; }
    auto& frame = stack_.back();
    if (frame.node->is_array()) {
      if (frame.node->size() >= 100) return nullptr;
      frame.node->push_back(std::move(value));
      return &frame.node->back();
    }
    return &((*frame.node)[frame.key] = std::move(value));
  }
  bool start(Json value) {
    if (stack_.size() >= 8) return false;
    auto* node = put(std::move(value));
    if (!node) return false;
    stack_.push_back({node, {}, {}});
    return true;
  }
};

bool fields(const Json& object, std::initializer_list<const char*> names) {
  if (!object.is_object() || object.size() != names.size()) return false;
  return std::all_of(names.begin(), names.end(), [&](const char* key) { return object.contains(key); });
}
bool identifier_field(const Json& value, std::string_view prefix, std::string& out) {
  if (!value.is_string()) return false;
  const auto& str = value.get_ref<const std::string&>();
  if (!identifier(str, prefix)) return false;
  out = str;
  return true;
}
bool name(const Json& value, std::string& out) {
  if (!value.is_string()) return false;
  const auto& str = value.get_ref<const std::string&>();
  if (str.empty() || str.size() > cloud::kMaxCloudNameBytes ||
      std::any_of(str.begin(), str.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
  out = str;
  return true;
}
bool nonnegative(const Json& value, std::int64_t& out) {
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
    out = static_cast<std::int64_t>(number);
    return true;
  }
  if (!value.is_number_integer()) return false;
  out = value.get<std::int64_t>();
  return out >= 0;
}
ResultCode parse_response(const net::HttpResponse& response, Json& root) {
  if (response.transport_code != ResultCode::Ok) return ResultCode::NetworkFailed;
  if (response.status_code == 401 || response.status_code == 403) return ResultCode::PermissionDenied;
  if (response.status_code != 200 || response.body.empty() || response.body.size() > kBodyBytes ||
      !json_content_type(response)) return ResultCode::NetworkFailed;
  BoundedJson parser;
  if (!Json::sax_parse(response.body.begin(), response.body.end(), &parser)) return ResultCode::NetworkFailed;
  root = std::move(parser.root);
  return ResultCode::Ok;
}
ResultCode envelope(const net::HttpResponse& response, Json& root) {
  const auto code = parse_response(response, root);
  if (code != ResultCode::Ok) return code;
  if (!fields(root, {"api_version", "items", "next_cursor"})) return ResultCode::NetworkFailed;
  std::int64_t version = 0;
  if (!nonnegative(root["api_version"], version) || version != 1 ||
      !root["items"].is_array() || !root["next_cursor"].is_string() ||
      !valid_cursor(root["next_cursor"].get_ref<const std::string&>())) return ResultCode::NetworkFailed;
  return ResultCode::Ok;
}
bool space(const Json& item, cloud::CloudSpace& out) {
  if (!fields(item, {"id", "name", "kind", "permissions"}) ||
      !identifier_field(item["id"], "sp_", out.id) || !name(item["name"], out.name)) return false;
  if (item["kind"] == "personal") out.kind = cloud::SpaceKind::Personal;
  else if (item["kind"] == "team") out.kind = cloud::SpaceKind::Team;
  else return false;
  const auto& permissions = item["permissions"];
  if (!permissions.is_array() || permissions.size() > 3) return false;
  for (const auto& value : permissions) {
    cloud::Permission permission;
    if (value == "read") permission = cloud::Permission::Read;
    else if (value == "write") permission = cloud::Permission::Write;
    else if (value == "manage") permission = cloud::Permission::Manage;
    else return false;
    if (std::find(out.permissions.begin(), out.permissions.end(), permission) != out.permissions.end()) return false;
    out.permissions.push_back(permission);
  }
  return true;
}
bool project(const Json& item, cloud::CloudProjectSummary& out) {
  return fields(item, {"id", "space_id", "name", "revision", "updated_at", "size_bytes"}) &&
         identifier_field(item["id"], "prj_", out.id) && identifier_field(item["space_id"], "sp_", out.space_id) &&
         name(item["name"], out.name) && nonnegative(item["revision"], out.revision) &&
         nonnegative(item["updated_at"], out.updated_at) && nonnegative(item["size_bytes"], out.size_bytes);
}
template<typename Page, typename Decode>
ResultCode decode(const net::HttpResponse& response, Page& page, Decode item_decoder) {
  page = {};
  try {
    Json root;
    const auto code = envelope(response, root);
    if (code != ResultCode::Ok) return code;
    Page candidate;
    for (const auto& item : root["items"]) {
      typename decltype(candidate.items)::value_type value;
      if (!item_decoder(item, value)) return ResultCode::NetworkFailed;
      candidate.items.push_back(std::move(value));
    }
    candidate.next_cursor = root["next_cursor"].get<std::string>();
    page = std::move(candidate);
    return ResultCode::Ok;
  } catch (const std::exception&) { return ResultCode::NetworkFailed; }
}
}  // namespace

BackendV1Codec::BackendV1Codec(std::string https_origin, std::size_t page_size)
    : origin_(std::move(https_origin)), page_size_(page_size) {
  if (!origin_.empty() && origin_.back() == '/') origin_.pop_back();
}
ResultCode BackendV1Codec::encode_auth(const account::AuthRequest& input, net::HttpRequest& request) const {
  using account::AuthOperation;
  request = {};
  if (!origin_shape(origin_)) return ResultCode::StateConflict;
  const auto token = [](const std::string& value) { return value.size() == 43 && url_safe(value); };
  const auto digits = [](const std::string& value) {
    return std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
  };
  try {
    std::string path;
    Json body = Json::object();
    if (input.operation == AuthOperation::Configure) path = "/v1/auth/config";
    else if (input.operation == AuthOperation::Refresh || input.operation == AuthOperation::Logout) {
      if (!token(input.token)) return ResultCode::StateConflict;
      path = input.operation == AuthOperation::Refresh ? "/v1/auth/refresh" : "/v1/auth/logout";
      body["refresh_token"] = input.token;
    } else {
      if (input.phone.size() != 11 || input.phone[0] != '1' || input.phone[1] < '3' ||
          input.phone[1] > '9' || !digits(input.phone)) return ResultCode::StateConflict;
      body["phone"] = input.phone;
      if (input.operation == AuthOperation::SendCode) {
        if (input.purpose != "login" && input.purpose != "register" && input.purpose != "reset") return ResultCode::StateConflict;
        path = "/v1/auth/code"; body["purpose"] = input.purpose;
      } else {
        if (input.operation != AuthOperation::PasswordLogin) {
          if (!identifier(input.challenge_id, "otp_") || input.code.size() != 6 || !digits(input.code)) return ResultCode::StateConflict;
          body["challenge_id"] = input.challenge_id; body["code"] = input.code;
        }
        if (input.operation != AuthOperation::SmsLogin) {
          if (input.password.empty() || input.password.size() > 512) return ResultCode::StateConflict;
          body["password"] = input.password;
        }
        switch (input.operation) {
          case AuthOperation::SmsLogin: path = "/v1/auth/sms/login"; break;
          case AuthOperation::PasswordLogin: path = "/v1/auth/password/login"; break;
          case AuthOperation::Register: path = "/v1/auth/register"; break;
          case AuthOperation::ResetPassword: path = "/v1/auth/password/reset"; break;
          default: return ResultCode::StateConflict;
        }
      }
    }
    request = {input.operation == AuthOperation::Configure ? "GET" : "POST", origin_ + path,
               {{"Accept", "application/json"}}, {}};
    if (request.method == "POST") {
      request.headers.emplace_back("Content-Type", "application/json");
      const auto bytes = body.dump(); request.body.assign(bytes.begin(), bytes.end());
    }
    request.response_limits = {16384, 32768, 128};
    return ResultCode::Ok;
  } catch (const std::exception&) { request = {}; return ResultCode::StateConflict; }
}
account::AuthReply BackendV1Codec::decode_auth(account::AuthOperation operation, const net::HttpResponse& response) const {
  using account::AuthError;
  using account::AuthOperation;
  const auto failed = [](AuthError error, ResultCode code = ResultCode::NetworkFailed) {
    account::AuthReply reply; reply.code = code; reply.error = error; return reply;
  };
  if (response.transport_code != ResultCode::Ok || response.body.size() > 16384) return failed(AuthError::Network);
  if (operation == AuthOperation::Logout && response.status_code == 204 && response.body.empty()) return {};
  if (response.status_code != 200 && response.status_code != 400 && response.status_code != 401 &&
      response.status_code != 409 && response.status_code != 429 && response.status_code != 503) return failed(AuthError::Network);
  try {
    auto json_response = response;
    json_response.status_code = 200;
    Json root;
    std::int64_t version = 0;
    if (parse_response(json_response, root) != ResultCode::Ok || !root.is_object() || !root.contains("api_version") ||
        !nonnegative(root["api_version"], version) || version != 1) return failed(AuthError::Network);
    if (response.status_code != 200) {
      if (!fields(root, {"api_version", "error"}) || !fields(root["error"], {"code", "message"}) ||
          !root["error"]["message"].is_string()) return failed(AuthError::Network);
      const auto& code = root["error"]["code"];
      if (response.status_code == 401 && (code == "unauthorized" || code == "credentials_invalid"))
        return failed(AuthError::CredentialsInvalid, ResultCode::PermissionDenied);
      if (response.status_code == 429 && code == "rate_limited") return failed(AuthError::RateLimited);
      if (response.status_code == 503 && code == "sms_unavailable") return failed(AuthError::Unconfigured);
      if (response.status_code == 409 && code == "account_exists") return failed(AuthError::AccountExists, ResultCode::StateConflict);
      if (response.status_code == 400) {
        if (code == "code_invalid") return failed(AuthError::CodeInvalid, ResultCode::StateConflict);
        if (code == "code_expired") return failed(AuthError::CodeExpired, ResultCode::StateConflict);
        if (code == "password_weak") return failed(AuthError::PasswordWeak, ResultCode::StateConflict);
        if (code == "invalid_request") return failed(AuthError::InvalidInput, ResultCode::StateConflict);
      }
      return failed(AuthError::Network);
    }
    account::AuthReply reply;
    if (operation == AuthOperation::Configure) {
      if (!fields(root, {"api_version", "sms_enabled", "development", "password_min_length", "password_max_length"}) ||
          !root["sms_enabled"].is_boolean() || !root["development"].is_boolean() ||
          !nonnegative(root["password_min_length"], version) || version != 15 ||
          !nonnegative(root["password_max_length"], version) || version != 128) return failed(AuthError::Network);
      reply.sms_enabled = root["sms_enabled"]; reply.development = root["development"];
    } else if (operation == AuthOperation::SendCode) {
      if (!fields(root, {"api_version", "challenge_id", "expires_at", "resend_after", "development_code"}) ||
          !identifier_field(root["challenge_id"], "otp_", reply.challenge_id) ||
          !nonnegative(root["expires_at"], reply.code_expires_at) ||
          !nonnegative(root["resend_after"], reply.resend_after) || reply.resend_after != 60 ||
          !root["development_code"].is_string()) return failed(AuthError::Network);
      reply.development_code = root["development_code"];
      if (!reply.development_code.empty() && (reply.development_code.size() != 6 ||
          !std::all_of(reply.development_code.begin(), reply.development_code.end(), [](char c) { return c >= '0' && c <= '9'; })))
        return failed(AuthError::Network);
    } else if (operation != AuthOperation::Logout) {
      if (!fields(root, {"api_version", "account_id", "access_token", "refresh_token", "session_expires_at", "refresh_expires_at"}) ||
          !identifier_field(root["account_id"], "acc_", reply.account_id) || !root["access_token"].is_string() ||
          !root["refresh_token"].is_string() || !nonnegative(root["session_expires_at"], reply.session_expires_at) ||
          !nonnegative(root["refresh_expires_at"], reply.refresh_expires_at) ||
          reply.refresh_expires_at < reply.session_expires_at) return failed(AuthError::Network);
      reply.access_token = root["access_token"]; reply.refresh_token = root["refresh_token"];
      if (reply.access_token.size() != 43 || !url_safe(reply.access_token) ||
          reply.refresh_token.size() != 43 || !url_safe(reply.refresh_token)) return failed(AuthError::Network);
    } else return failed(AuthError::Network);
    return reply;
  } catch (const std::exception&) { return failed(AuthError::Network); }
}
ResultCode BackendV1Codec::encode_account(const std::string& token, net::HttpRequest& request) const {
  request = {};
  if (!origin_shape(origin_) || token.size() != 43 || !url_safe(token)) return ResultCode::StateConflict;
  try {
    request = {"GET", origin_ + "/v1/account", {{"Authorization", "Bearer " + token}, {"Accept", "application/json"}}, {}};
    return ResultCode::Ok;
  } catch (const std::exception&) { request = {}; return ResultCode::StateConflict; }
}
ResultCode BackendV1Codec::decode_account(const net::HttpResponse& response, cloud::CloudAccount& account) const {
  account = {};
  try {
    Json root;
    const auto code = parse_response(response, root);
    if (code != ResultCode::Ok) return code;
    std::int64_t version = 0;
    cloud::CloudAccount candidate;
    if (!fields(root, {"api_version", "account_id", "session_expires_at"}) ||
        !nonnegative(root["api_version"], version) || version != 1 ||
        !identifier_field(root["account_id"], "acc_", candidate.account_id) ||
        !nonnegative(root["session_expires_at"], candidate.session_expires_at)) return ResultCode::NetworkFailed;
    account = std::move(candidate);
    return ResultCode::Ok;
  } catch (const std::exception&) { return ResultCode::NetworkFailed; }
}
ResultCode BackendV1Codec::encode(const cloud::CloudIdentity& identity, const std::string& path,
                                  const std::string& cursor, net::HttpRequest& request) const {
  request = {};
  if (!origin_shape(origin_) || page_size_ == 0 || page_size_ > 100 ||
      !identifier(identity.account_id, "acc_") || identity.session_handle.size() != 43 ||
      !url_safe(identity.session_handle) || !valid_cursor(cursor)) return ResultCode::StateConflict;
  try {
    net::HttpRequest candidate;
    candidate.method = "GET";
    candidate.url = origin_ + path + "?limit=" + std::to_string(page_size_);
    if (!cursor.empty()) candidate.url += "&cursor=" + cursor;
    candidate.headers = {{"Authorization", "Bearer " + identity.session_handle}, {"Accept", "application/json"}};
    request = std::move(candidate);
    return ResultCode::Ok;
  } catch (const std::exception&) { return ResultCode::StateConflict; }
}
ResultCode BackendV1Codec::encode_spaces(const cloud::CloudIdentity& identity, const std::string& cursor,
                                         net::HttpRequest& request) const {
  return encode(identity, "/v1/spaces", cursor, request);
}
ResultCode BackendV1Codec::encode_projects(const cloud::CloudIdentity& identity, const std::string& space_id,
                                           const std::string& cursor, net::HttpRequest& request) const {
  request = {};
  if (!identifier(space_id, "sp_")) return ResultCode::StateConflict;
  return encode(identity, "/v1/spaces/" + space_id + "/projects", cursor, request);
}
ResultCode BackendV1Codec::decode_spaces(const net::HttpResponse& response, SpacesPage& page) const {
  return decode(response, page, space);
}
ResultCode BackendV1Codec::decode_projects(const net::HttpResponse& response, ProjectsPage& page) const {
  return decode(response, page, project);
}
}  // namespace pixcake::cloud_transport
