#include "pixcake/account/account_session.hpp"

#include <algorithm>
#include <utility>

namespace pixcake::account {
namespace {

constexpr std::size_t kToolbarLimit = 5;
constexpr std::size_t kFavoriteColorLimit = 6;

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.find('\0') == std::string::npos;
}

bool valid_method(LoginMethod method) {
  switch (method) {
    case LoginMethod::Sms:
    case LoginMethod::Wechat:
    case LoginMethod::Business:
      return true;
  }
  return false;
}

bool duplicate(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

const char* login_method_name(LoginMethod method) {
  switch (method) {
    case LoginMethod::Sms:
      return "手机号短信";
    case LoginMethod::Wechat:
      return "微信";
    case LoginMethod::Business:
      return "商家账号";
  }
  return "";
}

const char* session_state_name(SessionState state) {
  switch (state) {
    case SessionState::SignedOut:
      return "未登录";
    case SessionState::SignedIn:
      return "已登录";
  }
  return "";
}

ResultCode validate(const AccountSnapshot& snapshot) {
  if (!valid_identifier(snapshot.account_id) || !valid_identifier(snapshot.session_handle) ||
      !valid_method(snapshot.login_method) || snapshot.remaining_sheets < 0 || snapshot.creative_points < 0 ||
      snapshot.toolbar_capability_ids.size() > kToolbarLimit || snapshot.favorite_colors.size() > kFavoriteColorLimit) {
    return ResultCode::StateConflict;
  }
  if (snapshot.merchant_id.has_value() && !valid_identifier(*snapshot.merchant_id)) {
    return ResultCode::StateConflict;
  }
  std::vector<std::string> subscription_ids;
  for (const auto& subscription : snapshot.subscriptions) {
    if (!valid_identifier(subscription.package_id) || subscription.expires_at < 0 ||
        duplicate(subscription_ids, subscription.package_id)) {
      return ResultCode::StateConflict;
    }
    subscription_ids.push_back(subscription.package_id);
  }
  std::vector<std::string> toolbar_ids;
  for (const auto& capability_id : snapshot.toolbar_capability_ids) {
    if (!valid_identifier(capability_id) || duplicate(toolbar_ids, capability_id)) {
      return ResultCode::StateConflict;
    }
    toolbar_ids.push_back(capability_id);
  }
  std::vector<std::string> colors;
  for (const auto& color_id : snapshot.favorite_colors) {
    if (!valid_identifier(color_id) || duplicate(colors, color_id)) {
      return ResultCode::StateConflict;
    }
    colors.push_back(color_id);
  }
  return ResultCode::Ok;
}

AccountSession::AccountSession() = default;

CacheLoadResult AccountSession::load(std::optional<AccountSnapshot> cached) {
  if (!cached.has_value()) {
    snapshot_ = {};
    state_ = SessionState::SignedOut;
    return {ResultCode::Ok, false};
  }
  if (validate(*cached) != ResultCode::Ok) {
    return {ResultCode::StateConflict, state_ == SessionState::SignedIn};
  }
  snapshot_ = std::move(*cached);
  state_ = SessionState::SignedIn;
  return {ResultCode::Ok, true};
}

ResultCode AccountSession::sign_in(AccountSnapshot snapshot) {
  if (validate(snapshot) != ResultCode::Ok) {
    return ResultCode::StateConflict;
  }
  snapshot_ = std::move(snapshot);
  state_ = SessionState::SignedIn;
  return ResultCode::Ok;
}

ResultCode AccountSession::sign_out() {
  if (state_ != SessionState::SignedIn) {
    return ResultCode::StateConflict;
  }
  snapshot_ = {};
  state_ = SessionState::SignedOut;
  return ResultCode::Ok;
}

ResultCode AccountSession::add_toolbar_capability(std::string capability_id) {
  if (state_ != SessionState::SignedIn || !valid_identifier(capability_id) ||
      duplicate(snapshot_.toolbar_capability_ids, capability_id) ||
      snapshot_.toolbar_capability_ids.size() >= kToolbarLimit) {
    return ResultCode::StateConflict;
  }
  snapshot_.toolbar_capability_ids.push_back(std::move(capability_id));
  return ResultCode::Ok;
}

ResultCode AccountSession::remove_toolbar_capability(const std::string& capability_id) {
  if (state_ != SessionState::SignedIn) {
    return ResultCode::StateConflict;
  }
  const auto found = std::find(snapshot_.toolbar_capability_ids.begin(), snapshot_.toolbar_capability_ids.end(),
                               capability_id);
  if (found == snapshot_.toolbar_capability_ids.end()) {
    return ResultCode::StateConflict;
  }
  snapshot_.toolbar_capability_ids.erase(found);
  return ResultCode::Ok;
}

ResultCode AccountSession::add_favorite_color(std::string color_id) {
  if (state_ != SessionState::SignedIn || !valid_identifier(color_id) ||
      duplicate(snapshot_.favorite_colors, color_id) || snapshot_.favorite_colors.size() >= kFavoriteColorLimit) {
    return ResultCode::StateConflict;
  }
  snapshot_.favorite_colors.push_back(std::move(color_id));
  return ResultCode::Ok;
}

ResultCode AccountSession::remove_favorite_color(const std::string& color_id) {
  if (state_ != SessionState::SignedIn) {
    return ResultCode::StateConflict;
  }
  const auto found = std::find(snapshot_.favorite_colors.begin(), snapshot_.favorite_colors.end(), color_id);
  if (found == snapshot_.favorite_colors.end()) {
    return ResultCode::StateConflict;
  }
  snapshot_.favorite_colors.erase(found);
  return ResultCode::Ok;
}

SessionState AccountSession::state() const { return state_; }

bool AccountSession::signed_in() const { return state_ == SessionState::SignedIn; }

const AccountSnapshot& AccountSession::snapshot() const { return snapshot_; }

}  // namespace pixcake::account
