#include "histogram_refresh_gate.hpp"

#include <cassert>

using pixcake::shell::HistogramRefreshGate;

int main() {
  HistogramRefreshGate gate;

  // The initial preview is high quality even though it enters through the
  // normal preview request path.
  gate.request(1, false, true);
  assert(!gate.frozen());
  assert(gate.accept_frame(1, false));

  // Drag frames are never eligible to replace the snapshot.
  gate.request(2, false, false);
  assert(gate.frozen());
  assert(!gate.accept_frame(2));
  gate.request(3, false, false);
  assert(!gate.accept_frame(3));

  // Release sends a high-quality request; only that frame unfreezes the chart.
  gate.request(4, true, false);
  assert(gate.frozen());
  assert(!gate.accept_frame(3, false));
  // The sequence itself identifies the pending high request; this also
  // handles a source whose high frame is naturally smaller than 600px.
  assert(gate.accept_frame(4, false));
  assert(!gate.frozen());

  // A new drag can start before the previous high-quality frame is delivered.
  // The high frame still refreshes the snapshot, but the hint remains visible
  // until the new drag is released and its high frame arrives.
  gate.request(5, false, false);
  gate.request(6, true, false);
  gate.request(7, false, false);
  assert(gate.frozen());
  assert(!gate.accept_frame(7, false));
  assert(gate.accept_frame(6, true));
  assert(gate.frozen());
  gate.request(8, true, false);
  assert(gate.accept_frame(8, true));
  assert(!gate.frozen());

  // A preview-size menu change is an immediate high-quality refresh, not a
  // drag. If it interrupts a frozen drag, the hint stays until its frame.
  gate.request(9, false, false);
  gate.request_high_quality(10);
  assert(gate.frozen());
  assert(gate.accept_frame(10, true));
  assert(!gate.frozen());
  return 0;
}
