#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <esp_timer.h>
#include <driver/gpio.h>
#include "../prize_wheel_gpt/pw_capture_cycle.h"
#include "../prize_wheel_gpt/pw_capture_arm.h"
#include "../prize_wheel_gpt/pw_capture_lease.h"
#include "../prize_wheel_gpt/pw_brake_profile.h"
#include "../prize_wheel_gpt/pw_speedup_watch.h"

std::uint64_t fakeNowUs = 1;
FakeEspTimer fakeTimer;
int fakePins[64] = {};
unsigned enableCalls = 0;
bool enableResult = true;
bool enableOutput() { ++enableCalls; fakePins[7] = 0; return enableResult; }

void testPhysicalOwnership() {
  PwCaptureCycle cycle;
  assert(!cycle.available() && !cycle.beginAttempt());
  cycle.observe(100, true, 50, 11);
  cycle.observe(1099, true, 50, 11);
  assert(!cycle.available());
  cycle.observe(1100, true, 50, 11);
  assert(cycle.available() && cycle.beginAttempt());
  cycle.markEnergized(); cycle.abandon();
  // Failed pickup -> RELEASED -> reversed/reclassified/repushed spin: no reset.
  for (uint32_t ms = 1200; ms <= 5000; ms += 50) {
    cycle.observe(ms, false, int32_t(ms), 11);
    assert(!cycle.available() && !cycle.beginAttempt());
  }
  assert(cycle.attempted() && cycle.energized() && cycle.abandoned());
  cycle.observe(5100, true, 50, 11);
  cycle.observe(5900, true, 70, 11); // slow drift restarts the stationary interval
  cycle.observe(6100, true, 70, 11);
  assert(!cycle.available());
  cycle.observe(6900, true, 70, 11);
  assert(cycle.available() && !cycle.attempted());
  assert(cycle.beginAttempt());
  // Stale encoder, powered hold, running queue and fault are all ineligible
  // in the real caller; false must never count toward this rest window.
  cycle.observe(7000, true, 70, 11);
  cycle.observe(7950, false, 70, 11);
  cycle.observe(8000, true, 70, 11);
  cycle.observe(8950, true, 70, 11);
  assert(!cycle.available());
  cycle.observe(9000, true, 70, 11);
  assert(cycle.available());
  PwCaptureCycle rollover;
  rollover.observe(UINT32_MAX - 500, true, 0, 11);
  rollover.observe(499, true, 0, 11);
  assert(rollover.available());
}

void testPulseProof() {
  for (bool dir : {false, true}) {
    PwCaptureArm arm;
    assert(arm.begin(1000, 0, 608, dir));
    assert(arm.update(2000, 1, true, dir, true, .20f, 3200, .95f) == PwCaptureArm::WAIT);
    assert(arm.update(22000, 13, true, dir, true, .20f, 3200, .95f) == PwCaptureArm::READY);
    assert(arm.verified && arm.verifiedUs == 22000);
    assert(arm.update(23000, 14, true, dir, false, .20f, 3200, .95f) == PwCaptureArm::REJECT);
    assert(!arm.verified && arm.reason == PwCaptureArm::ENCODER);
  }
  for (unsigned reason = 0; reason < 6; ++reason) {
    PwCaptureArm arm; assert(arm.begin(1000, 0, 608, true));
    const auto result = arm.update(reason == 0 ? 151000 : 2000,
        reason == 1 ? -1 : 1, reason != 2, reason != 3, true,
        reason == 4 ? .2001f : (reason == 5 ? -.2f : .2f), 3200, .95f);
    assert(result == PwCaptureArm::REJECT && !arm.verified);
  }
  PwCaptureArm fast; assert(!fast.begin(1000, 0, 641, true));
  PwCaptureArm noPulse; assert(noPulse.begin(1000, 0, 608, true));
  assert(noPulse.update(150999, 0, true, true, true, .2f, 3200, .95f) == PwCaptureArm::WAIT);
  assert(noPulse.update(151000, 0, true, true, true, .2f, 3200, .95f) == PwCaptureArm::REJECT);
}

void testSuccessfulHoldToNextSpin() {
  PwCaptureCycle cycle;
  cycle.observe(1, true, 0, 11); cycle.observe(1001, true, 0, 11);
  assert(cycle.beginAttempt()); cycle.markEnergized();
  // Actual controller supplies true only after fresh stable safe landing.
  cycle.markSuccessfulLanding(true);
  for (uint32_t ms = 2000; ms <= 3500; ms += 50) {
    cycle.observe(ms, false, 0, 11); // powered HOLD never resets the budget
    assert(!cycle.available() && !cycle.beginAttempt());
  }
  cycle.holdReleased(true); // outputs are now off; still no new attempt yet
  assert(!cycle.available());
  assert(cycle.confirmHandSpinAfterHold(true, true, true, true));
  assert(cycle.available() && !cycle.attempted());
  assert(cycle.beginAttempt()); cycle.markEnergized(); cycle.abandon();
  // A failed second pickup cannot reuse the previous successful landing.
  cycle.holdReleased(true);
  assert(!cycle.confirmHandSpinAfterHold(true, true, true, true));
  assert(!cycle.available());

  for (unsigned failure = 0; failure < 8; ++failure) {
    PwCaptureCycle blocked;
    blocked.observe(1, true, 0, 11); blocked.observe(1001, true, 0, 11);
    assert(blocked.beginAttempt());
    if (failure != 0) blocked.markEnergized();
    if (failure == 1) blocked.abandon();
    blocked.markSuccessfulLanding(failure != 2);
    blocked.holdReleased(failure != 3);
    if (failure == 4) blocked.fault();
    const bool accepted = blocked.confirmHandSpinAfterHold(failure != 5,
        failure != 4, failure != 6, failure != 7);
    assert(!accepted && !blocked.available());
  }
  PwCaptureCycle clearedFault;
  clearedFault.observe(1, true, 0, 11); clearedFault.observe(1001, true, 0, 11);
  assert(clearedFault.available()); clearedFault.fault();
  assert(!clearedFault.available());
  clearedFault.observe(2000, true, 0, 11);
  clearedFault.observe(2999, true, 0, 11); assert(!clearedFault.available());
  clearedFault.observe(3000, true, 0, 11); assert(clearedFault.available());
}

void testLease() {
  PwCaptureLease lease; assert(lease.begin(7));
  assert(fakeTimer.period == 1000);
  fakePins[7] = 1; fakeNowUs = 1000; assert(lease.arm());
  // No loop servicing: independent timer expires; a new proof cannot revive it.
  fakeNowUs = 151000; fakeTimer.args.callback(fakeTimer.args.arg);
  assert(lease.expired() && fakePins[7] == 1);
  assert(!lease.enable(true, uint32_t(fakeNowUs), enableOutput));
  assert(enableCalls == 0 && fakePins[7] == 1);
  lease.cancel(); fakeNowUs = 200000; assert(lease.arm());
  fakeNowUs += 5000;
  assert(!lease.enable(true, uint32_t(fakeNowUs - 2001), enableOutput));
  assert(enableCalls == 0);
  lease.cancel(); assert(lease.arm()); lease.cancel();
  assert(!lease.enable(true, uint32_t(fakeNowUs), enableOutput));
  assert(enableCalls == 0);
  assert(lease.arm());
  assert(!lease.enable(false, uint32_t(fakeNowUs), enableOutput));
  assert(enableCalls == 0);
  assert(lease.arm()); fakeNowUs += 2000;
  assert(lease.enable(true, uint32_t(fakeNowUs), enableOutput));
  assert(enableCalls == 1 && fakePins[7] == 0);
  // A finished lease cannot produce a second enable edge.
  assert(!lease.enable(true, uint32_t(fakeNowUs), enableOutput));
  assert(enableCalls == 1 && fakePins[7] == 1);
  assert(lease.arm()); enableResult = false;
  assert(!lease.enable(true, uint32_t(fakeNowUs), enableOutput));
  assert(enableCalls == 2 && fakePins[7] == 1);
}

void testCoherentPlanAndSpeedup() {
  assert(!pwControlOverspeed(.30f) && !pwControlOverspeed(-.30f));
  assert(pwControlOverspeed(.3001f) && pwControlOverspeed(-.3001f));
  assert(pwControlOverspeed(1.0f) && pwControlOverspeed(-1.0f));
  assert(!pwControlOverspeed(NAN)); // invalid-encoder handling is not replaced
  for (uint32_t hz = 40; hz <= 640; hz += 40) {
    for (unsigned deg = 7; deg <= 180; ++deg) {
      auto plan = pwPlanBrake(hz, float(deg), 3200, 320);
      if (!plan.feasible) continue;
      assert(plan.accelerationSps2 <= 320);
      assert(pwBrakeDistanceDeg(float(hz) / 3200, plan.decelRevS2) <= float(deg) + .001f);
      const float limited = pwLimitBrakeCommand(.2f, 0, plan.decelRevS2, .5f);
      assert(limited <= .2f && limited >= .2f - plan.decelRevS2 * .050f - .000001f);
    }
  }
  assert(!pwPlanBrake(640, 7, 3200, 320).feasible); // reject, never silently clip
  assert(!pwPlanBrake(0, 50, 3200, 320).feasible);
  assert(!pwPlanBrake(640, NAN, 3200, 320).feasible);
  PwSpeedupWatch watch;
  for (uint32_t ms = 0; ms < 1000; ms += 25) {
    const float speed = ms == 500 ? .02f : .2f;
    assert(!watch.update(ms, speed, .05f, 400)); // one low outlier cannot poison baseline
  }
  bool fault = false;
  for (uint32_t ms = 1000; ms <= 1650; ms += 25)
    fault = watch.update(ms, .27f, .05f, 400) || fault;
  assert(fault);
  watch.reset();
  for (uint32_t ms = 0; ms < 250; ms += 25) assert(!watch.update(ms, .2f, .05f, 400));
  assert(!watch.update(1000, .27f, .05f, 400)); // no missing-time debounce credit
}

int main() {
  testPhysicalOwnership(); testSuccessfulHoldToNextSpin(); testPulseProof(); testLease(); testCoherentPlanAndSpeedup();
  std::puts("capture core tests passed: physical rest/hold ownership, pulse proof, independent lease, coherent plan, overspeed and speed-up guards");
}
