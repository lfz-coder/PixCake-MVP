#include "pixcake/manual/manual_stroke_session.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::manual::ManualStroke stroke(const char* id, pixcake::manual::StrokeKind kind) {
  pixcake::manual::ManualStroke value;
  value.stroke_id = id;
  value.image_id = "image-1";
  value.base_revision = 4;
  value.kind = kind;
  value.points = {{10.0, 20.0}, {11.0, 21.0}};
  if (kind == pixcake::manual::StrokeKind::Heal || kind == pixcake::manual::StrokeKind::Patch ||
      kind == pixcake::manual::StrokeKind::CloneStamp) {
    value.source_point = pixcake::manual::Coordinate{5.0, 6.0};
  }
  return value;
}

void test_identity_and_append_validation() {
  using pixcake::ResultCode;
  using pixcake::manual::StrokeKind;
  EXPECT(!pixcake::manual::StrokeSession::open("", 4).has_value());
  auto session = pixcake::manual::StrokeSession::open("image-1", 4);
  EXPECT(session.has_value());
  EXPECT(session->state() == pixcake::manual::SessionState::Open);
  EXPECT(session->append(stroke("liquify-1", StrokeKind::Liquify)) == ResultCode::Ok);

  auto duplicate = stroke("liquify-1", StrokeKind::Liquify);
  EXPECT(session->append(duplicate) == ResultCode::StateConflict);
  auto wrong_image = stroke("wrong-image", StrokeKind::Liquify);
  wrong_image.image_id = "image-2";
  EXPECT(session->append(wrong_image) == ResultCode::StateConflict);
  auto wrong_revision = stroke("wrong-revision", StrokeKind::Liquify);
  wrong_revision.base_revision = 5;
  EXPECT(session->append(wrong_revision) == ResultCode::StateConflict);
  auto missing_source = stroke("missing-source", StrokeKind::Heal);
  missing_source.source_point.reset();
  EXPECT(session->append(missing_source) == ResultCode::StateConflict);
  auto non_finite = stroke("non-finite", StrokeKind::Liquify);
  non_finite.points[0].x = std::numeric_limits<double>::quiet_NaN();
  EXPECT(session->append(non_finite) == ResultCode::StateConflict);
}

void test_confirm_and_cancel_have_explicit_lifecycles() {
  using pixcake::ResultCode;
  using pixcake::manual::StrokeKind;
  auto empty = pixcake::manual::StrokeSession::open("image-1", 4);
  pixcake::manual::ManualCommit commit;
  EXPECT(empty->confirm(commit) == ResultCode::StateConflict);
  EXPECT(empty->cancel() == ResultCode::Ok);
  EXPECT(empty->strokes().empty());
  EXPECT(empty->confirm(commit) == ResultCode::StateConflict);

  auto session = pixcake::manual::StrokeSession::open("image-1", 4);
  EXPECT(session->append(stroke("heal-1", StrokeKind::Heal)) == ResultCode::Ok);
  EXPECT(session->confirm(commit) == ResultCode::Ok);
  EXPECT(session->state() == pixcake::manual::SessionState::Confirmed);
  EXPECT(commit.image_id == "image-1");
  EXPECT(commit.base_revision == 4);
  EXPECT(commit.strokes.size() == 1);
  EXPECT(commit.strokes.front().source_point.has_value());
  EXPECT(session->append(stroke("late", StrokeKind::Liquify)) == ResultCode::StateConflict);
  EXPECT(session->cancel() == ResultCode::StateConflict);
}


}  // namespace

int main() {
  test_identity_and_append_validation();
  test_confirm_and_cancel_have_explicit_lifecycles();
  return g_failures == 0 ? 0 : 1;
}
