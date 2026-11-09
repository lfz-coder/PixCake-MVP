#pragma once

#include <cstdint>

namespace pixcake::shell {

// Keeps histogram sampling tied to a high-quality preview request. It does
// not render or sample anything itself; it only decides whether an arriving
// preview frame is eligible to replace the last histogram snapshot.
class HistogramRefreshGate final {
 public:
  void reset() {
    waiting_for_high_ = 0;
    drag_started_ = 0;
    initial_high_pending_ = false;
    frozen_ = false;
  }

  void request(std::uint64_t sequence, bool commit, bool first_request) {
    if (sequence == 0) return;
    if (!commit) {
      if (first_request) {
        waiting_for_high_ = sequence;
        drag_started_ = 0;
        initial_high_pending_ = true;
        frozen_ = false;
      } else {
        if (!frozen_ || waiting_for_high_ != 0) drag_started_ = sequence;
        frozen_ = true;
      }
      return;
    }
    waiting_for_high_ = sequence;
    initial_high_pending_ = false;
    if (!frozen_) drag_started_ = 0;
  }

  // A menu change issues a high-quality redraw outside a slider drag.
  void request_high_quality(std::uint64_t sequence) {
    if (sequence == 0) return;
    waiting_for_high_ = sequence;
    initial_high_pending_ = false;
    // If a drag is still being displayed, keep the hint until this request's
    // high-quality frame is accepted. A quiet request starts unfrozen.
    if (!frozen_) drag_started_ = 0;
  }

  // Returns true only for a high-quality frame allowed to replace the
  // histogram snapshot. Low interactive frames are rejected before sequence
  // state is changed, so an out-of-order low frame cannot satisfy a pending
  // high-quality request.
  // A pending high-quality frame is accepted even when a newer drag has
  // already started, so the snapshot is never lost between interactions.
  bool accept_frame(std::uint64_t sequence, bool high_quality = true) {
    if (sequence == 0) return false;
    // If the source itself is shorter than the interactive edge, the first
    // high-quality frame has the same dimensions as a low frame. It is still
    // safe to use for a request whose sequence is explicitly pending; all
    // other later frames require the explicit high-quality size check.
    const bool is_pending_request = waiting_for_high_ != 0 && sequence == waiting_for_high_;
    if (!high_quality && !initial_high_pending_ && !is_pending_request) return false;
    if (waiting_for_high_ != 0) {
      if (sequence < waiting_for_high_) return false;
      waiting_for_high_ = 0;
      initial_high_pending_ = false;
      if (drag_started_ == 0 || drag_started_ <= sequence) {
        drag_started_ = 0;
        frozen_ = false;
      }
      return true;
    }
    return !frozen_;
  }

  bool frozen() const { return frozen_; }

 private:
  std::uint64_t waiting_for_high_ = 0;
  std::uint64_t drag_started_ = 0;
  bool initial_high_pending_ = false;
  bool frozen_ = false;
};

}  // namespace pixcake::shell
