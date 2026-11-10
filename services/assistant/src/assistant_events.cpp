#include "pixcake/assistant/assistant_events.hpp"

#include <utility>

namespace pixcake::assistant {

void AssistantSseParser::reset_event() {
  type_ = "message";
  event_id_.clear();
  data_.clear();
  event_bytes_ = 0;
}

void AssistantSseParser::reset() {
  buffer_.clear();
  reset_event();
}

ResultCode AssistantSseParser::consume_line(const std::string& raw_line, std::vector<AssistantEvent>& out,
                                            std::string& diagnostic) {
  std::string line = raw_line;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  event_bytes_ += line.size() + 1;
  if (event_bytes_ > kMaxAssistantEventBytes) {
    diagnostic = "assistant SSE event is too large";
    reset_event();
    return ResultCode::StateConflict;
  }
  if (!line.empty()) {
    if (line.front() == ':') return ResultCode::Ok;
    const std::size_t separator = line.find(':');
    const std::string field = separator == std::string::npos ? line : line.substr(0, separator);
    std::string value = separator == std::string::npos ? std::string{} : line.substr(separator + 1);
    if (!value.empty() && value.front() == ' ') value.erase(value.begin());
    if (field == "event") {
      type_ = value.empty() ? "message" : std::move(value);
    } else if (field == "id") {
      event_id_ = std::move(value);
    } else if (field == "data") {
      data_.push_back(std::move(value));
    }
    return ResultCode::Ok;
  }
  if (has_event()) {
    AssistantEvent event;
    event.type = type_;
    event.event_id = event_id_;
    for (std::size_t i = 0; i < data_.size(); ++i) {
      if (i != 0) event.data.push_back('\n');
      event.data += data_[i];
    }
    out.push_back(std::move(event));
  }
  reset_event();
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode AssistantSseParser::feed(const std::string& chunk, std::vector<AssistantEvent>& out,
                                    std::string& diagnostic) {
  out.clear();
  diagnostic.clear();
  buffer_ += chunk;
  if (buffer_.size() > kMaxAssistantEventBytes) {
    diagnostic = "assistant SSE buffer is too large";
    reset();
    return ResultCode::StateConflict;
  }
  for (;;) {
    const std::size_t newline = buffer_.find('\n');
    if (newline == std::string::npos) break;
    std::string line = buffer_.substr(0, newline);
    buffer_.erase(0, newline + 1);
    const ResultCode code = consume_line(line, out, diagnostic);
    if (code != ResultCode::Ok) return code;
  }
  return ResultCode::Ok;
}

ResultCode AssistantSseParser::finish(std::vector<AssistantEvent>& out, std::string& diagnostic) {
  out.clear();
  diagnostic.clear();
  if (!buffer_.empty()) {
    const std::string line = std::move(buffer_);
    buffer_.clear();
    const ResultCode code = consume_line(line, out, diagnostic);
    if (code != ResultCode::Ok) return code;
  }
  if (has_event()) {
    AssistantEvent event;
    event.type = type_;
    event.event_id = event_id_;
    for (std::size_t i = 0; i < data_.size(); ++i) {
      if (i != 0) event.data.push_back('\n');
      event.data += data_[i];
    }
    out.push_back(std::move(event));
  }
  reset_event();
  return ResultCode::Ok;
}

}  // namespace pixcake::assistant
