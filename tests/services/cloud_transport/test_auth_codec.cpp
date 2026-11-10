#include "pixcake/cloud_transport/backend_v1_codec.hpp"
#include <cstdio>

namespace {
using namespace pixcake;
int failures = 0;
#define EXPECT(value) do { if (!(value)) { std::fprintf(stderr, "FAIL auth codec line %d\n", __LINE__); ++failures; } } while (false)
net::HttpResponse response(std::string value, int status = 200) {
  return {ResultCode::Ok, status, {{"Content-Type", "application/json"}}, {value.begin(), value.end()}};
}
void run() {
  using account::AuthOperation;
  using account::AuthError;
  cloud_transport::BackendV1Codec codec("https://cloud.invalid");
  net::HttpRequest wire;
  account::AuthRequest request; request.operation = AuthOperation::Register; request.phone = "13800000001";
  request.challenge_id = "otp_" + std::string(32, 'c'); request.code = "012345"; request.password = "Password with \"quotes\"";
  EXPECT(codec.encode_auth(request, wire) == ResultCode::Ok && wire.method == "POST" && !wire.follow_redirects);
  EXPECT(wire.url == "https://cloud.invalid/v1/auth/register" && wire.response_limits.body_bytes == 16384);
  const std::string body(wire.body.begin(), wire.body.end());
  EXPECT(body.find("\\\"quotes\\\"") != std::string::npos && wire.url.find(request.phone) == std::string::npos);
  request.phone[2] = '\n';
  EXPECT(codec.encode_auth(request, wire) == ResultCode::StateConflict && wire.url.empty());
  const std::string payload = "{\"api_version\":1,\"account_id\":\"acc_" + std::string(32, 'a') +
    "\",\"access_token\":\"" + std::string(43, 'A') + "\",\"refresh_token\":\"" + std::string(43, 'R') +
    "\",\"session_expires_at\":2000000000,\"refresh_expires_at\":2000100000}";
  const auto decoded = codec.decode_auth(AuthOperation::Register, response(payload));
  EXPECT(decoded.code == ResultCode::Ok && decoded.access_token.size() == 43);
  auto duplicate = payload; duplicate.insert(1, "\"api_version\":1,");
  EXPECT(codec.decode_auth(AuthOperation::Register, response(duplicate)).code == ResultCode::NetworkFailed);
  auto bad = payload; bad.replace(bad.find("2000000000"), 10, "true");
  EXPECT(codec.decode_auth(AuthOperation::Register, response(bad)).access_token.empty());
  EXPECT(codec.decode_auth(AuthOperation::Register, response(payload, 302)).error == AuthError::Network);
  auto error = response("{\"api_version\":1,\"error\":{\"code\":\"code_expired\",\"message\":\"ignored\"}}", 400);
  EXPECT(codec.decode_auth(AuthOperation::SmsLogin, error).error == AuthError::CodeExpired);
  error.status_code = 200;
  EXPECT(codec.decode_auth(AuthOperation::SmsLogin, error).code == ResultCode::NetworkFailed);
  EXPECT(codec.decode_auth(AuthOperation::Logout, {ResultCode::Ok, 204, {}, {}}).code == ResultCode::Ok);
  EXPECT(codec.decode_auth(AuthOperation::Configure, response(std::string(16385, ' '))).error == AuthError::Network);
}
}
int main() { run(); return failures ? 1 : 0; }
