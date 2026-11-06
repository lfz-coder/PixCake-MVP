#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::config {

enum class AccelerationCharge {
  LimitedFree,
  PermanentFree,
  Points,
};

struct QueuePolicy {
  bool official_free_queue = false;
  bool subscribed_non_high_compute_free_queue = true;
};

struct AccelerationRule {
  std::string tool_name;
  AccelerationCharge charge = AccelerationCharge::Points;
  std::int64_t points = 0;
};

struct PackagePrice {
  std::string package_name;
  std::int64_t annual_price = 0;
};

struct ThirdPartyToolRule {
  std::string package_name;
  std::string tool_name;
  std::int64_t points = 0;
  bool high_compute = false;
};

struct ConfigSnapshot {
  std::string client_version;
  QueuePolicy queue_policy;
  std::vector<AccelerationRule> official_acceleration;
  std::vector<PackagePrice> package_prices;
  std::vector<ThirdPartyToolRule> third_party_tools;
};

struct ConfigLoadResult {
  ResultCode code = ResultCode::Ok;
  bool used_defaults = false;
};

const ConfigSnapshot& default_snapshot();
ResultCode validate(const ConfigSnapshot& snapshot);
std::optional<AccelerationRule> find_official_acceleration(const ConfigSnapshot& snapshot,
                                                            const std::string& tool_name);
std::optional<ThirdPartyToolRule> find_third_party_tool(const ConfigSnapshot& snapshot,
                                                        const std::string& package_name,
                                                        const std::string& tool_name);

class ConfigCache {
 public:
  ConfigCache();

  ConfigLoadResult load(std::optional<ConfigSnapshot> cached);
  ResultCode replace(ConfigSnapshot snapshot);

  const ConfigSnapshot& snapshot() const;
  bool used_defaults() const;

 private:
  ConfigSnapshot snapshot_;
  bool used_defaults_ = true;
};

}  // namespace pixcake::config
