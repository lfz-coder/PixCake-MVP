#include "pixcake/preview_channel/preview_session.hpp"

#include "pixcake/capability/capability_registry.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>

extern char** environ;

namespace pixcake::preview_channel {
namespace {

// Unix socket paths are limited to 104 bytes: use the per-user temporary
// directory when it is short enough, /tmp otherwise.
std::string socket_path() {
  static std::atomic<unsigned> counter{0};
  const char* tmp = std::getenv("TMPDIR");
  std::string directory = tmp != nullptr && *tmp != '\0' ? tmp : "/tmp";
  if (directory.back() == '/') directory.pop_back();
  char name[64];
  std::snprintf(name, sizeof(name), "/pxc-renderd-%d-%u.sock", static_cast<int>(::getpid()), counter++);
  if (directory.size() + std::char_traits<char>::length(name) >= 100) directory = "/tmp";
  return directory + name;
}

// Stable per photo, so reopening a photo finds its document again.
std::string image_id_for(const std::string& photo_path) {
  char text[32];
  std::snprintf(text, sizeof(text), "photo-%016zx", std::hash<std::string>{}(photo_path));
  return text;
}

}  // namespace

ResultCode RenderdProcess::start(const std::string& executable, const std::string& log_directory, int timeout_ms,
                                 std::unique_ptr<RenderdProcess>& out, PreviewClient& client,
                                 std::string& diagnostic) {
  out.reset();
  if (::access(executable.c_str(), X_OK) != 0) {
    diagnostic = "renderd executable not found: " + executable;
    return ResultCode::SourceMissing;
  }
  const std::string socket = socket_path();
  std::vector<std::string> args = {executable, "--socket", socket, "--log-dir", log_directory};
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  pid_t pid = -1;
  if (posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
    diagnostic = "renderd could not be started: " + executable;
    return ResultCode::IoFailed;
  }
  std::unique_ptr<RenderdProcess> process(new RenderdProcess(pid, socket));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    if (PreviewClient::connect(socket, client, diagnostic) == ResultCode::Ok) break;
    if (process->wait(0) >= 0) {
      diagnostic = "renderd exited before accepting the preview connection";
      return ResultCode::IoFailed;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      diagnostic = "renderd did not open its preview socket in time";
      return ResultCode::Cancelled;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  out = std::move(process);
  diagnostic.clear();
  return ResultCode::Ok;
}

int RenderdProcess::wait(int timeout_ms) {
  if (reaped_) return 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    int status = 0;
    const pid_t done = ::waitpid(pid_, &status, WNOHANG);
    if (done == pid_) {
      reaped_ = true;
      return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    }
    if (done < 0) {
      reaped_ = true;
      return 0;
    }
    if (std::chrono::steady_clock::now() >= deadline) return -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

RenderdProcess::~RenderdProcess() {
  // The connection is closed by now; renderd exits on its own.
  if (wait(2000) < 0) {
    ::kill(pid_, SIGKILL);
    int status = 0;
    ::waitpid(pid_, &status, 0);
  }
  ::unlink(socket_.c_str());
}

PreviewSession::PreviewSession(std::string photo_path, EditDocument document, std::string document_path,
                               const PreviewSessionOptions& options)
    : photo_path_(std::move(photo_path)),
      document_(std::move(document)),
      document_path_(std::move(document_path)),
      options_(options) {
  gate_ = std::make_unique<CommandGate>(document_, latch_, files_, document_path_, phase_one_version_limits(), nullptr);
}

PreviewSession::~PreviewSession() {
  client_ = PreviewClient{};  // renderd sees the connection end and exits
  renderd_.reset();
}

ResultCode PreviewSession::open(const std::string& photo_path, const PreviewSessionOptions& options,
                                std::unique_ptr<PreviewSession>& out, std::string& diagnostic) {
  out.reset();
  struct stat info {};
  if (photo_path.empty() || ::stat(photo_path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    diagnostic = "photo does not exist: " + photo_path;
    return ResultCode::SourceMissing;
  }
  if (options.document_directory.empty()) {
    diagnostic = "no directory for the edit document";
    return ResultCode::StateConflict;
  }
  PosixFileSystem files;
  ::mkdir(options.document_directory.c_str(), 0700);  // parents must exist; an existing directory is fine
  const std::string id = options.document_id.empty() ? image_id_for(photo_path) : options.document_id;
  const std::string document_path = options.document_directory + "/" + id + ".pxedit";
  std::optional<EditDocument> document = EditDocument::create(id);
  if (files.exists(document_path)) {
    // Reopening a photo resumes its committed edits.
    const LoadResult loaded = EditDocument::load(*document, files, document_path, phase_one_version_limits());
    if (loaded.code && *loaded.code != ResultCode::Ok) {
      diagnostic = "edit document cannot be read: " + document_path;
      return *loaded.code;
    }
  }
  std::unique_ptr<PreviewSession> session(new PreviewSession(photo_path, std::move(*document), document_path, options));
  // 历史：这次打开的第一步。已有提交的图折叠成“上一次保存”，没有的是“原图”（需求 §8.3）。
  if (session->document_.revision() > 0) {
    session->history_.reopen_folded(session->document_.image_id(), session->document_);
  } else {
    session->history_.begin_original(session->document_.image_id(), session->document_);
  }
  ResultCode code = RenderdProcess::start(options.renderd_executable, options.log_directory,
                                          options.connect_timeout_ms, session->renderd_, session->client_, diagnostic);
  if (code != ResultCode::Ok) return code;
  code = session->send_source(diagnostic);
  if (code != ResultCode::Ok) return code;
  out = std::move(session);
  return ResultCode::Ok;
}

int PreviewSession::fd() const { return client_.fd(); }

ResultCode PreviewSession::send_source(std::string& diagnostic) {
  ipc::PreviewSourceMessage source;
  source.image_id = document_.image_id();
  source.editing_instance_id = document_.editing_instance_id();
  source.document_revision = document_.revision();
  source.source_path = photo_path_;
  source.document = document_.encode();
  return client_.send_source(source, diagnostic);
}

ResultCode PreviewSession::send_request(const std::string& capability_id, const std::string& parameter_id,
                                        const std::string& display_value, std::string& diagnostic) {
  ipc::PreviewRequestMessage message;
  message.client_sequence = ++sequence_;
  message.request.image_id = document_.image_id();
  message.request.editing_instance_id = document_.editing_instance_id();
  message.request.base_revision = document_.revision();
  message.request.capability_id = capability_id;
  message.request.parameter_id = parameter_id;
  message.request.display_value = display_value;
  message.request.max_edge = options_.max_edge;
  return client_.send_request(message, diagnostic);
}

ResultCode PreviewSession::preview(const std::string& capability_id, const std::string& parameter_id,
                                   const std::string& display_value, std::string& diagnostic) {
  UserCommand command;
  command.name = "preview";
  command.target_id = document_.image_id();
  command.revision = document_.revision();
  command.capability_id = capability_id;
  command.parameter_id = parameter_id;
  command.display_value = display_value;
  const CommandResult result = gate_->submit(command);
  if (result.code != ResultCode::Ok) {
    diagnostic = std::string("preview refused: ") + result_code_label(result.code);
    return result.code;
  }
  return send_request(capability_id, parameter_id, display_value, diagnostic);
}

ResultCode PreviewSession::commit(const std::string& capability_id, const std::string& parameter_id,
                                  const std::string& display_value, std::string& diagnostic) {
  if (const ResultCode code = preview(capability_id, parameter_id, display_value, diagnostic); code != ResultCode::Ok) {
    return code;
  }
  UserCommand command;
  command.name = "commit";
  command.target_id = document_.image_id();
  command.revision = document_.revision();
  command.capability_id = capability_id;
  command.parameter_id = parameter_id;
  const CommandResult result = gate_->submit(command);
  if (result.code != ResultCode::Ok) {
    diagnostic = std::string("commit refused: ") + result_code_label(result.code);
    return result.code;
  }
  history_.record_committed(document_.image_id(), document_, capability_id, parameter_id, display_value, false);
  last_capability_ = capability_id;
  last_parameter_ = parameter_id;
  if (committed_) committed_();
  // New revision: renderd needs the committed document before the next request.
  if (const ResultCode code = send_source(diagnostic); code != ResultCode::Ok) return code;
  return send_request(capability_id, parameter_id, display_value, diagnostic);
}

namespace {

// 参数的默认值写成显示值（小数位由步进推出，与滑杆一致）；枚举参数取默认选项。
std::string default_display(const std::string& capability, const std::string& parameter) {
  const ParameterMode* mode = find_parameter_mode(capability, parameter);
  if (mode == nullptr) return {};
  if (!mode->choices.empty()) return mode->default_choice;
  int decimals = 0;
  for (double step = mode->step; step > 0 && step < 1 - 1e-9 && decimals < 6; step *= 10) ++decimals;
  char text[64];
  std::snprintf(text, sizeof(text), "%.*f", decimals, mode->default_value);
  return text;
}

}  // namespace

std::vector<PreviewSession::HistoryEntry> PreviewSession::history() const {
  std::vector<HistoryEntry> out;
  const auto* steps = history_.steps(document_.editing_instance_id());
  if (steps == nullptr) steps = history_.steps(document_.image_id());
  if (steps == nullptr) return out;
  for (const auto& step : *steps) {
    out.push_back({step.label, step.capability_id, step.parameter_id, step.display_value});
  }
  return out;
}

std::size_t PreviewSession::history_current() const {
  auto current = history_.current_index(document_.editing_instance_id());
  if (!current) current = history_.current_index(document_.image_id());
  return current.value_or(0);
}

ResultCode PreviewSession::select_history(std::size_t index, std::string& diagnostic) {
  const auto steps = history();
  const std::size_t from = history_current();
  if (index >= steps.size() || index == from) {
    diagnostic = "no such history step";
    return ResultCode::StateConflict;
  }
  const CommandResult result = gate_->select_history(history_, index, document_.revision());
  if (result.code != ResultCode::Ok) {
    diagnostic = std::string("history refused: ") + result_code_label(result.code);
    return result.code;
  }
  if (committed_) committed_();
  return redraw({{steps[index].capability_id, steps[index].parameter_id},
                 {steps[from].capability_id, steps[from].parameter_id},
                 {last_capability_, last_parameter_}},
                diagnostic);
}

ResultCode PreviewSession::redraw(const std::vector<std::pair<std::string, std::string>>& prefer,
                                  std::string& diagnostic) {
  if (const ResultCode code = send_source(diagnostic); code != ResultCode::Ok) return code;
  std::string capability = "color.global";
  std::string parameter = "exposure";
  for (const auto& [candidate, name] : prefer) {
    if (!candidate.empty() && find_parameter_mode(candidate, name) != nullptr) {
      capability = candidate;
      parameter = name;
      break;
    }
  }
  const auto value = document_.effective_parameter(capability, parameter);
  return send_request(capability, parameter, value ? *value : default_display(capability, parameter), diagnostic);
}

ResultCode PreviewSession::apply_preset(PresetLibrary& library, const Preset& preset, std::string& diagnostic) {
  const CommandResult result = gate_->apply_preset(library, preset, document_.revision());
  if (result.code != ResultCode::Ok) {
    diagnostic = std::string("preset refused: ") + result_code_label(result.code);
    return result.code;
  }
  history_.record_committed(document_.image_id(), document_, kPresetStep, "", preset.name, false);
  if (committed_) committed_();
  return redraw({{last_capability_, last_parameter_}}, diagnostic);
}

ResultCode PreviewSession::paste(const EffectClipboard& clipboard, std::string& diagnostic) {
  const CommandResult result = gate_->paste(clipboard, document_.revision());
  if (result.code != ResultCode::Ok) {
    diagnostic = std::string("paste refused: ") + result_code_label(result.code);
    return result.code;
  }
  history_.record_committed(document_.image_id(), document_, kPasteStep, "", "", false);
  if (committed_) committed_();
  return redraw({{last_capability_, last_parameter_}}, diagnostic);
}

ResultCode PreviewSession::undo(std::string& diagnostic) {
  const std::size_t current = history_current();
  if (current == 0) {
    diagnostic = "nothing to undo";
    return ResultCode::StateConflict;
  }
  return select_history(current - 1, diagnostic);
}

ResultCode PreviewSession::redo(std::string& diagnostic) {
  const std::size_t current = history_current();
  if (current + 1 >= history().size()) {
    diagnostic = "nothing to redo";
    return ResultCode::StateConflict;
  }
  return select_history(current + 1, diagnostic);
}

ResultCode PreviewSession::take_frame(PreviewFrame& out, std::string& diagnostic) {
  out = {};
  PreviewFrame frame;
  const ResultCode code = client_.receive(frame, 0, diagnostic);
  if (code != ResultCode::Ok) return code;
  if (frame.result.client_sequence <= shown_) {
    diagnostic = "an older frame arrived after a newer one";
    return ResultCode::Cancelled;
  }
  shown_ = frame.result.client_sequence;
  out = std::move(frame);
  return ResultCode::Ok;
}

ResultCode PreviewSession::wait_latest_frame(PreviewFrame& out, int timeout_ms, std::string& diagnostic) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    PreviewFrame frame;
    const ResultCode code = client_.receive(frame, static_cast<int>(std::max<long long>(0, left.count())), diagnostic);
    if (code != ResultCode::Ok) return code;
    if (frame.result.client_sequence > shown_) shown_ = frame.result.client_sequence;
    if (frame.result.client_sequence == sequence_) {
      out = std::move(frame);
      return ResultCode::Ok;
    }
  }
}

}  // namespace pixcake::preview_channel
