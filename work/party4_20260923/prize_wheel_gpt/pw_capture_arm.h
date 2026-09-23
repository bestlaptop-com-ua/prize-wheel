#pragma once
#include <math.h>
#include <stdint.h>

// Immediate diagnostic ceiling retained in active powered capture, braking
// and settling. Invalid velocity remains the encoder watchdog's responsibility.
inline bool pwControlOverspeed(float wheelRevS) {
  return isfinite(wheelRevS) && fabsf(wheelRevS) > 0.80f;
}

// Pulse observation only: agreement in speed DOES NOT establish rotor phase.
// All times use unsigned differences so the micros() rollover is harmless.
struct PwCaptureArm {
  enum Verdict : uint8_t { WAIT, READY, REJECT };
  enum Reason : uint8_t { NONE, CONFIG, DEADLINE, ENABLE, DIRECTION, ENCODER,
                          POSITION, PULSE_RATE, WHEEL_RATE };
  static constexpr uint32_t DEADLINE_US = 150000;
  static constexpr uint32_t MIN_WINDOW_US = 20000;
  static constexpr int32_t MIN_PULSES = 8;
  uint32_t startUs = 0, windowUs = 0, requestedHz = 0;
  int32_t startPosition = 0, windowPosition = 0, previousPosition = 0;
  bool firstPulse = false, expectedDirHigh = false, started = false;
  bool terminal = false, verified = false;
  uint32_t verifiedUs = 0;
  Verdict terminalVerdict = REJECT;
  Reason reason = NONE;
  float pulseHz = 0.0f, quantizationHz = 0.0f;

  bool begin(uint32_t nowUs, int32_t position, uint32_t hz, bool dirHigh) {
    *this = PwCaptureArm();
    startUs = nowUs;
    startPosition = windowPosition = previousPosition = position;
    requestedHz = hz;
    expectedDirHigh = dirHigh;
    started = hz >= 40 && hz <= 2400; // party window: up to 0.72 rev/s at 3200 usteps/rev
    if (!started) reject(CONFIG);
    return started;
  }

  Verdict reject(Reason why) {
    reason = why;
    terminal = true;
    verified = false;
    terminalVerdict = REJECT;
    return REJECT;
  }

  Verdict update(uint32_t nowUs, int32_t position, bool enHigh, bool dirHigh,
                 bool motionReady, float forwardRevS, float stepsPerRev,
                 float entryFraction) {
    verified = false;
    if (terminal) return terminalVerdict;
    if (!started || !(stepsPerRev > 0.0f) || !isfinite(stepsPerRev) ||
        !(entryFraction > 0.0f && entryFraction <= 1.0f)) return reject(CONFIG);
    if (uint32_t(nowUs - startUs) >= DEADLINE_US) return reject(DEADLINE);
    if (!enHigh) return reject(ENABLE);
    if (dirHigh != expectedDirHigh) return reject(DIRECTION);
    if (!motionReady || !isfinite(forwardRevS) || forwardRevS < 0.02f ||
        forwardRevS > 0.75f) return reject(ENCODER);
    const int64_t total = int64_t(position) - int64_t(startPosition);
    if (position < previousPosition || total < 0 || total > 512)
      return reject(POSITION);
    previousPosition = position;
    if (!firstPulse) {
      if (!total) return WAIT;
      firstPulse = true;
      windowUs = nowUs;
      windowPosition = position;
      return WAIT; // exclude asynchronous start latency from the rate estimate
    }
    const uint32_t dt = nowUs - windowUs;
    const int64_t pulses = int64_t(position) - int64_t(windowPosition);
    if (dt < MIN_WINDOW_US || pulses < MIN_PULSES) return WAIT;
    pulseHz = float(pulses) * 1000000.0f / float(dt);
    // One boundary pulse may fall on either side of a timestamped window.
    quantizationHz = 1000000.0f / float(dt);
    if (fabsf(pulseHz - float(requestedHz)) >
        float(requestedHz) * 0.05f + quantizationHz) return reject(PULSE_RATE);
    const float desiredHz = entryFraction * forwardRevS * stepsPerRev;
    if (fabsf(pulseHz - desiredHz) >
        desiredHz * 0.05f + 0.005f * stepsPerRev + quantizationHz)
      return reject(WHEEL_RATE);
    // Re-evaluation must still reject stale inputs or elapsed deadlines.
    verified = true;
    verifiedUs = nowUs;
    return READY;
  }
};

