// The shell's preview session against a real renderd process: drag (preview),
// release (commit through CommandGate), a second parameter keeps the first,
// reopening resumes the committed document, renderd exits with the session.
#include "pixcake/preview_channel/preview_session.hpp"

#include <signal.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace pixcake;
using namespace pixcake::preview_channel;

namespace {

void write_grey_bmp(const std::string& path, int w, int h, unsigned char grey) {
  const int row = (w * 3 + 3) / 4 * 4;
  const int size = 54 + row * h;
  std::vector<unsigned char> b(static_cast<std::size_t>(size), 0);
  auto put = [&](int at, int value, int bytes) {
    for (int i = 0; i < bytes; ++i) b[static_cast<std::size_t>(at + i)] = static_cast<unsigned char>(value >> (8 * i));
  };
  b[0] = 'B'; b[1] = 'M';
  put(2, size, 4); put(10, 54, 4); put(14, 40, 4); put(18, w, 4); put(22, h, 4);
  put(26, 1, 2); put(28, 24, 2); put(34, row * h, 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w * 3; ++x) b[static_cast<std::size_t>(54 + y * row + x)] = grey;
  }
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), size);
}

bool alive(pid_t pid) { return ::kill(pid, 0) == 0; }

}  // namespace

int main() {
  const std::string base = std::string(PIXCAKE_TEST_WORK_DIR) + "/preview-session-" + std::to_string(::getpid());
  const std::string photo = base + ".bmp";
  write_grey_bmp(photo, 40, 20, 90);
  PreviewSessionOptions options;
  options.renderd_executable = PIXCAKE_RENDERD_EXE;
  options.log_directory = base + "-logs";
  options.document_directory = base + "-docs";
  std::string diagnostic;
  PreviewFrame frame;

  std::uint64_t revision = 0;
  int bright = 0;
  {
    std::unique_ptr<PreviewSession> session;
    assert(PreviewSession::open(photo, options, session, diagnostic) == ResultCode::Ok);
    assert(session->fd() >= 0);

    // Dragging: several previews, the frame for the last one arrives.
    assert(session->preview("color.global", "exposure", "0.50", diagnostic) == ResultCode::Ok);
    assert(session->preview("color.global", "exposure", "1.00", diagnostic) == ResultCode::Ok);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok);
    assert(frame.result.code == ResultCode::Ok && frame.result.header.width == 40);
    bright = frame.rgba[0];
    assert(bright > 120);

    // Release: committed through CommandGate, new revision on disk.
    // Display values use the parameter mode's format (step 0.05 -> two decimals).
    assert(session->commit("color.global", "exposure", "1.00", diagnostic) == ResultCode::Ok);
    revision = session->document().revision();
    assert(revision > 0);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok);
    assert(frame.result.code == ResultCode::Ok && frame.rgba[0] == bright);

    // Another parameter at its neutral value keeps the committed exposure.
    assert(session->preview("color.global", "temperature", "0", diagnostic) == ResultCode::Ok);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok);
    assert(frame.rgba[0] == bright);

    // A value outside the parameter mode is refused at commit; the document keeps its revision.
    assert(session->commit("color.global", "exposure", "99.00", diagnostic) == ResultCode::StateConflict);
    assert(session->document().revision() == revision);
  }

  // Reopen: the committed document is loaded, exposure still applied.
  {
    std::unique_ptr<PreviewSession> session;
    assert(PreviewSession::open(photo, options, session, diagnostic) == ResultCode::Ok);
    assert(session->document().revision() == revision);
    // History after reopening: the earlier steps are folded into one.
    assert(session->history().size() == 1 && session->history()[0].label == kHistoryFoldedLabel);
    assert(session->undo(diagnostic) == ResultCode::StateConflict && session->redo(diagnostic) == ResultCode::StateConflict);
    assert(session->preview("color.global", "temperature", "0", diagnostic) == ResultCode::Ok);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok);
    assert(frame.result.code == ResultCode::Ok && frame.rgba[0] == bright);
  }

  // History: undo, redo, jumping to a step, and a new edit after undo (requirement 8.3).
  {
    const std::string photo2 = base + "-h.bmp";
    write_grey_bmp(photo2, 40, 20, 90);
    std::unique_ptr<PreviewSession> session;
    assert(PreviewSession::open(photo2, options, session, diagnostic) == ResultCode::Ok);
    assert(session->history().size() == 1 && session->history()[0].label == kHistoryOriginalLabel);
    assert(session->undo(diagnostic) == ResultCode::StateConflict);
    int commits = 0;
    session->on_committed([&commits] { ++commits; });
    assert(session->commit("color.global", "exposure", "1.00", diagnostic) == ResultCode::Ok);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok);
    const int one = frame.rgba[0];
    assert(session->commit("color.global", "exposure", "1.50", diagnostic) == ResultCode::Ok);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok);
    const int two = frame.rgba[0];
    assert(two > one && one > 90 && commits == 2);
    assert(session->history().size() == 3 && session->history_current() == 2);
    assert(session->history()[2].capability_id == "color.global" && session->history()[2].display_value == "1.50");
    const std::uint64_t last_revision = session->document().revision();

    // Undo shows the first edit, redo the second; the revision never goes back.
    assert(session->undo(diagnostic) == ResultCode::Ok && session->history_current() == 1);
    assert(session->document().revision() == last_revision);
    assert(session->document().effective_parameter("color.global", "exposure") == std::optional<std::string>("1.00"));
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok && frame.rgba[0] == one);
    assert(session->redo(diagnostic) == ResultCode::Ok && session->history_current() == 2);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok && frame.rgba[0] == two);
    assert(session->redo(diagnostic) == ResultCode::StateConflict);

    // The first step is the original: nothing applied, on screen and on disk.
    const ResultCode jumped = session->select_history(0, diagnostic);
    if (jumped != ResultCode::Ok) std::printf("jump to original: %s\n", diagnostic.c_str());
    assert(jumped == ResultCode::Ok && session->history_current() == 0);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok && frame.rgba[0] < one);
    assert(session->undo(diagnostic) == ResultCode::StateConflict);
    assert(session->select_history(9, diagnostic) == ResultCode::StateConflict);
    assert(commits == 2 + 3);  // every jump counts as a change (thumbnail, edited state)

    // A new edit after going back is a new step with a newer revision.
    assert(session->commit("color.global", "exposure", "0.50", diagnostic) == ResultCode::Ok);
    assert(session->document().revision() > last_revision);
    assert(session->history_current() == 1 && session->history().size() == 2);  // original, new edit
    assert(session->redo(diagnostic) == ResultCode::StateConflict);
    assert(session->undo(diagnostic) == ResultCode::Ok);
    assert(session->history_current() == 0 && !session->document().effective_parameter("color.global", "exposure"));
    session.reset();
    std::remove(photo2.c_str());
  }

  // Paste effect: another photo's committed effect goes through the command gate, is one history step, and undoes.
  {
    const std::string source_photo = base + "-s.bmp";
    const std::string target_photo = base + "-t.bmp";
    write_grey_bmp(source_photo, 40, 20, 90);
    write_grey_bmp(target_photo, 40, 20, 90);
    EffectClipboard clipboard;
    {
      std::unique_ptr<PreviewSession> source;
      assert(PreviewSession::open(source_photo, options, source, diagnostic) == ResultCode::Ok);
      assert(source->commit("color.global", "contrast", "40", diagnostic) == ResultCode::Ok);
      clipboard = copy_committed(source->document());
    }
    assert(!clipboard.fragments.empty());
    std::unique_ptr<PreviewSession> target;
    assert(PreviewSession::open(target_photo, options, target, diagnostic) == ResultCode::Ok);
    int pastes = 0;
    target->on_committed([&pastes] { ++pastes; });
    assert(target->paste(clipboard, diagnostic) == ResultCode::Ok && pastes == 1);
    assert(target->document().effective_parameter("color.global", "contrast") == std::optional<std::string>("40"));
    assert(target->history().size() == 2 && target->history()[1].capability_id == PreviewSession::kPasteStep);
    assert(target->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok);
    assert(frame.result.code == ResultCode::Ok);
    assert(target->undo(diagnostic) == ResultCode::Ok && !target->document().effective_parameter("color.global", "contrast"));
    assert(target->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok && frame.result.code == ResultCode::Ok);
    assert(target->redo(diagnostic) == ResultCode::Ok &&
           target->document().effective_parameter("color.global", "contrast") == std::optional<std::string>("40"));
    target.reset();
    std::remove(source_photo.c_str());
    std::remove(target_photo.c_str());
  }

  // Failures before any process is left behind.
  std::unique_ptr<PreviewSession> none;
  assert(PreviewSession::open(base + "-missing.bmp", options, none, diagnostic) == ResultCode::SourceMissing);
  auto broken = options;
  broken.renderd_executable = base + "-no-renderd";
  assert(PreviewSession::open(photo, broken, none, diagnostic) == ResultCode::SourceMissing);
  assert(!none);

  // RenderdProcess on its own: exits when the client goes away.
  {
    std::unique_ptr<RenderdProcess> process;
    PreviewClient client;
    assert(RenderdProcess::start(PIXCAKE_RENDERD_EXE, options.log_directory, 10'000, process, client, diagnostic) ==
           ResultCode::Ok);
    assert(alive(process->pid()));
    client = PreviewClient{};
    assert(process->wait(5000) == 0);
  }

  std::remove(photo.c_str());
  std::printf("preview session ok\n");
  return 0;
}
