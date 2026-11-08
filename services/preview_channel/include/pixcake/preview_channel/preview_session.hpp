#pragma once

#include "pixcake/command/command_gate.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/preview_channel/preview_client.hpp"

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pixcake::preview_channel {

// A renderd process started for one shell session.  The shell owns it: closing
// the connection ends renderd's session and the process exits; the destructor
// waits briefly and kills it if it does not.
class RenderdProcess {
 public:
  // Starts `executable --socket <short temp path> --log-dir <log_directory>`
  // and connects within timeout_ms.
  static ResultCode start(const std::string& executable, const std::string& log_directory, int timeout_ms,
                          std::unique_ptr<RenderdProcess>& out, PreviewClient& client, std::string& diagnostic);
  ~RenderdProcess();
  RenderdProcess(const RenderdProcess&) = delete;
  RenderdProcess& operator=(const RenderdProcess&) = delete;
  pid_t pid() const { return pid_; }
  // Waits up to timeout_ms for exit; returns the exit code, or -1 if still running.
  int wait(int timeout_ms);

 private:
  RenderdProcess(pid_t pid, std::string socket) : pid_(pid), socket_(std::move(socket)) {}
  pid_t pid_ = -1;
  std::string socket_;
  bool reaped_ = false;
};

struct PreviewSessionOptions {
  std::string renderd_executable;
  std::string log_directory;
  // Where the session's edit document is written on commit.
  std::string document_directory;
  std::uint32_t max_edge = 4000;
  int connect_timeout_ms = 10'000;
  // The edit document's image id (file <document_id>.pxedit).  Empty: derived
  // from the photo path (photos opened outside a project).
  std::string document_id;
};

// One photo being edited in the shell: its edit document (changed only through
// CommandGate, architecture 6.2), the renderd connection, and the latest
// frame.  Slider drags send previews; release commits and re-sends the
// document to renderd.  No Qt; the shell watches fd() for frames.
class PreviewSession {
 public:
  // Starts renderd, creates the document and sends the source.  Nothing is
  // rendered until the first preview()/commit().
  static ResultCode open(const std::string& photo_path, const PreviewSessionOptions& options,
                         std::unique_ptr<PreviewSession>& out, std::string& diagnostic);
  ~PreviewSession();
  PreviewSession(const PreviewSession&) = delete;
  PreviewSession& operator=(const PreviewSession&) = delete;

  // Readable when renderd sent something (a frame, or the connection ended).
  int fd() const;
  const EditDocument& document() const { return document_; }
  const std::string& photo_path() const { return photo_path_; }

  // In-memory value while dragging; renderd renders the latest one.
  ResultCode preview(const std::string& capability_id, const std::string& parameter_id,
                     const std::string& display_value, std::string& diagnostic);
  // Writes the value into the document (new revision) and shows it.
  ResultCode commit(const std::string& capability_id, const std::string& parameter_id,
                    const std::string& display_value, std::string& diagnostic);

  // Reads one frame if one is waiting (Cancelled otherwise).  Frames older
  // than one already returned are dropped (also Cancelled).
  ResultCode take_frame(PreviewFrame& out, std::string& diagnostic);
  // Blocks up to timeout_ms for the frame answering the latest request.
  ResultCode wait_latest_frame(PreviewFrame& out, int timeout_ms, std::string& diagnostic);
  std::uint64_t latest_sequence() const { return sequence_; }
  // The request the last returned frame answers; equal to latest_sequence() once the newest frame is in.
  std::uint64_t shown_sequence() const { return shown_; }
  // 历史（需求 §8.3）：本次打开以来的步骤；第一步是“原图”或“上一次保存”。
  struct HistoryEntry {
    std::string label;  // 第一步的名字；提交的步骤为空，由壳层按参数写
    std::string capability_id;
    std::string parameter_id;
    std::string display_value;
  };
  std::vector<HistoryEntry> history() const;
  std::size_t history_current() const;
  // 回到第 index 步，写盘并重新显示。撤销和重做是它的两个特例；没有前一步或后一步时返回状态冲突。
  ResultCode select_history(std::size_t index, std::string& diagnostic);
  // 粘贴效果（需求 §18）：把剪贴板里的效果搬到这张图，经命令入口提交，历史多一步“粘贴效果”。
  static constexpr char kPasteStep[] = "paste";
  ResultCode paste(const EffectClipboard& clipboard, std::string& diagnostic);
  ResultCode undo(std::string& diagnostic);
  ResultCode redo(std::string& diagnostic);
  // Called after each successful commit (e.g. to mark the photo edited).
  void on_committed(std::function<void()> callback) { committed_ = std::move(callback); }

 private:
  PreviewSession(std::string photo_path, EditDocument document, std::string document_path,
                 const PreviewSessionOptions& options);
  ResultCode send_source(std::string& diagnostic);
  // 文档整体变了（历史、粘贴）：重发文档，请 renderd 按文档现在的样子重画。优先用 prefer 里第一个有参数模式的
  // 参数当请求的锚点，都没有就用曝光。
  ResultCode redraw(const std::vector<std::pair<std::string, std::string>>& prefer, std::string& diagnostic);
  ResultCode send_request(const std::string& capability_id, const std::string& parameter_id,
                          const std::string& display_value, std::string& diagnostic);

  class Latch final : public FastExportLatch {
   public:
    bool enabled() const override { return false; }
  };

  std::string photo_path_;
  EditDocument document_;
  std::string document_path_;
  PreviewSessionOptions options_;
  PosixFileSystem files_;
  Latch latch_;
  std::unique_ptr<CommandGate> gate_;
  PreviewClient client_;
  std::unique_ptr<RenderdProcess> renderd_;
  std::uint64_t sequence_ = 0;
  std::uint64_t shown_ = 0;
  std::function<void()> committed_;
  HistoryLog history_;
  std::string last_capability_;
  std::string last_parameter_;
};

}  // namespace pixcake::preview_channel
