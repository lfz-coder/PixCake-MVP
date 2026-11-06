#pragma once

#include "pixcake/account/account_session.hpp"
#include "pixcake/support/result_code.hpp"

#include <string>

namespace pixcake::account {

// 手机号 + 6 位短信验证码登录（需求第 223 行）。远端由 LoginGateway 实现：真实的短信与账号服务，
// 或测试替身。失败一律停留登录页并说明（需求第 897 行，运维清单 O-13）。
enum class VerifyVerdict { Accepted, Wrong, Expired };

struct VerifyReply {
  ResultCode code = ResultCode::Ok;  // 断网、超时为 NetworkFailed
  VerifyVerdict verdict = VerifyVerdict::Accepted;
  AccountSnapshot account;           // 通过时的账号
};

class LoginGateway {
 public:
  virtual ~LoginGateway() = default;
  virtual ResultCode send_code(const std::string& phone) = 0;
  virtual VerifyReply verify(const std::string& phone, const std::string& code) = 0;
};

enum class LoginOutcome {
  CodeSent,
  SignedIn,
  PhoneInvalid,
  NoCodeRequested,
  CodeFormatInvalid,
  CodeWrong,
  CodeExpired,
  CodeNotReceived,
  NetworkFailed,
  AccountRejected,
};

struct LoginNotice {
  LoginOutcome outcome = LoginOutcome::CodeSent;
  std::string text;
  bool stays_on_login = true;
};

class LoginFlow {
 public:
  LoginFlow(AccountSession& session, LoginGateway& gateway);

  LoginNotice request_code(const std::string& phone);
  // 用户说没收到：说明可能的原因，允许重新获取（不自动重发）。
  LoginNotice report_not_received();
  LoginNotice submit(const std::string& code);

 private:
  AccountSession& session_;
  LoginGateway& gateway_;
  std::string phone_;
};

}  // namespace pixcake::account
