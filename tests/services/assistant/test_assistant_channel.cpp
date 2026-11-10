#include "pixcake/assistant/assistant_channel.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace {

using pixcake::ResultCode;
using pixcake::assistant::AssistantChannel;
using pixcake::assistant::kMaxAssistantFrameBytes;
using pixcake::preview_channel::Channel;
using pixcake::preview_channel::UniqueFd;

void expect(bool condition) {
  if (!condition) std::abort();
}

}  // namespace

int main() {
  int sockets[2] = {-1, -1};
  expect(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  AssistantChannel sender{Channel{UniqueFd{sockets[0]}}};
  AssistantChannel receiver{Channel{UniqueFd{sockets[1]}}};

  const std::vector<std::uint8_t> request{'{', '"', 'o', 'k', '"', ':', '1', '}'};
  std::string diagnostic;
  expect(sender.send(request, diagnostic) == ResultCode::Ok);
  std::vector<std::uint8_t> received;
  expect(receiver.receive(received, 1000, diagnostic) == ResultCode::Ok);
  expect(received == request);

  const std::vector<std::uint8_t> oversized(kMaxAssistantFrameBytes + 1, 0);
  expect(sender.send(oversized, diagnostic) == ResultCode::StateConflict);
  return 0;
}
