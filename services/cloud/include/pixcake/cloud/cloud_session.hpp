#pragma once

#include "pixcake/account/account_session.hpp"
#include "pixcake/cloud/cloud_types.hpp"

#include <optional>

namespace pixcake::cloud {

// Serialized owner: use on the same execution context as AccountSession; not
// thread-safe. The account must outlive this session. No credential is cached.
// Authentication changes immediately hide all old directory data/permissions.
class CloudSession final {
 public:
  explicit CloudSession(const account::AccountSession& account);
  CloudSession(const CloudSession&) = delete;
  CloudSession& operator=(const CloudSession&) = delete;

  // Bind to the current live login, clearing any prior space directory.
  ResultCode open();
  ResultCode close(); // Does not sign out AccountSession or affect local projects.
  bool signed_in() const;
  std::vector<CloudSpace> spaces() const;
  bool can_read(const std::string& space_id) const;
  bool can_write(const std::string& space_id) const;
  bool can_manage(const std::string& space_id) const;

 private:
  friend class CloudService;
  CloudIdentity identity() const;
  bool has_permission(const std::string& space_id, Permission permission) const;
  void replace_spaces(std::vector<CloudSpace> spaces);

  const account::AccountSession& account_;
  std::optional<std::uint64_t> account_generation_;
  std::vector<CloudSpace> spaces_;
  std::uint64_t directory_generation_ = 0;
};

}  // namespace pixcake::cloud
