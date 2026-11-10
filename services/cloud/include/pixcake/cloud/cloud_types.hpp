#pragma once

#include "pixcake/support/result_code.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::cloud {

// Client safety bounds for a normalized directory response, not cloud storage
// quotas. Adapters must also cap wire bytes before allocation/decoding.
inline constexpr std::size_t kMaxCloudIdBytes = 512;
inline constexpr std::size_t kMaxCloudNameBytes = 1024;
inline constexpr std::size_t kMaxCloudSessionBytes = 8192;
inline constexpr std::size_t kMaxCloudSpaces = 1024;
inline constexpr std::size_t kMaxCloudProjects = 10000;
inline constexpr std::chrono::milliseconds kMaxCloudRequestTimeout{60000};

enum class SpaceKind {
  Personal,
  Team,
};

enum class Permission {
  Read,
  Write,
  Manage,
};

struct CloudSpace {
  std::string id;
  std::string name;
  SpaceKind kind = SpaceKind::Personal;
  // Independent capabilities explicitly returned by the gateway. Manage does
  // not imply Write or Read. The server remains the authorization authority.
  std::vector<Permission> permissions;
};

struct CloudProjectSummary {
  std::string id;
  std::string space_id;
  std::string name;
  std::int64_t revision = 0;
  std::int64_t updated_at = 0;
  std::int64_t size_bytes = 0;
};

struct CloudIdentity {
  std::string account_id;
  std::string session_handle; // Opaque credential: never log or persist here.
};

struct CloudAccount {
  std::string account_id;
  std::int64_t session_expires_at = 0;
};

struct CloudSpacesReply {
  ResultCode code = ResultCode::Ok;
  std::vector<CloudSpace> spaces;
};

struct CloudProjectsReply {
  ResultCode code = ResultCode::Ok;
  std::vector<CloudProjectSummary> projects;
};

}  // namespace pixcake::cloud
