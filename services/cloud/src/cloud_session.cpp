#include "pixcake/cloud/cloud_session.hpp"

#include "cloud_validation.hpp"

#include <algorithm>
#include <utility>

namespace pixcake::cloud {

CloudSession::CloudSession(const account::AccountSession& account) : account_(account) {}

ResultCode CloudSession::open() {
  // A new binding never retains a prior account's directory, even on failure.
  account_generation_.reset();
  replace_spaces({});
  if (!account_.signed_in() || !detail::valid_text(account_.snapshot().account_id, kMaxCloudIdBytes) ||
      !detail::valid_text(account_.snapshot().session_handle, kMaxCloudSessionBytes)) return ResultCode::StateConflict;
  account_generation_ = account_.generation();
  return ResultCode::Ok;
}

ResultCode CloudSession::close() {
  const bool was_open = account_generation_.has_value();
  account_generation_.reset();
  replace_spaces({});
  return was_open ? ResultCode::Ok : ResultCode::StateConflict;
}

void CloudSession::replace_spaces(std::vector<CloudSpace> spaces) {
  spaces_ = std::move(spaces);
  ++directory_generation_;
}

bool CloudSession::signed_in() const {
  return account_.signed_in() && account_generation_ && *account_generation_ == account_.generation();
}

CloudIdentity CloudSession::identity() const {
  if (!signed_in()) return {};
  return {account_.snapshot().account_id, account_.snapshot().session_handle};
}

std::vector<CloudSpace> CloudSession::spaces() const {
  return signed_in() ? spaces_ : std::vector<CloudSpace>{};
}

bool CloudSession::has_permission(const std::string& space_id, Permission permission) const {
  if (!signed_in()) return false;
  const auto found = std::find_if(spaces_.begin(), spaces_.end(), [&space_id](const CloudSpace& space) {
    return space.id == space_id;
  });
  return found != spaces_.end() &&
         std::find(found->permissions.begin(), found->permissions.end(), permission) != found->permissions.end();
}

bool CloudSession::can_read(const std::string& space_id) const { return has_permission(space_id, Permission::Read); }
bool CloudSession::can_write(const std::string& space_id) const { return has_permission(space_id, Permission::Write); }
bool CloudSession::can_manage(const std::string& space_id) const { return has_permission(space_id, Permission::Manage); }

}  // namespace pixcake::cloud
