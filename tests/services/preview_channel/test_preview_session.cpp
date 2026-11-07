// The shell's preview session against a real renderd process: drag (preview),
// release (commit through CommandGate), a second parameter keeps the first,
// reopening resumes the committed document, renderd exits with the session.
#include "pixcake/preview_channel/preview_session.hpp"

#include <signal.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <fstream>
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
    assert(session->preview("color.global", "temperature", "0", diagnostic) == ResultCode::Ok);
    assert(session->wait_latest_frame(frame, 15'000, diagnostic) == ResultCode::Ok);
    assert(frame.result.code == ResultCode::Ok && frame.rgba[0] == bright);
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
