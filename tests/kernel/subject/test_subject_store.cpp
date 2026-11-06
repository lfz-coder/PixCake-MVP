#include "pixcake/subject/subject_store.hpp"

#include <cstdio>
#include <limits>
#include <string>
#include <utility>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::subject::FaceRecord face(const char* id, const char* person_id = nullptr) {
  pixcake::subject::FaceRecord value;
  value.face_id = id;
  if (person_id != nullptr) {
    value.person_id = person_id;
  }
  value.box = {10.0, 20.0, 100.0, 120.0};
  return value;
}

pixcake::subject::DetectionSnapshot ready(std::vector<pixcake::subject::FaceRecord> faces) {
  pixcake::subject::DetectionSnapshot snapshot;
  snapshot.state = pixcake::subject::DetectionState::Ready;
  snapshot.faces = std::move(faces);
  return snapshot;
}

void test_pending_and_failed_do_not_expose_geometry() {
  using pixcake::ResultCode;
  using pixcake::subject::DetectionState;
  auto store = pixcake::subject::SubjectStore::open("image-1");
  EXPECT(store.has_value());
  EXPECT(!store->available());
  EXPECT(store->faces().empty());
  EXPECT(store->replace_detection(ready({face("face-1")})) == ResultCode::Ok);
  EXPECT(store->faces().size() == 1);

  pixcake::subject::DetectionSnapshot pending;
  pending.state = DetectionState::Pending;
  EXPECT(store->replace_detection(pending) == ResultCode::Ok);
  EXPECT(!store->available());
  EXPECT(store->faces().empty());

  pixcake::subject::DetectionSnapshot invalid_pending;
  invalid_pending.state = DetectionState::Pending;
  invalid_pending.faces.push_back(face("face-2"));
  EXPECT(store->replace_detection(invalid_pending) == ResultCode::StateConflict);
}

void test_detection_validation_and_override_persistence() {
  using pixcake::ResultCode;
  using pixcake::subject::AgeOverride;
  using pixcake::subject::GenderOverride;
  auto store = pixcake::subject::SubjectStore::open("image-1");
  EXPECT(store->replace_detection(ready({face("face-1", "person-7")})) == ResultCode::Ok);
  pixcake::subject::SubjectOverride value;
  value.gender = GenderOverride::Female;
  value.age = AgeOverride::Adult;
  EXPECT(store->set_override("face-1", value) == ResultCode::Ok);
  EXPECT(store->override_for("face-1").has_value());

  EXPECT(store->replace_detection(ready({face("face-1", "person-7")})) == ResultCode::Ok);
  const auto retained = store->override_for("face-1");
  EXPECT(retained.has_value());
  EXPECT(retained->gender == GenderOverride::Female);
  EXPECT(retained->age == AgeOverride::Adult);

  auto duplicate = ready({face("face-1"), face("face-1")});
  EXPECT(store->replace_detection(duplicate) == ResultCode::StateConflict);
  auto bad_box = ready({face("bad-box")});
  bad_box.faces.front().box.width = std::numeric_limits<double>::quiet_NaN();
  EXPECT(store->replace_detection(bad_box) == ResultCode::StateConflict);
  auto bad_person = ready({face("bad-person")});
  bad_person.faces.front().person_id = "";
  EXPECT(store->replace_detection(bad_person) == ResultCode::StateConflict);
}

void test_single_adjustment_lock_and_restore() {
  using pixcake::ResultCode;
  using pixcake::subject::AgeOverride;
  using pixcake::subject::GenderOverride;
  auto store = pixcake::subject::SubjectStore::open("image-1");
  EXPECT(store->replace_detection(ready({face("face-1")})) == ResultCode::Ok);
  pixcake::subject::SubjectOverride value;
  value.gender = GenderOverride::Male;
  value.age = AgeOverride::Elder;
  EXPECT(store->set_override("face-1", value) == ResultCode::Ok);
  EXPECT(store->mark_single_adjustment("face-1") == ResultCode::Ok);
  EXPECT(store->single_adjustment_locked("face-1"));
  EXPECT(!store->attribute_applicable("face-1"));
  EXPECT(store->set_override("face-1", value) == ResultCode::StateConflict);
  EXPECT(store->clear_override("face-1") == ResultCode::StateConflict);
  EXPECT(store->restore_original_attributes("face-1") == ResultCode::Ok);
  EXPECT(!store->single_adjustment_locked("face-1"));
  EXPECT(store->attribute_applicable("face-1"));
  EXPECT(!store->override_for("face-1").has_value());
}

}  // namespace

int main() {
  test_pending_and_failed_do_not_expose_geometry();
  test_detection_validation_and_override_persistence();
  test_single_adjustment_lock_and_restore();
  return g_failures == 0 ? 0 : 1;
}
