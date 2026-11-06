#include "pixcake/subject/subject_store.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pixcake::subject {
namespace {

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.find('\0') == std::string::npos;
}

bool valid_box(const FaceBox& box) {
  return std::isfinite(box.left) && std::isfinite(box.top) && std::isfinite(box.width) &&
         std::isfinite(box.height) && box.width > 0.0 && box.height > 0.0;
}

bool valid_gender(GenderOverride value) {
  switch (value) {
    case GenderOverride::Unspecified:
    case GenderOverride::Female:
    case GenderOverride::Male:
      return true;
  }
  return false;
}

bool valid_age(AgeOverride value) {
  switch (value) {
    case AgeOverride::Unspecified:
    case AgeOverride::Child:
    case AgeOverride::Adult:
    case AgeOverride::Elder:
      return true;
  }
  return false;
}

bool valid_override(const SubjectOverride& value) {
  return valid_gender(value.gender) && valid_age(value.age);
}

bool same_override(const SubjectOverride& value) {
  return value.gender != GenderOverride::Unspecified || value.age != AgeOverride::Unspecified;
}

}  // namespace

const char* detection_state_name(DetectionState state) {
  switch (state) {
    case DetectionState::Pending:
      return "检测中";
    case DetectionState::Ready:
      return "已完成";
    case DetectionState::Failed:
      return "失败";
  }
  return "";
}

const char* subject_attribute_name(SubjectAttribute attribute) {
  switch (attribute) {
    case SubjectAttribute::Female:
      return "女";
    case SubjectAttribute::Male:
      return "男";
    case SubjectAttribute::Child:
      return "儿童";
    case SubjectAttribute::ElderFemale:
      return "长辈女";
    case SubjectAttribute::ElderMale:
      return "长辈男";
    case SubjectAttribute::Single:
      return "单人";
    case SubjectAttribute::All:
      return "所有人";
    case SubjectAttribute::Linked:
      return "联动调整";
  }
  return "";
}

SubjectStore::SubjectStore(std::string image_id) : image_id_(std::move(image_id)) {}

std::optional<SubjectStore> SubjectStore::open(std::string image_id) {
  if (!valid_identifier(image_id)) {
    return std::nullopt;
  }
  return SubjectStore(std::move(image_id));
}

ResultCode SubjectStore::replace_detection(DetectionSnapshot snapshot) {
  if (snapshot.state != DetectionState::Ready && !snapshot.faces.empty()) {
    return ResultCode::StateConflict;
  }
  std::vector<std::string> seen;
  for (const auto& face : snapshot.faces) {
    if (!valid_identifier(face.face_id) || !valid_box(face.box) ||
        std::find(seen.begin(), seen.end(), face.face_id) != seen.end()) {
      return ResultCode::StateConflict;
    }
    if (face.person_id.has_value() && !valid_identifier(*face.person_id)) {
      return ResultCode::StateConflict;
    }
    seen.push_back(face.face_id);
  }
  faces_ = std::move(snapshot.faces);
  detection_state_ = snapshot.state;
  return ResultCode::Ok;
}

bool SubjectStore::has_face(const std::string& face_id) const {
  return detection_state_ == DetectionState::Ready &&
         std::find_if(faces_.begin(), faces_.end(), [&face_id](const FaceRecord& face) {
           return face.face_id == face_id;
         }) != faces_.end();
}

ResultCode SubjectStore::set_override(const std::string& face_id, SubjectOverride value) {
  if (!valid_identifier(face_id) || !has_face(face_id) || !valid_override(value) ||
      !attribute_applicable(face_id)) {
    return ResultCode::StateConflict;
  }
  const auto existing = std::find_if(
      overrides_.begin(), overrides_.end(), [&face_id](const auto& item) { return item.first == face_id; });
  if (!same_override(value)) {
    if (existing != overrides_.end()) {
      overrides_.erase(existing);
    }
    return ResultCode::Ok;
  }
  if (existing == overrides_.end()) {
    overrides_.emplace_back(face_id, value);
  } else {
    existing->second = value;
  }
  return ResultCode::Ok;
}

ResultCode SubjectStore::clear_override(const std::string& face_id) {
  if (!valid_identifier(face_id) || !has_face(face_id) || !attribute_applicable(face_id)) {
    return ResultCode::StateConflict;
  }
  const auto existing = std::find_if(
      overrides_.begin(), overrides_.end(), [&face_id](const auto& item) { return item.first == face_id; });
  if (existing != overrides_.end()) {
    overrides_.erase(existing);
  }
  return ResultCode::Ok;
}

ResultCode SubjectStore::mark_single_adjustment(const std::string& face_id) {
  if (!valid_identifier(face_id) || !has_face(face_id)) {
    return ResultCode::StateConflict;
  }
  if (std::find(single_adjustment_faces_.begin(), single_adjustment_faces_.end(), face_id) ==
      single_adjustment_faces_.end()) {
    single_adjustment_faces_.push_back(face_id);
  }
  return ResultCode::Ok;
}

ResultCode SubjectStore::restore_original_attributes(const std::string& face_id) {
  if (!valid_identifier(face_id) || !has_face(face_id)) {
    return ResultCode::StateConflict;
  }
  const auto override_it = std::find_if(
      overrides_.begin(), overrides_.end(), [&face_id](const auto& item) { return item.first == face_id; });
  if (override_it != overrides_.end()) {
    overrides_.erase(override_it);
  }
  const auto lock_it = std::find(single_adjustment_faces_.begin(), single_adjustment_faces_.end(), face_id);
  if (lock_it != single_adjustment_faces_.end()) {
    single_adjustment_faces_.erase(lock_it);
  }
  return ResultCode::Ok;
}

std::string SubjectStore::image_id() const { return image_id_; }

DetectionState SubjectStore::detection_state() const { return detection_state_; }

bool SubjectStore::available() const { return detection_state_ == DetectionState::Ready; }

std::vector<FaceRecord> SubjectStore::faces() const {
  if (!available()) {
    return {};
  }
  return faces_;
}

std::optional<SubjectOverride> SubjectStore::override_for(const std::string& face_id) const {
  const auto existing = std::find_if(
      overrides_.begin(), overrides_.end(), [&face_id](const auto& item) { return item.first == face_id; });
  if (existing == overrides_.end()) {
    return std::nullopt;
  }
  return existing->second;
}

bool SubjectStore::single_adjustment_locked(const std::string& face_id) const {
  return std::find(single_adjustment_faces_.begin(), single_adjustment_faces_.end(), face_id) !=
         single_adjustment_faces_.end();
}

bool SubjectStore::attribute_applicable(const std::string& face_id) const {
  return has_face(face_id) && !single_adjustment_locked(face_id);
}

}  // namespace pixcake::subject
