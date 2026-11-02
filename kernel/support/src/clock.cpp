#include "pixcake/support/clock.hpp"

#include <chrono>

namespace pixcake {

std::int64_t SystemClock::unix_seconds() const {
  const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
}

}  // namespace pixcake
