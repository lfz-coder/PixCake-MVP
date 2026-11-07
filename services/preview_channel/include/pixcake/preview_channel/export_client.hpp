#pragma once

#include "pixcake/ipc/process_messages.hpp"
#include "pixcake/preview_channel/channel.hpp"
#include "pixcake/support/result_code.hpp"

#include <sys/types.h>

#include <memory>
#include <string>

namespace pixcake::preview_channel {

// The exportd process of one shell session and its connection.  Closing the
// connection (the destructor) ends exportd's session; tasks it has not started
// stay queued in the export log.  No Qt; the shell watches fd().
class ExportClient {
 public:
  // Starts `executable --socket <short temp path> --log-dir <log_directory>
  // --data-dir <data_directory>` and connects within timeout_ms.
  static ResultCode start(const std::string& executable, const std::string& log_directory,
                          const std::string& data_directory, int timeout_ms, std::unique_ptr<ExportClient>& out,
                          std::string& diagnostic);
  ~ExportClient();
  ExportClient(const ExportClient&) = delete;
  ExportClient& operator=(const ExportClient&) = delete;

  int fd() const { return channel_.fd(); }
  pid_t pid() const { return pid_; }
  ResultCode send_start(const ipc::ExportStartMessage& message, std::string& diagnostic);
  ResultCode send_cancel(const std::string& export_id, std::string& diagnostic);
  // One progress report if waiting (timeout_ms 0) or within timeout_ms.
  // Cancelled when none; IoFailed when exportd is gone.
  ResultCode receive(ipc::ExportProgressMessage& out, int timeout_ms, std::string& diagnostic);

 private:
  ExportClient(pid_t pid, std::string socket, Channel channel);
  ResultCode send(const std::vector<std::uint8_t>& bytes, std::string& diagnostic);
  pid_t pid_ = -1;
  std::string socket_;
  Channel channel_;
};

}  // namespace pixcake::preview_channel
