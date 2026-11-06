#pragma once

#include "pixcake/support/result_code.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pixcake::subject {

enum class DetectionState {
  Pending,
  Ready,
  Failed,
};

enum class SubjectAttribute {
  Female,
  Male,
  Child,
  ElderFemale,
  ElderMale,
  Single,
  All,
  Linked,
};

enum class GenderOverride {
  Unspecified,
  Female,
  Male,
};

enum class AgeOverride {
  Unspecified,
  Child,
  Adult,
  Elder,
};

struct FaceBox {
  double left = 0.0;
  double top = 0.0;
  double width = 0.0;
  double height = 0.0;
};

struct FaceRecord {
  std::string face_id;
  std::optional<std::string> person_id;
  FaceBox box;
};

struct SubjectOverride {
  GenderOverride gender = GenderOverride::Unspecified;
  AgeOverride age = AgeOverride::Unspecified;
};

struct DetectionSnapshot {
  DetectionState state = DetectionState::Pending;
  std::vector<FaceRecord> faces;
};

const char* detection_state_name(DetectionState state);
const char* subject_attribute_name(SubjectAttribute attribute);

// Owns the rebuildable subject detection view and the user-owned overrides.
// It does not run a model, infer identity, mutate an EditDocument, or write files.
class SubjectStore {
 public:
  static std::optional<SubjectStore> open(std::string image_id);

  ResultCode replace_detection(DetectionSnapshot snapshot);
  ResultCode set_override(const std::string& face_id, SubjectOverride value);
  ResultCode clear_override(const std::string& face_id);
  ResultCode mark_single_adjustment(const std::string& face_id);
  ResultCode restore_original_attributes(const std::string& face_id);

  std::string image_id() const;
  DetectionState detection_state() const;
  bool available() const;
  std::vector<FaceRecord> faces() const;
  std::optional<SubjectOverride> override_for(const std::string& face_id) const;
  bool single_adjustment_locked(const std::string& face_id) const;
  bool attribute_applicable(const std::string& face_id) const;

 private:
  explicit SubjectStore(std::string image_id);

  bool has_face(const std::string& face_id) const;

  std::string image_id_;
  DetectionState detection_state_ = DetectionState::Pending;
  std::vector<FaceRecord> faces_;
  std::vector<std::pair<std::string, SubjectOverride>> overrides_;
  std::vector<std::string> single_adjustment_faces_;
};

}  // namespace pixcake::subject
