#include "pixcake/assistant/assistant_events.hpp"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

using pixcake::ResultCode;
using pixcake::assistant::AssistantEvent;
using pixcake::assistant::AssistantSseParser;
using pixcake::assistant::kMaxAssistantEventBytes;

void expect(bool condition) {
  if (!condition) std::abort();
}

}  // namespace

int main() {
  AssistantSseParser parser;
  std::vector<AssistantEvent> events;
  std::string diagnostic;
  const std::string raw = "event: plan.ready\r\nid: e-1\r\ndata: {\"plan_id\":\"p\"}\r\n\r\n";
  for (std::size_t offset = 0; offset < raw.size(); offset += 3) {
    expect(parser.feed(raw.substr(offset, 3), events, diagnostic) == ResultCode::Ok);
  }
  expect(events.size() == 1 && events.front().type == "plan.ready" && events.front().event_id == "e-1");
  expect(events.front().data == R"({"plan_id":"p"})");

  expect(parser.feed("event: message\ndata: first\ndata: second\n", events, diagnostic) == ResultCode::Ok);
  expect(parser.finish(events, diagnostic) == ResultCode::Ok);
  expect(events.size() == 1 && events.front().data == "first\nsecond");

  expect(parser.feed(std::string(kMaxAssistantEventBytes + 1, 'x'), events, diagnostic) == ResultCode::StateConflict);
  return 0;
}
