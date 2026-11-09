#pragma once

namespace pixcake::shell {

// Temporary canvas overlay. position is a percentage along the full canvas axis.
struct GuideLine {
  bool vertical = false;
  double position = 50.0;
};

}  // namespace pixcake::shell
