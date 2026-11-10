#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pixcake::assistant {

inline constexpr std::size_t kMaxAssistantEventBytes = 256 * 1024;

struct AssistantEvent {
  std::string type = "message";
  std::string data;
  std::string event_id;
};

// Incremental Server-Sent Events decoder.  It accepts arbitrary network chunk
// boundaries and leaves payload interpretation to the UI/protocol adapter.
class AssistantSseParser final {
 public:
  ResultCode feed(const std::string& chunk, std::vector<AssistantEvent>& out, std::string& diagnostic);
  ResultCode finish(std::vector<AssistantEvent>& out, std::string& diagnostic);
  void reset();

 private:
  ResultCode consume_line(const std::string& line, std::vector<AssistantEvent>& out, std::string& diagnostic);
  void reset_event();
  bool has_event() const { return !data_.empty() || type_ != "message" || !event_id_.empty(); }

  std::string buffer_;
  std::string type_ = "message";
  std::string event_id_;
  std::vector<std::string> data_;
  std::size_t event_bytes_ = 0;
};

}  // namespace pixcake::assistant
