#pragma once

#include "pixcake/support/result_code.hpp"

#include <memory>
#include <optional>
#include <string>

namespace pixcake::instance {

// 单实例（需求 §3.2，架构第 576 行）：用 flock 锁住 <directory>/instance.lock 的进程是主实例，
// 它在 <directory>/instance.sock 上监听本机套接字。后启动的进程拿不到锁，就通过套接字通知主实例，
// 自己退出，不打开任何数据库。锁随进程退出释放，主实例被强杀后下一次启动能接手。
// 这里不依赖窗口系统；壳层把 listen_fd() 接到自己的事件循环。
class PrimaryInstance {
 public:
  ~PrimaryInstance();
  PrimaryInstance(const PrimaryInstance&) = delete;
  PrimaryInstance& operator=(const PrimaryInstance&) = delete;

  int listen_fd() const { return listen_fd_; }
  // 取一条已到达的消息（一行文本，不含换行，最长 4096 字节）。没有连接时返回 nullopt，不阻塞。
  // 不是同一用户发来的连接直接关闭。
  std::optional<std::string> take_message();

 private:
  friend struct InstanceAccess;
  PrimaryInstance(int lock_fd, int listen_fd, std::string socket_path);

  int lock_fd_;
  int listen_fd_;
  std::string socket_path_;
};

struct InstanceClaim {
  ResultCode code = ResultCode::Ok;          // 读写错误时非 Ok，这时 primary 为空、another_running 为假
  bool another_running = false;              // 锁已被别的进程持有
  std::unique_ptr<PrimaryInstance> primary;  // 成为主实例时非空
};

// directory 不存在时以 0700 建立。套接字路径超过系统上限时返回 IoFailed，不建任何文件。
InstanceClaim claim_instance(const std::string& directory);

// 把一行消息交给主实例。主实例刚拿到锁、还没开始监听时，最多重试 2 秒。
ResultCode notify_primary(const std::string& directory, const std::string& message);

// $HOME/Library/Application Support/PixCake（需求 §3.8 的应用支持目录）。拿不到 HOME 时为空。
std::string default_instance_directory();

}  // namespace pixcake::instance
