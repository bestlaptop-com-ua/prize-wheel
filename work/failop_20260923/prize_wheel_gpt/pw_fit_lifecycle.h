#pragma once
#include <stdint.h>

enum class PwFitEnd { Preview, CoastEnd, Powered, Contact, Reversal, Repush, Fault };

// Owns only the sampling/finalization policy, not the friction estimator.
class PwFitLifecycle {
 public:
  void begin() { open_ = true; attempted_ = false; lastSampleCount_ = 0; }
  void discard() { open_ = false; }
  bool open() const { return open_; }
  bool canSample(bool outputsOff) {
    if (!outputsOff) discard();
    return open_;
  }
  bool beginAttempt(PwFitEnd end, bool enoughData, uint8_t sampleCount) {
    if (!open_) return false;
    if (end == PwFitEnd::Contact || end == PwFitEnd::Reversal ||
        end == PwFitEnd::Repush || end == PwFitEnd::Fault) {
      discard();
      return false;  // known contamination: do not fit that coast
    }
    if (end == PwFitEnd::Preview) {
      if (attempted_ && sampleCount == lastSampleCount_) return false;
      attempted_ = true;
      lastSampleCount_ = sampleCount;
    }
    if (end != PwFitEnd::Preview) discard();
    return enoughData;
  }
  void completeAttempt(bool accepted, bool contaminated = false) {
    if (accepted || contaminated) discard();
    // An unsuccessful Preview leaves the clean coast open for more samples.
  }
 private:
  bool open_ = false;
  bool attempted_ = false;
  uint8_t lastSampleCount_ = 0;
};
