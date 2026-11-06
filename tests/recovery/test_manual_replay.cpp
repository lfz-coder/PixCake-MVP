// 测试清单 T-08：手动笔触重放失败只做标记、不删数据（从 tests/kernel/manual/test_manual_stroke_session.cpp 迁来）。
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




}  // namespace

void test_replay_failure_marks_data_without_deleting_it() {
  using pixcake::ResultCode;
  using pixcake::manual::StrokeKind;
  auto session = pixcake::manual::StrokeSession::open("image-1", 4);
  EXPECT(session->append(stroke("clone-1", StrokeKind::CloneStamp)) == ResultCode::Ok);
  pixcake::manual::ManualCommit commit;
  EXPECT(session->confirm(commit) == ResultCode::Ok);
  EXPECT(session->mark_replay_invalid("clone-1") == ResultCode::Ok);
  EXPECT(session->mark_replay_invalid("clone-1") == ResultCode::Ok);
  const auto retained = session->strokes();
  EXPECT(retained.size() == 1);
  EXPECT(retained.front().stroke_id == "clone-1");
  EXPECT(retained.front().replay_invalid);
  EXPECT(retained.front().points.size() == 2);
  EXPECT(retained.front().source_point->x == 5.0);
  EXPECT(session->mark_replay_invalid("missing") == ResultCode::StateConflict);
  EXPECT(session->mark_replay_invalid("") == ResultCode::StateConflict);
}

int main() {
  test_replay_failure_marks_data_without_deleting_it();
  return g_failures == 0 ? 0 : 1;
}
