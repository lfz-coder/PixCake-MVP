#include "pixcake/account/login_flow.hpp"

#include "pixcake/support/user_notice.hpp"

#include <algorithm>
#include <cctype>

namespace pixcake::account {
namespace {

bool all_digits(const std::string& text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

LoginNotice stay(LoginOutcome outcome, std::string text) {
  LoginNotice notice;
  notice.outcome = outcome;
  notice.text = std::move(text);
  return notice;
}

}  // namespace

LoginFlow::LoginFlow(AccountSession& session, LoginGateway& gateway) : session_(session), gateway_(gateway) {}

LoginNotice LoginFlow::request_code(const std::string& phone) {
  if (!all_digits(phone)) return stay(LoginOutcome::PhoneInvalid, "请输入正确的手机号");
  const ResultCode sent = gateway_.send_code(phone);
  if (sent != ResultCode::Ok) {
    return stay(sent == ResultCode::NetworkFailed ? LoginOutcome::NetworkFailed : LoginOutcome::AccountRejected,
                sent == ResultCode::NetworkFailed ? "网络连接失败，验证码没有发出，请检查网络后重试"
                                                  : "验证码没有发出：" + notice_for(NoticeScene::Login, sent).text);
  }
  phone_ = phone;
  return stay(LoginOutcome::CodeSent, "验证码已发送，请查看短信");
}

LoginNotice LoginFlow::report_not_received() {
  return stay(LoginOutcome::CodeNotReceived, "没收到验证码？请确认手机号无误、短信没有被拦截，然后重新获取");
}

LoginNotice LoginFlow::submit(const std::string& code) {
  if (phone_.empty()) return stay(LoginOutcome::NoCodeRequested, "请先获取验证码");
  if (code.size() != 6 || !all_digits(code)) return stay(LoginOutcome::CodeFormatInvalid, "请输入短信里的 6 位验证码");
  const VerifyReply reply = gateway_.verify(phone_, code);
  if (reply.code == ResultCode::NetworkFailed) {
    return stay(LoginOutcome::NetworkFailed, "网络连接失败，登录没有完成，请检查网络后重试");
  }
  if (reply.code != ResultCode::Ok) {
    return stay(LoginOutcome::AccountRejected, "登录没有完成：" + notice_for(NoticeScene::Login, reply.code).text);
  }
  if (reply.verdict == VerifyVerdict::Wrong) return stay(LoginOutcome::CodeWrong, "验证码错误，请检查后重新输入");
  if (reply.verdict == VerifyVerdict::Expired) return stay(LoginOutcome::CodeExpired, "验证码已过期，请重新获取");
  if (session_.sign_in(reply.account) != ResultCode::Ok) {
    return stay(LoginOutcome::AccountRejected, "账号信息不完整，登录没有完成，请重新获取验证码");
  }
  LoginNotice done;
  done.outcome = LoginOutcome::SignedIn;
  done.stays_on_login = false;
  return done;
}

}  // namespace pixcake::account
