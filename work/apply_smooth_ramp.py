from pathlib import Path
import shutil
root=Path(__file__).parent.parent
src=root/'prize_wheel_gpt/prize_wheel_gpt.ino'
backup=root/'work/before-smooth-ramp';backup.mkdir(exist_ok=True)
shutil.copy2(src,backup/src.name)
s=src.read_text(encoding='utf-8-sig')
def swap(a,b):
 global s
 assert a in s,a[:100]
 s=s.replace(a,b,1)
header='''#pragma once
#include <math.h>
#include <stdint.h>

struct PwBrakePlan {
  bool feasible;
  uint32_t accelerationSps2;
  float decelRevS2;
};

// Same total deceleration is used for feasibility, pulse ramps and stopping.
inline float pwBrakeDistanceDeg(float speedRevS, float decelRevS2) {
  if (!(speedRevS >= 0.0f) || !(decelRevS2 > 0.0f)) return INFINITY;
  return 180.0f * speedRevS * speedRevS / decelRevS2;
}

inline PwBrakePlan pwPlanBrake(uint32_t entryHz, float remainingDeg,
                              float stepsPerRev, uint32_t capSps2) {
  PwBrakePlan out = {false, 0, 0.0f};
  if (!entryHz || !(remainingDeg > 0.0f) || !isfinite(remainingDeg) ||
      !(stepsPerRev > 0.0f) || !isfinite(stepsPerRev) || !capSps2) return out;
  float steps = remainingDeg * stepsPerRev / 360.0f;
  float required = ceilf((float)entryHz * (float)entryHz / (2.0f * steps));
  required = fmaxf(required, ceilf(0.008f * stepsPerRev));
  if (!isfinite(required) || required > (float)capSps2) return out;
  out.feasible = true;
  out.accelerationSps2 = (uint32_t)required;
  out.decelRevS2 = required / stepsPerRev;
  return out;
}

inline float pwLimitBrakeCommand(float previous, float desired,
                                float decelRevS2, float dtSeconds) {
  // A delayed loop must not deliver a large catch-up change in one update.
  float dt = fmaxf(0.0f, fminf(dtSeconds, 0.050f));
  float lower = fmaxf(0.0f, previous - decelRevS2 * dt);
  return fminf(previous, fmaxf(lower, desired));
}
'''
(root/'prize_wheel_gpt/pw_brake_profile.h').write_bytes(header.replace('\n','\r\n').encode())
swap('#include "pw_step_clock.h"','#include "pw_step_clock.h"\n#include "pw_brake_profile.h"')
a=s.index('// FastAccelStepper\'s acceleration must out-pace')
b=s.index('// --- landing / hold ---',a)
s=s[:a]+'// The pulse generator uses the planned deceleration, including stopMove().\n// No faster tracking acceleration is used during normal braking.\n\n'+s[b:]
s=s.replace('const uint16_t SPEEDUP_TRIM_MS        = 30;     // trim command after this\n','')
s=s.replace('uint32_t lastSpeedupTrimMs = 0;\n','').replace('  lastSpeedupTrimMs = 0;\n','')
# Feasibility floors in target selection AND reachable-window estimate.
needle='  if (coupledMinDeg > winMin) winMin = coupledMinDeg;'
assert s.count(needle)==2
s=s.replace(needle,needle+'\n  float profileMinDeg = latencyDeg + pwBrakeDistanceDeg(\n      fminf(cmd0est, CAPTURE_MAX_CMD_REV_S),\n      (float)DECEL_CEILING_SPS2 / WHEEL_USTEPS_PER_REV) + STOP_GATE_EXTRA_DEG;\n  if (profileMinDeg > winMin) winMin = profileMinDeg;')
swap('                  + 2.0f;\n\n  // Pass 1', '''                  + 2.0f;
  assistMin = fmaxf(assistMin, latencyDeg + pwBrakeDistanceDeg(
      fminf(cmd0est, CAPTURE_MAX_CMD_REV_S),
      (float)ASSIST_DECEL_MAX_SPS2 / WHEEL_USTEPS_PER_REV) + STOP_GATE_EXTRA_DEG);

  // Pass 1''')
swap('  if (!out.found && naturalDeg > 10.0f) {','  if (!out.found && naturalDeg > 10.0f && naturalDeg - 2.0f >= assistMin) {')
a=s.index('  // Plan the command-profile deceleration to consume exactly the runway.')
b=s.index('  if (!fasSetSpeedHz(hz)) return false;',a)
s=s[:a]+'''  // Quantize entry speed before planning. Reject a too-short target rather
  // than clipping acceleration and creating an immediate speed discontinuity.
  cmd0 = (float)hz / WHEEL_USTEPS_PER_REV;
  PwBrakePlan plan = pwPlanBrake(hz, remainingDeg, WHEEL_USTEPS_PER_REV,
                                planDecelCapSps2);
  if (!plan.feasible) return false;
  float aPlan = plan.decelRevS2;
  if (spin.targetQuality == 0 &&
      aPlan > naturalDecelRevS2(forward, takeoverDir)) return false;
  planDecelRevS2 = aPlan;
  uint32_t aPlanSps2 = plan.accelerationSps2;
  uint32_t aFasSps2 = aPlanSps2;
  // Jump-start the pulse clock near the already-moving rotor. This is a
  // ramp coordinate, not extra commanded travel; do not clamp it to 60%
  // of the runway and silently lower the entry speed.
  uint32_t jumpStep = (uint32_t)lroundf(
      (float)hz * (float)hz / (2.0f * (float)aFasSps2));

'''+s[b:]
a=s.index('      if (nowMs - speedupSinceMs >= SPEEDUP_TRIM_MS')
b=s.index('      if (nowMs - speedupSinceMs >= SPEEDUP_FAULT_MS)',a)
s=s[:a]+'''      // Keep the fault debounce, but do not add repeated 5% command
      // cuts outside the planned ramp. They amplified a tracking loss.
'''+s[b:]
swap('  if (stopRequested) return;  // FAS ignores speed updates while stopping','''  if (stopRequested) {
    // The library now owns the smooth final ramp at planDecelRevS2.
    cmdRevS = fminf(cmdRevS, fasWheelRevS());
    if (cmdRevS < spin.cmdMinRevS) spin.cmdMinRevS = cmdRevS;
    return;
  }''')
a=s.index('  // Overshoot: begin the taper at once.')
b=s.index('  // Monotonic command computation.',a)
s=s[:a]+'''  // Both overshoot and normal stop use the SAME planned acceleration.
  // Keep braking current while pulses ramp down; do not weaken coupling
  // at the instant the motor is asked to finish the stop.
  float stopLeadDeg = (float)stepper->stepsToStop() * 360.0f / WHEEL_USTEPS_PER_REV
                    + STOP_GATE_EXTRA_DEG;
  if (takeoverDir * (encoderCountsMT - targetCountsMT) > toleranceCounts ||
      remaining <= stopLeadDeg) {
    stopRequested = true;
    stepper->stopMove();
    Serial.printf("# RAMP_STOP a=%lu remain=%.1f lead=%.1f fas=%.3f\\n",
                  (unsigned long)stepper->getAcceleration(), remaining,
                  stopLeadDeg, fasWheelRevS());
    return;
  }

'''+s[b:]
a=s.index('  // Monotonic command computation.')
b=s.index('  float prevCmd = cmdRevS;',a)
s=s[:a]+'''  // Position supplies a desired speed, while elapsed time bounds changes.
  // The hardware uses the same acceleration limit, rather than an 8x ramp.
'''+s[b:]
a=s.index('  // Capture-ramp surge fix')
b=s.index('  float couplingSlack =',a)
s=s[:a]+'''  // A slower encoder can lower the desired command, but cannot introduce
  // an unbounded step. Existing reversal, speed-up and fight faults remain.
'''+s[b:]
swap('  // Invariant: commanded motor speed never increases after capture.','''  newCmd = pwLimitBrakeCommand(prevCmd, newCmd, planDecelRevS2,
                               (float)dtMs * 0.001f);

  // Invariant: commanded motor speed never increases after capture.''')
swap('    stopRequested = true;\n    setCurrentStage(CS_TAPER);\n    stepper->stopMove();','    stopRequested = true;\n    stepper->stopMove();')
a=s.index('  // Apply to the pulse generator only on meaningful change')
b=s.index('  uint32_t deltaHz =',a)
s=s[:a]+'''  // Avoid unnecessary library ramp re-quantization. Actual acceleration
  // stays bounded by aPlanSps2 even when a lower target is requested.
'''+s[b:]
swap('      // Never step the ladder back up if the stop taper already began.\n        if (!stopRequested) setCurrentStage(CS_BRAKE);','      // A planned stop may already be running; keep full braking torque.\n        setCurrentStage(CS_BRAKE);')
swap('      setCurrentStage(CS_TAPER);  // idempotent; undoes any late CS_BRAKE write','      setCurrentStage(CS_BRAKE);  // retain torque through settling and hold')
swap('v2-hold1650-20260918; 1650mA hold until next spin; STEP clock fixed','v2-smooth-ramp-20260918; planned pulse ramp; 1650mA persistent hold')
src.write_bytes(s.replace('\n','\r\n').encode())
print('Patched',src)