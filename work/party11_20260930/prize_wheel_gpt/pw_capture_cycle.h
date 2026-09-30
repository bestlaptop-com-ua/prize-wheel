#pragma once
#include <stdint.h>

// 2026-09-23 fail-operational (FAILURE_MODES_2026-09-23.md, A2/A3/P3).
// The previous class allowed ONE capture attempt per 1 s rest in freewheel:
// a failed pickup, a spin after a reboot or fault clear, and any spin that
// started from the hold after a recovery got no steering at all.
//
// Now there is no budget.  An attempt may begin whenever none is being armed.
// An attempt that is abandoned before torque-on has had no mechanical effect
// (EN stayed high), so it may be retried after the caller's short back-off.
// Once energized, the state machine owns the wheel until it is held at rest
// or a guest re-spins it; a new spin clears any stale arming flag.
// The remaining flags are per-spin telemetry for the SUMMARY line.
class PwCaptureCycle {
 public:
  bool available() const { return !arming_; }
  bool beginAttempt() {
    if (arming_) return false;
    arming_ = true;
    attempted_ = true;
    if (attempts_ < 255) ++attempts_;
    return true;
  }
  void markEnergized() {
    if (!arming_) return;
    arming_ = false;
    energized_ = true;
  }
  void abandon() {
    if (arming_) abandoned_ = true;
    arming_ = false;
  }
  void newSpin() {
    arming_ = attempted_ = energized_ = abandoned_ = false;
    attempts_ = 0;
  }
  bool arming() const { return arming_; }
  bool attempted() const { return attempted_; }
  bool energized() const { return energized_; }
  bool abandoned() const { return abandoned_; }
  uint8_t attempts() const { return attempts_; }
 private:
  bool arming_ = false, attempted_ = false, energized_ = false, abandoned_ = false;
  uint8_t attempts_ = 0;
};
