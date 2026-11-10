#pragma once
#include "pixcake/support/result_code.hpp"
#include <string>

namespace pixcake::account {
// The platform stores only a refresh credential scoped to its HTTPS origin.
// A missing entry is Ok with an empty value. Never serialize to project data.
class CredentialStore {
 public:
  virtual ~CredentialStore() = default;
  virtual ResultCode read(const std::string& origin, std::string& token) = 0;
  virtual ResultCode write(const std::string& origin, const std::string& token) = 0;
  virtual ResultCode erase(const std::string& origin) = 0;
};
}  // namespace pixcake::account
