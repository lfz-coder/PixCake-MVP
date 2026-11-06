#include "pixcake/cull/cull_session.hpp"

#include <algorithm>
#include <utility>

namespace pixcake::cull {
namespace {

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.find('\0') == std::string::npos;
}

bool valid_scene(Scene scene) {
  switch (scene) {
    case Scene::General:
    case Scene::Portrait:
    case Scene::IdPhoto:
    case Scene::Event:
    case Scene::Wedding:
    case Scene::Family:
    case Scene::Child:
    case Scene::School:
    case Scene::Pregnancy:
    case Scene::Boudoir:
      return true;
  }
  return false;
}

bool valid_sensitivity(Sensitivity sensitivity) {
  switch (sensitivity) {
    case Sensitivity::Weak:
    case Sensitivity::Standard:
    case Sensitivity::Strong:
      return true;
  }
  return false;
}

bool valid_target(MarkTarget target) {
  switch (target) {
    case MarkTarget::Star:
    case MarkTarget::Color:
    case MarkTarget::Flag:
      return true;
  }
  return false;
}

bool has_target(const ExistingLabels& existing, MarkTarget target) {
  switch (target) {
    case MarkTarget::Star:
      return existing.has_star;
    case MarkTarget::Color:
      return existing.has_color;
    case MarkTarget::Flag:
      return existing.has_flag;
  }
  return false;
}

}  // namespace

const char* scene_name(Scene scene) {
  switch (scene) {
    case Scene::General:
      return "通用";
    case Scene::Portrait:
      return "人像";
    case Scene::IdPhoto:
      return "证件照";
    case Scene::Event:
      return "活动";
    case Scene::Wedding:
      return "婚礼";
    case Scene::Family:
      return "家庭";
    case Scene::Child:
      return "儿童";
    case Scene::School:
      return "学校";
    case Scene::Pregnancy:
      return "孕妇";
    case Scene::Boudoir:
      return "私房";
  }
  return "";
}

const char* sensitivity_name(Sensitivity sensitivity) {
  switch (sensitivity) {
    case Sensitivity::Weak:
      return "弱";
    case Sensitivity::Standard:
      return "标准";
    case Sensitivity::Strong:
      return "强";
  }
  return "";
}

const char* mark_target_name(MarkTarget target) {
  switch (target) {
    case MarkTarget::Star:
      return "星标";
    case MarkTarget::Color:
      return "色标";
    case MarkTarget::Flag:
      return "旗标";
  }
  return "";
}

CullSession::CullSession(CullOptions options) : options_(options) {}

std::optional<CullSession> CullSession::begin(CullOptions options) {
  if (!valid_scene(options.scene) || !valid_sensitivity(options.sensitivity) ||
      !valid_target(options.mark_target) || (options.selection_limit.has_value() && *options.selection_limit == 0)) {
    return std::nullopt;
  }
  return CullSession(options);
}

ResultCode CullSession::record_decision(CullDecision decision) {
  if (!active_ || !valid_identifier(decision.image_id) ||
      std::find_if(decisions_.begin(), decisions_.end(), [&decision](const CullDecision& existing) {
        return existing.image_id == decision.image_id;
      }) != decisions_.end()) {
    return ResultCode::StateConflict;
  }
  if (!decision.duplicate_group.empty() && !valid_identifier(decision.duplicate_group)) {
    return ResultCode::StateConflict;
  }
  decisions_.push_back(std::move(decision));
  return ResultCode::Ok;
}

ResultCode CullSession::finish(CullResult& out) {
  if (!active_) {
    return ResultCode::StateConflict;
  }
  out.options = options_;
  out.decisions = decisions_;
  out.actions.clear();
  std::size_t selected = 0;
  for (const auto& decision : decisions_) {
    MarkAction action;
    action.image_id = decision.image_id;
    action.target = options_.mark_target;
    const bool within_limit = !options_.selection_limit.has_value() || selected < *options_.selection_limit;
    if (decision.selected && within_limit) {
      ++selected;
      if (options_.preserve_existing_labels && has_target(decision.existing, options_.mark_target)) {
        action.skipped_existing = true;
      } else {
        action.applied = true;
      }
    }
    out.actions.push_back(std::move(action));
  }
  active_ = false;
  return ResultCode::Ok;
}

ResultCode CullSession::cancel() {
  if (!active_) {
    return ResultCode::StateConflict;
  }
  decisions_.clear();
  active_ = false;
  return ResultCode::Ok;
}

bool CullSession::active() const { return active_; }

std::vector<CullDecision> CullSession::decisions() const { return decisions_; }

}  // namespace pixcake::cull
