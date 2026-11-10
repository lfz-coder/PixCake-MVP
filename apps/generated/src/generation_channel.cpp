#include "pixcake/generated/generation_channel.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <thread>

namespace pixcake::generated {

ResultCode GenerationServer::serve_once(preview_channel::Channel& channel, int timeout_ms,
                                        std::string& diagnostic) {
  std::vector<std::uint8_t> bytes;
  preview_channel::UniqueFd attached;
  const ResultCode received =
      channel.receive(bytes, attached, ipc::kMaxProcessMessageBytes, timeout_ms, diagnostic);
  if (received != ResultCode::Ok) return received;
  if (attached.valid()) {
    diagnostic = "generation messages cannot carry file descriptors";
    return ResultCode::StateConflict;
  }

  if (ipc::is_generation_submit(bytes)) {
    ipc::GenerationSubmitMessage submit;
    const ResultCode decoded = ipc::decode_generation_submit(bytes, submit);
    if (decoded != ResultCode::Ok) {
      diagnostic = "generated received a malformed submit message";
      return decoded;
    }
    const ipc::GenerationProgressMessage progress = service_.run(submit.task);
    std::vector<std::uint8_t> response;
    const ResultCode encoded = ipc::encode_generation_progress(progress, response);
    if (encoded != ResultCode::Ok) {
      diagnostic = "generated could not encode its progress message";
      return encoded;
    }
    return channel.send(response, -1, diagnostic);
  }

  if (ipc::is_task_cancel(bytes)) {
    ipc::TaskCancelMessage cancel;
    const ResultCode decoded = ipc::decode_task_cancel(bytes, cancel);
    if (decoded != ResultCode::Ok || cancel.kind != ipc::TaskKind::Generation) {
      diagnostic = "generated received an invalid cancel message";
      return decoded == ResultCode::Ok ? ResultCode::StateConflict : decoded;
    }
    const ResultCode cancelled = service_.cancel(cancel.task_id);
    // Cancellation is one-way.  A task that has already completed is ignored
    // here; the ledger remains the authority for the final state.
    if (cancelled != ResultCode::Ok && cancelled != ResultCode::StateConflict) return cancelled;
    diagnostic.clear();
    return ResultCode::Ok;
  }

  if (ipc::is_generation_result_confirm(bytes)) {
    ipc::GenerationResultConfirmMessage confirmation;
    const ResultCode decoded = ipc::decode_generation_result_confirm(bytes, confirmation);
    if (decoded != ResultCode::Ok) {
      diagnostic = "generated received a malformed result confirmation";
      return decoded;
    }
    const ResultCode confirmed = service_.confirm(confirmation);
    if (confirmed != ResultCode::Ok) {
      diagnostic = "generated rejected a result confirmation";
      return confirmed;
    }
    diagnostic.clear();
    return ResultCode::Ok;
  }

  diagnostic = "generated received an unknown process message";
  return ResultCode::StateConflict;
}

ResultCode GenerationServer::serve(preview_channel::Channel& channel, int timeout_ms,
                                   std::string& diagnostic) {
  struct Worker {
    bool active = false;
    std::atomic<bool> done{false};
    ipc::GenerationProgressMessage progress;
    std::thread thread;
    ~Worker() { if (thread.joinable()) thread.join(); }
  } worker;

  const auto join_worker = [&] {
    if (worker.thread.joinable()) worker.thread.join();
    worker.active = false;
    worker.done.store(false, std::memory_order_relaxed);
  };

  for (;;) {
    if (worker.active && worker.done.load(std::memory_order_acquire)) {
      if (worker.thread.joinable()) worker.thread.join();
      std::vector<std::uint8_t> response;
      const ResultCode encoded = ipc::encode_generation_progress(worker.progress, response);
      if (encoded != ResultCode::Ok) {
        diagnostic = "generated could not encode its progress message";
        worker.active = false;
        return encoded;
      }
      const ResultCode sent = channel.send(response, -1, diagnostic);
      worker.active = false;
      worker.done.store(false, std::memory_order_relaxed);
      if (sent != ResultCode::Ok) return sent;
      continue;
    }

    std::vector<std::uint8_t> bytes;
    preview_channel::UniqueFd attached;
    // A synchronous provider call must not stop the server from accepting a
    // cancellation.  The worker never touches the channel; this loop remains
    // its sole reader/writer.
    const int receive_timeout = worker.active ? 50 : timeout_ms;
    const ResultCode received =
        channel.receive(bytes, attached, ipc::kMaxProcessMessageBytes, receive_timeout, diagnostic);
    if (received == ResultCode::Cancelled && worker.active) continue;
    if (received != ResultCode::Ok) {
      join_worker();
      return received;
    }
    if (attached.valid()) {
      diagnostic = "generation messages cannot carry file descriptors";
      join_worker();
      return ResultCode::StateConflict;
    }

    if (ipc::is_generation_submit(bytes)) {
      if (worker.active) {
        diagnostic = "generated received a second submit while one is running";
        join_worker();
        return ResultCode::StateConflict;
      }
      ipc::GenerationSubmitMessage submit;
      const ResultCode decoded = ipc::decode_generation_submit(bytes, submit);
      if (decoded != ResultCode::Ok) {
        diagnostic = "generated received a malformed submit message";
        return decoded;
      }
      worker.active = true;
      worker.done.store(false, std::memory_order_relaxed);
      worker.thread = std::thread([&worker, this, task = std::move(submit.task)]() mutable {
        worker.progress = service_.run(task);
        worker.done.store(true, std::memory_order_release);
      });
      continue;
    }

    if (ipc::is_task_cancel(bytes)) {
      ipc::TaskCancelMessage cancel;
      const ResultCode decoded = ipc::decode_task_cancel(bytes, cancel);
      if (decoded != ResultCode::Ok || cancel.kind != ipc::TaskKind::Generation) {
        diagnostic = "generated received an invalid cancel message";
        return decoded == ResultCode::Ok ? ResultCode::StateConflict : decoded;
      }
      const ResultCode cancelled = service_.cancel(cancel.task_id);
      diagnostic.clear();
      // A late cancel is idempotent: a completed or already-failed task is
      // owned by the ledger and must not tear down the process session.
      if (cancelled != ResultCode::Ok && cancelled != ResultCode::StateConflict) return cancelled;
      continue;
    }

    if (ipc::is_generation_result_confirm(bytes)) {
      ipc::GenerationResultConfirmMessage confirmation;
      const ResultCode decoded = ipc::decode_generation_result_confirm(bytes, confirmation);
      if (decoded != ResultCode::Ok) {
        diagnostic = "generated received a malformed result confirmation";
        return decoded;
      }
      const ResultCode confirmed = service_.confirm(confirmation);
      if (confirmed != ResultCode::Ok) {
        diagnostic = "generated rejected a result confirmation";
        return confirmed;
      }
      diagnostic.clear();
      continue;
    }

    diagnostic = "generated received an unknown process message";
    join_worker();
    return ResultCode::StateConflict;
  }
}

ResultCode GenerationClient::connect(const std::string& socket_path, GenerationClient& out,
                                     std::string& diagnostic) {
  preview_channel::UniqueFd fd;
  const ResultCode code = preview_channel::connect_to(socket_path, fd, diagnostic);
  if (code != ResultCode::Ok) return code;
  out = GenerationClient(preview_channel::Channel{std::move(fd)});
  return ResultCode::Ok;
}

ResultCode GenerationClient::send(ResultCode encoded, const std::vector<std::uint8_t>& bytes,
                                  std::string& diagnostic) {
  if (encoded != ResultCode::Ok) {
    diagnostic = "generation message is invalid and was not sent";
    return encoded;
  }
  return channel_.send(bytes, -1, diagnostic);
}

ResultCode GenerationClient::submit(const generate::GenerationTask& task,
                                    ipc::GenerationProgressMessage& progress, int timeout_ms,
                                    std::string& diagnostic, const std::atomic<bool>* cancel_requested) {
  progress = {};
  if (cancel_requested != nullptr && cancel_requested->load(std::memory_order_acquire)) {
    diagnostic = "generation was cancelled before submission";
    return ResultCode::Cancelled;
  }
  std::vector<std::uint8_t> bytes;
  const ResultCode encoded = ipc::encode_generation_submit({ipc::kProcessProtocolVersion, task}, bytes);
  const ResultCode sent = send(encoded, bytes, diagnostic);
  if (sent != ResultCode::Ok) return sent;

  preview_channel::UniqueFd attached;
  // Only this worker touches the channel. The UI signals an atomic flag;
  // polling sends cancellation without racing another channel reader/writer.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeout_ms));
  bool cancellation_sent = false;
  ResultCode received = ResultCode::Cancelled;
  for (;;) {
    if (!cancellation_sent && cancel_requested != nullptr && cancel_requested->load(std::memory_order_acquire)) {
      const ResultCode cancelled = cancel(task.task_id, diagnostic);
      if (cancelled != ResultCode::Ok) return cancelled;
      cancellation_sent = true;
      const auto grace = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      deadline = timeout_ms < 0 ? grace : std::min(deadline, grace);
    }
    const bool bounded = timeout_ms >= 0 || cancellation_sent;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    const int wait_ms = bounded ? static_cast<int>(std::clamp<long long>(left, 0, 50)) : 50;
    received = channel_.receive(bytes, attached, ipc::kMaxProcessMessageBytes, wait_ms, diagnostic);
    if (received != ResultCode::Cancelled) break;
    if (bounded && std::chrono::steady_clock::now() >= deadline) return received;
  }
  if (received != ResultCode::Ok) return received;
  if (attached.valid()) {
    diagnostic = "generated returned a file descriptor with a progress message";
    return ResultCode::StateConflict;
  }
  if (!ipc::is_generation_progress(bytes)) {
    diagnostic = "generated returned an unexpected process message";
    return ResultCode::StateConflict;
  }
  const ResultCode decoded = ipc::decode_generation_progress(bytes, progress);
  if (decoded != ResultCode::Ok) {
    diagnostic = "generated returned a malformed progress message";
    return decoded;
  }
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode GenerationClient::cancel(const std::string& task_id, std::string& diagnostic) {
  ipc::TaskCancelMessage message;
  message.kind = ipc::TaskKind::Generation;
  message.task_id = task_id;
  std::vector<std::uint8_t> bytes;
  return send(ipc::encode_task_cancel(message, bytes), bytes, diagnostic);
}

ResultCode GenerationClient::confirm(const ipc::GenerationResultConfirmMessage& message,
                                     std::string& diagnostic) {
  std::vector<std::uint8_t> bytes;
  return send(ipc::encode_generation_result_confirm(message, bytes), bytes, diagnostic);
}

}  // namespace pixcake::generated
