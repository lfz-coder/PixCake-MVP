#pragma once

namespace pixcake {

// 极速导出不因为队列空闲而自己关闭。这里没有「队列已空」的操作，开关只由持有者显式关掉。
class FastExportLatch {
 public:
  virtual ~FastExportLatch() = default;
  virtual bool enabled() const = 0;
};

}  // namespace pixcake
