// Transport between shell and renderd: framing, fd passing, shared frames,
// limits, timeouts and a vanished peer.
#include "pixcake/preview_channel/channel.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <string>
#include <thread>

using namespace pixcake::preview_channel;
using pixcake::ResultCode;

int main() {
  std::string diagnostic;
  int pair[2];
  assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
  Channel a{UniqueFd(pair[0])};
  Channel b{UniqueFd(pair[1])};

  // Plain message round trip; message bytes may contain NUL.
  const std::vector<std::uint8_t> hello = {1, 0, 2, 0, 3};
  assert(a.send(hello, -1, diagnostic) == ResultCode::Ok);
  std::vector<std::uint8_t> got;
  UniqueFd fd;
  assert(b.readable(1000));
  assert(b.receive(got, fd, 1024, 1000, diagnostic) == ResultCode::Ok);
  assert(got == hello && !fd.valid());

  // Nothing pending: timeout is Cancelled, not an error of the channel.
  assert(!b.readable(0));
  assert(b.receive(got, fd, 1024, 20, diagnostic) == ResultCode::Cancelled);

  // A large message arrives whole (several kernel writes).
  std::vector<std::uint8_t> big(3 * 1024 * 1024);
  for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::uint8_t>(i * 7);
  std::thread writer([&] { assert(a.send(big, -1, diagnostic) == ResultCode::Ok); });
  std::string reader_diagnostic;
  assert(b.receive(got, fd, 8 * 1024 * 1024, 5000, reader_diagnostic) == ResultCode::Ok);
  writer.join();
  assert(got == big);

  // A shared frame crosses with its message; the receiver maps the same memory.
  SharedFrame frame;
  assert(SharedFrame::create(4096 * 3, frame, diagnostic) == ResultCode::Ok);
  std::memset(frame.data(), 0, frame.size());
  frame.data()[0] = 42;
  frame.data()[frame.size() - 1] = 24;
  assert(a.send({9}, frame.fd(), diagnostic) == ResultCode::Ok);
  assert(b.receive(got, fd, 1024, 1000, diagnostic) == ResultCode::Ok);
  assert(got == std::vector<std::uint8_t>{9} && fd.valid());
  SharedFrame mapped;
  assert(SharedFrame::map(std::move(fd), 4096 * 3, mapped, diagnostic) == ResultCode::Ok);
  assert(mapped.data()[0] == 42 && mapped.data()[mapped.size() - 1] == 24);
  // Mapping more than the object holds is refused (sizes round up to whole pages).
  SharedFrame too_big;
  assert(a.send({9}, frame.fd(), diagnostic) == ResultCode::Ok);
  assert(b.receive(got, fd, 1024, 1000, diagnostic) == ResultCode::Ok);
  assert(SharedFrame::map(std::move(fd), 1 << 20, too_big, diagnostic) == ResultCode::StateConflict);
  assert(SharedFrame::create(0, too_big, diagnostic) == ResultCode::StateConflict);

  // A length above the receiver's limit is refused before reading the body.
  assert(a.send(std::vector<std::uint8_t>(2048, 1), -1, diagnostic) == ResultCode::Ok);
  assert(b.receive(got, fd, 1024, 1000, diagnostic) == ResultCode::StateConflict);

  // Peer gone: the reader sees IoFailed, the writer too.
  {
    int p2[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, p2) == 0);
    Channel left{UniqueFd(p2[0])};
    {
      Channel right{UniqueFd(p2[1])};
    }
    assert(left.receive(got, fd, 1024, 1000, diagnostic) == ResultCode::IoFailed);
    assert(left.send({1}, -1, diagnostic) == ResultCode::IoFailed);
  }

  // Listening path, same-user accept, connect.
  const std::string path = "/tmp/pixcake-channel-" + std::to_string(::getpid()) + ".sock";
  UniqueFd listener;
  assert(listen_at(path, listener, diagnostic) == ResultCode::Ok);
  UniqueFd accepted;
  assert(accept_same_user(listener.get(), 20, accepted, diagnostic) == ResultCode::Cancelled);
  UniqueFd client;
  assert(connect_to(path, client, diagnostic) == ResultCode::Ok);
  assert(accept_same_user(listener.get(), 1000, accepted, diagnostic) == ResultCode::Ok);
  Channel server{std::move(accepted)};
  Channel user{std::move(client)};
  assert(user.send({5, 6}, -1, diagnostic) == ResultCode::Ok);
  assert(server.receive(got, fd, 16, 1000, diagnostic) == ResultCode::Ok && got.size() == 2);
  // A stale socket file is replaced; the listener can be recreated.
  listener.reset();
  assert(listen_at(path, listener, diagnostic) == ResultCode::Ok);
  listener.reset();
  ::unlink(path.c_str());
  assert(connect_to(path, client, diagnostic) == ResultCode::IoFailed);
  // Refuses to replace something that is not a socket.
  const std::string file = path + ".txt";
  { FILE* f = std::fopen(file.c_str(), "w"); std::fputs("x", f); std::fclose(f); }
  assert(listen_at(file, listener, diagnostic) == ResultCode::StateConflict);
  ::unlink(file.c_str());
  return 0;
}
