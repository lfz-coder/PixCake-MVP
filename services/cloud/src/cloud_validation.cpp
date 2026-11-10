#include "cloud_validation.hpp"

#include <algorithm>
#include <set>
#include <string_view>

namespace pixcake::cloud::detail {

bool valid_text(const std::string& value, std::size_t max_bytes) {
  if (value.empty() || value.size() > max_bytes) return false;
  // Keep non-ASCII names/opaque IDs; reject ASCII controls including NUL.
  return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

bool valid_spaces(const std::vector<CloudSpace>& spaces) {
  if (spaces.size() > kMaxCloudSpaces) return false;
  std::set<std::string_view> ids;
  for (const auto& space : spaces) {
    if (!valid_text(space.id, kMaxCloudIdBytes) || !valid_text(space.name, kMaxCloudNameBytes) ||
        (space.kind != SpaceKind::Personal && space.kind != SpaceKind::Team) || !ids.insert(space.id).second ||
        space.permissions.size() > 3) return false;
    unsigned seen = 0;
    for (auto permission : space.permissions) {
      unsigned bit = 0;
      switch (permission) {
        case Permission::Read: bit = 1; break;
        case Permission::Write: bit = 2; break;
        case Permission::Manage: bit = 4; break;
        default: return false;
      }
      if ((seen & bit) != 0) return false;
      seen |= bit;
    }
  }
  return true;
}

bool valid_projects(const std::string& space_id, const std::vector<CloudProjectSummary>& projects) {
  if (projects.size() > kMaxCloudProjects) return false;
  std::set<std::string_view> ids;
  for (const auto& project : projects) {
    if (!valid_text(project.id, kMaxCloudIdBytes) || project.space_id != space_id ||
        !valid_text(project.name, kMaxCloudNameBytes) || project.revision < 0 || project.updated_at < 0 ||
        project.size_bytes < 0 || !ids.insert(project.id).second) return false;
  }
  return true;
}

bool valid_timeout(std::chrono::milliseconds timeout) {
  return timeout.count() > 0 && timeout <= kMaxCloudRequestTimeout;
}

}  // namespace pixcake::cloud::detail
