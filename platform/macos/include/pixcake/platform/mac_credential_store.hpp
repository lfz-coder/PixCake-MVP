#pragma once
#include "pixcake/account/credential_store.hpp"

namespace pixcake::platform {
class MacCredentialStore final : public account::CredentialStore {
 public:
  ResultCode read(const std::string& origin, std::string& token) override;
  ResultCode write(const std::string& origin, const std::string& token) override;
  ResultCode erase(const std::string& origin) override;
};
}  // namespace pixcake::platform
