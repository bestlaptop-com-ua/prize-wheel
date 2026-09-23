#pragma once
#include <stdint.h>

// Ownership follows physical rest, not logical spin records. A re-push,
// reversal, failed pickup or new startSpinEvent cannot replenish this budget.
class PwCaptureCycle {
 public:
  static constexpr uint32_t REST_MS = 1000;
  void observe(uint32_t nowMs, bool freewheelStationary, int32_t position,
               int32_t maxRestTravelCounts) {
    if (!freewheelStationary) { resting_ = false; return; }
    const int64_t travel = int64_t(position) - int64_t(anchor_);
    if (!resting_ || travel > maxRestTravelCounts || travel < -maxRestTravelCounts) {
      resting_ = true; since_ = nowMs; anchor_ = position; return;
    }
    if (uint32_t(nowMs - since_) >= REST_MS) {
      qualify();
    }
  }
  bool available() const { return qualified_ && !attempted_; }
  bool beginAttempt() {
    if (!available()) return false;
    attempted_ = true; resting_ = false;
    completedLanding_ = releasedLanding_ = false;
    return true;
  }
  void markEnergized() { if (attempted_) energized_ = true; }
  void abandon() {
    if (attempted_) abandoned_ = true;
    completedLanding_ = releasedLanding_ = false;
  }
  // A successful controlled landing already passed the controller's fresh
  // stable-position/stillness verdict. Merely entering a hold is insufficient,
  // and this authorization does not replenish the attempt while powered.
  void markSuccessfulLanding(bool stableSafeVerdict) {
    completedLanding_ = stableSafeVerdict && attempted_ && energized_ && !abandoned_;
    releasedLanding_ = false;
  }
  void holdReleased(bool outputsFreewheel) {
    releasedLanding_ = completedLanding_ && outputsFreewheel;
    completedLanding_ = false;
  }
  bool confirmHandSpinAfterHold(bool kinematicallyConfirmed, bool faultFree,
                                bool enHigh, bool queueEmpty) {
    const bool allowed = releasedLanding_ && kinematicallyConfirmed &&
                         faultFree && enHigh && queueEmpty;
    completedLanding_ = releasedLanding_ = false; // one confirmed spin only
    if (allowed) { qualify(); resting_ = false; }
    return allowed;
  }
  void fault() {
    completedLanding_ = releasedLanding_ = false;
    qualified_ = resting_ = false; // a cleared fault still requires fresh rest
  }
  bool attempted() const { return attempted_; }
  bool energized() const { return energized_; }
  bool abandoned() const { return abandoned_; }
 private:
  void qualify() {
    qualified_ = true; attempted_ = energized_ = abandoned_ = false;
    completedLanding_ = releasedLanding_ = false;
  }
  bool qualified_ = false, resting_ = false, attempted_ = false;
  bool energized_ = false, abandoned_ = false;
  bool completedLanding_ = false, releasedLanding_ = false;
  uint32_t since_ = 0;
  int32_t anchor_ = 0;
};
