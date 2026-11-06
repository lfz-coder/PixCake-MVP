#pragma once

namespace pixcake {

// 登录态（安全清单 S-25）：来自账号服务和本机会话，内核不读窗口系统去猜。
// 未登录时，项目、导出、生成、扣点、扣张的命令都返回状态冲突（契约 C-22：当前状态不允许这个命令）。
class SessionState {
 public:
  virtual ~SessionState() = default;
  virtual bool signed_in() const = 0;
};

}  // namespace pixcake
