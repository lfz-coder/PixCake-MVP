#pragma once

#include <cstdint>

namespace pixcake {

// Unix 秒。回收站的 30 天按这个秒数相减，不按日历月。
class Clock {
 public:
  virtual ~Clock() = default;
  virtual std::int64_t unix_seconds() const = 0;
};

class SystemClock : public Clock {
 public:
  std::int64_t unix_seconds() const override;
};

}  // namespace pixcake
