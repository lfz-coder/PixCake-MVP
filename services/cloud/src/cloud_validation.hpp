#pragma once

#include "pixcake/cloud/cloud_types.hpp"

namespace pixcake::cloud::detail {

bool valid_text(const std::string& value, std::size_t max_bytes);
bool valid_spaces(const std::vector<CloudSpace>& spaces);
bool valid_projects(const std::string& space_id, const std::vector<CloudProjectSummary>& projects);
bool valid_timeout(std::chrono::milliseconds timeout);

}  // namespace pixcake::cloud::detail
