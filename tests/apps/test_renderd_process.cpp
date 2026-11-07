// End to end across processes: launch pixcake_app_renderd --socket, connect as
// the shell would, send a source and a slider value, receive the frame through
// shared memory, then close and check renderd exits cleanly.
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/preview_channel/preview_client.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace {

using namespace pixcake;
using namespace pixcake::preview_channel;

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

pid_t launch(const std::string& socket, const std::string& logs) {
  std::vector<std::string> args = {PIXCAKE_RENDERD_EXE, "--socket", socket, "--log-dir", logs};
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int spawned = posix_spawn(&pid, PIXCAKE_RENDERD_EXE, nullptr, nullptr, argv.data(), environ);
  assert(spawned == 0);
  (void)spawned;
  return pid;
}

int wait_exit(pid_t pid) {
  int status = 0;
  for (int i = 0; i < 500; ++i) {
    if (::waitpid(pid, &status, WNOHANG) == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  ::kill(pid, SIGKILL);
  ::waitpid(pid, &status, 0);
  return -2;
}

bool connect_with_retry(const std::string& socket, PreviewClient& client) {
  std::string diagnostic;
  for (int i = 0; i < 500; ++i) {
    if (PreviewClient::connect(socket, client, diagnostic) == ResultCode::Ok) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

}  // namespace

int main() {
  const std::string base = "/tmp/pxc-e2e-" + std::to_string(::getpid());
  const std::string socket = base + ".sock";
  const std::string logs = base + "-logs";
  const std::string image = base + ".bmp";
  write_grey_bmp(image, 120, 80, 90);

  const pid_t renderd = launch(socket, logs);
  PreviewClient client;
  assert(connect_with_retry(socket, client));
  std::string diagnostic;

  const auto document = *EditDocument::create("e2e-image");
  ipc::PreviewSourceMessage source;
  source.image_id = document.image_id();
  source.editing_instance_id = document.editing_instance_id();
  source.document_revision = document.revision();
  source.source_path = image;
  source.document = document.encode();
  assert(client.send_source(source, diagnostic) == ResultCode::Ok);

  ipc::PreviewRequestMessage request;
  request.client_sequence = 1;
  request.request.image_id = document.image_id();
  request.request.editing_instance_id = document.editing_instance_id();
  request.request.base_revision = document.revision();
  request.request.capability_id = "color.global";
  request.request.parameter_id = "exposure";
  request.request.display_value = "1";
  request.request.max_edge = 60;
  assert(client.send_request(request, diagnostic) == ResultCode::Ok);

  PreviewFrame frame;
  assert(client.receive(frame, 10'000, diagnostic) == ResultCode::Ok);
  assert(frame.result.code == ResultCode::Ok && frame.result.client_sequence == 1);
  assert(frame.result.header.width == 60 && frame.result.header.height == 40);
  assert(frame.rgba.size() == 60u * 4u * 40u);
  assert(frame.rgba[0] > 120 && frame.rgba[3] == 255);  // grey 90 brightened by +1 EV

  // The shell goes away: renderd ends its session and exits 0, socket removed.
  client = PreviewClient{};
  assert(wait_exit(renderd) == 0);
  struct stat info {};
  assert(::stat(socket.c_str(), &info) != 0);

  // A shell that connects and leaves without sending anything: clean exit.
  const pid_t lonely = launch(socket, logs);
  PreviewClient probe;
  assert(connect_with_retry(socket, probe));
  probe = PreviewClient{};
  assert(wait_exit(lonely) == 0);

  std::remove(image.c_str());
  std::remove((logs + "/renderd.log").c_str());
  ::rmdir(logs.c_str());
  std::printf("renderd process round trip ok\n");
  return 0;
}
