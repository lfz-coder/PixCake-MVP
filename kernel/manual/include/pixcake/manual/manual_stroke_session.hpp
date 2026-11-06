#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::manual {

enum class StrokeKind {
  Liquify,
  Heal,
  Patch,
  CloneStamp,
  SmearRevert,
  EdgeRemove,
};

enum class SessionState {
  Open,
  Confirmed,
  Cancelled,
};

struct Coordinate {
  double x = 0.0;
  double y = 0.0;
};

struct ManualStroke {
  std::string stroke_id;
  std::string image_id;
  std::uint64_t base_revision = 0;
  StrokeKind kind = StrokeKind::Liquify;
  std::vector<Coordinate> points;
  std::optional<Coordinate> source_point;
  bool replay_invalid = false;
};

struct ManualCommit {
  std::string image_id;
  std::uint64_t base_revision = 0;
  std::vector<ManualStroke> strokes;
};

const char* stroke_kind_name(StrokeKind kind);
const char* session_state_name(SessionState state);

// Owns only the in-memory modal stroke session. It never mutates an
// EditDocument or writes a file; the command layer commits ManualCommit.
class StrokeSession {
 public:
  static std::optional<StrokeSession> open(std::string image_id, std::uint64_t base_revision);

  ResultCode append(ManualStroke stroke);
  ResultCode confirm(ManualCommit& commit);
  ResultCode cancel();
  ResultCode mark_replay_invalid(const std::string& stroke_id);

  SessionState state() const;
  std::string image_id() const;
  std::uint64_t base_revision() const;
  std::vector<ManualStroke> strokes() const;

 private:
  StrokeSession(std::string image_id, std::uint64_t base_revision);

  std::string image_id_;
  std::uint64_t base_revision_ = 0;
  SessionState state_ = SessionState::Open;
  std::vector<ManualStroke> strokes_;
};

}  // namespace pixcake::manual
