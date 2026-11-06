#include "pixcake/manual/manual_stroke_session.hpp"

#include <cmath>
#include <algorithm>

namespace pixcake::manual {
namespace {

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.find('\0') == std::string::npos;
}

bool finite_coordinate(const Coordinate& point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

bool requires_source_point(StrokeKind kind) {
  return kind == StrokeKind::Heal || kind == StrokeKind::Patch || kind == StrokeKind::CloneStamp;
}

bool valid_kind(StrokeKind kind) {
  switch (kind) {
    case StrokeKind::Liquify:
    case StrokeKind::Heal:
    case StrokeKind::Patch:
    case StrokeKind::CloneStamp:
    case StrokeKind::SmearRevert:
    case StrokeKind::EdgeRemove:
      return true;
  }
  return false;
}

bool valid_stroke(const ManualStroke& stroke) {
  if (!valid_identifier(stroke.stroke_id) || !valid_identifier(stroke.image_id) ||
      !valid_kind(stroke.kind) || stroke.points.empty() || stroke.replay_invalid) {
    return false;
  }
  for (const auto& point : stroke.points) {
    if (!finite_coordinate(point)) {
      return false;
    }
  }
  if (stroke.source_point.has_value()) {
    if (!finite_coordinate(*stroke.source_point)) {
      return false;
    }
  }
  return !requires_source_point(stroke.kind) || stroke.source_point.has_value();
}

}  // namespace

const char* stroke_kind_name(StrokeKind kind) {
  switch (kind) {
    case StrokeKind::Liquify:
      return "液化";
    case StrokeKind::Heal:
      return "污点修复";
    case StrokeKind::Patch:
      return "修补";
    case StrokeKind::CloneStamp:
      return "仿制图章";
    case StrokeKind::SmearRevert:
      return "涂抹擦回";
    case StrokeKind::EdgeRemove:
      return "祛边";
  }
  return "";
}

const char* session_state_name(SessionState state) {
  switch (state) {
    case SessionState::Open:
      return "打开";
    case SessionState::Confirmed:
      return "已确认";
    case SessionState::Cancelled:
      return "已取消";
  }
  return "";
}

StrokeSession::StrokeSession(std::string image_id, std::uint64_t base_revision)
    : image_id_(std::move(image_id)), base_revision_(base_revision) {}

std::optional<StrokeSession> StrokeSession::open(std::string image_id,
                                                   std::uint64_t base_revision) {
  if (!valid_identifier(image_id)) {
    return std::nullopt;
  }
  return StrokeSession(std::move(image_id), base_revision);
}

ResultCode StrokeSession::append(ManualStroke stroke) {
  if (state_ != SessionState::Open || !valid_stroke(stroke) || stroke.image_id != image_id_ ||
      stroke.base_revision != base_revision_) {
    return ResultCode::StateConflict;
  }
  const auto duplicate = std::find_if(
      strokes_.begin(), strokes_.end(), [&stroke](const ManualStroke& existing) {
        return existing.stroke_id == stroke.stroke_id;
      });
  if (duplicate != strokes_.end()) {
    return ResultCode::StateConflict;
  }
  strokes_.push_back(std::move(stroke));
  return ResultCode::Ok;
}

ResultCode StrokeSession::confirm(ManualCommit& commit) {
  if (state_ != SessionState::Open || strokes_.empty()) {
    return ResultCode::StateConflict;
  }
  commit.image_id = image_id_;
  commit.base_revision = base_revision_;
  commit.strokes = strokes_;
  state_ = SessionState::Confirmed;
  return ResultCode::Ok;
}

ResultCode StrokeSession::cancel() {
  if (state_ != SessionState::Open) {
    return ResultCode::StateConflict;
  }
  strokes_.clear();
  state_ = SessionState::Cancelled;
  return ResultCode::Ok;
}

ResultCode StrokeSession::mark_replay_invalid(const std::string& stroke_id) {
  if (state_ != SessionState::Confirmed || !valid_identifier(stroke_id)) {
    return ResultCode::StateConflict;
  }
  const auto found = std::find_if(
      strokes_.begin(), strokes_.end(), [&stroke_id](const ManualStroke& stroke) {
        return stroke.stroke_id == stroke_id;
      });
  if (found == strokes_.end()) {
    return ResultCode::StateConflict;
  }
  found->replay_invalid = true;
  return ResultCode::Ok;
}

SessionState StrokeSession::state() const { return state_; }

std::string StrokeSession::image_id() const { return image_id_; }

std::uint64_t StrokeSession::base_revision() const { return base_revision_; }

std::vector<ManualStroke> StrokeSession::strokes() const { return strokes_; }

}  // namespace pixcake::manual
