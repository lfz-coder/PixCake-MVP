#include "pixcake/config/config_store.hpp"

#include <algorithm>
#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

void test_default_policy_and_tables() {
  const auto& defaults = pixcake::config::default_snapshot();
  EXPECT(defaults.client_version == "9.3.0");
  EXPECT(!defaults.queue_policy.official_free_queue);
  EXPECT(defaults.queue_policy.subscribed_non_high_compute_free_queue);
  EXPECT(pixcake::config::validate(defaults) == pixcake::ResultCode::Ok);
  const auto fast = pixcake::config::find_official_acceleration(defaults, "AI 面部重绘极速");
  EXPECT(fast.has_value());
  EXPECT(fast->points == 5);
  const auto free = pixcake::config::find_official_acceleration(defaults, "智能消除快速");
  EXPECT(free.has_value());
  EXPECT(free->charge == pixcake::config::AccelerationCharge::PermanentFree);
  const auto third_party = pixcake::config::find_third_party_tool(defaults, "专业生图", "一句话生图");
  EXPECT(third_party.has_value());
  EXPECT(third_party->points == 100);
  EXPECT(third_party->high_compute);
  const auto package = std::find_if(defaults.package_prices.begin(), defaults.package_prices.end(),
                                    [](const auto& item) { return item.package_name == "专业生图"; });
  EXPECT(package != defaults.package_prices.end());
  EXPECT(package->annual_price == 49);
}

void test_cache_fallback_and_replacement() {
  pixcake::config::ConfigCache cache;
  EXPECT(cache.used_defaults());
  const auto missing = cache.load(std::nullopt);
  EXPECT(missing.code == pixcake::ResultCode::Ok);
  EXPECT(missing.used_defaults);
  EXPECT(cache.snapshot().client_version == "9.3.0");

  auto invalid = cache.snapshot();
  invalid.official_acceleration.front().points = 0;
  const auto rejected = cache.load(invalid);
  EXPECT(rejected.code == pixcake::ResultCode::StateConflict);
  EXPECT(cache.snapshot().official_acceleration.front().points == 5);

  auto updated = cache.snapshot();
  updated.client_version = "10.0.0";
  updated.queue_policy.official_free_queue = true;
  EXPECT(cache.replace(updated) == pixcake::ResultCode::Ok);
  EXPECT(!cache.used_defaults());
  EXPECT(cache.snapshot().client_version == "10.0.0");
  EXPECT(cache.snapshot().queue_policy.official_free_queue);
}

void test_two_policy_flags_are_not_merged() {
  auto value = pixcake::config::default_snapshot();
  value.queue_policy.official_free_queue = true;
  value.queue_policy.subscribed_non_high_compute_free_queue = false;
  EXPECT(pixcake::config::validate(value) == pixcake::ResultCode::Ok);
  EXPECT(value.queue_policy.official_free_queue != value.queue_policy.subscribed_non_high_compute_free_queue);
}

}  // namespace

int main() {
  test_default_policy_and_tables();
  test_cache_fallback_and_replacement();
  test_two_policy_flags_are_not_merged();
  return g_failures == 0 ? 0 : 1;
}
