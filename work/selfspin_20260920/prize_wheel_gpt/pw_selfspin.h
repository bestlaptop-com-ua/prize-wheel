#pragma once
#include <esp_timer.h>
#include <driver/gpio.h>

// LOCAL REVIEW CANDIDATE. Deliberately cannot energize the motor as supplied.
// Change only after target-toolchain validation and explicit physical clearance.
#ifndef PW_SELFSPIN_MOTION_ENABLE
#define PW_SELFSPIN_MOTION_ENABLE 0
#endif
#if PW_WIFI_ENABLE
#error "Self-spin candidate requires WiFi disabled; network command parser is not part of this review"
#endif

namespace PwSelfspin {
constexpr uint32_t HZ = 640, ACCEL = 160, FINITE_STEPS = 3200;
constexpr uint32_t MAX_PULSES = 6400, SPINUP_MS = 6000, TOTAL_MS = 25000;
constexpr uint32_t CRUISE_MS = 200, COAST_MS = 300;
constexpr float MAX_SPEED = 0.30f, MAX_TRAVEL_DEG = 720.0f;
enum Phase : uint8_t { READY, SPINUP, DRAIN, HANDOFF, DONE };
static Phase phase = READY;
static bool consumed = false, timerReady = false;
static volatile bool active = false, inhibit = true;
static volatile uint8_t deadlineReason = 0;
static uint64_t totalDeadlineUs = 0, spinupDeadlineUs = 0;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t timer = nullptr;
static int dir = 0;
static uint32_t startMs = 0, cruiseSinceMs = 0, releaseMs = 0;
static uint32_t lastHealthMs = 0, idleSinceMs = 0, peakMs = 0;
static uint32_t mismatchSinceMs = 0;
static int32_t idleAnchorCounts = 0, lastSteps = 0;
static int32_t lastEncoderCounts = 0;
static uint64_t encoderTravelCounts = 0;
static uint32_t pulses = 0;
static float peak = 0.0f;

// TASK-dispatched ESP timer: independent of loop(), NOT a hard hardware cutoff.
// Flash/cache or higher-priority work can delay this callback. No bus or FAS calls.
static void watchdog(void*) {
  const uint64_t now = (uint64_t)esp_timer_get_time();
  portENTER_CRITICAL(&mux);
  if (active && (now >= totalDeadlineUs ||
      (spinupDeadlineUs && now >= spinupDeadlineUs))) {
    inhibit = true;
    deadlineReason = now >= totalDeadlineUs ? 2 : 1;
    gpio_set_level((gpio_num_t)PIN_EN, 1);
  }
  portEXIT_CRITICAL(&mux);
}

static bool killed() {
  portENTER_CRITICAL(&mux);
  bool result = inhibit;
  portEXIT_CRITICAL(&mux);
  return result;
}

static void finish(bool success, const char* reason) {
  // Remove torque before FAS cleanup, flash/NVS persistence or serial output.
  portENTER_CRITICAL(&mux);
  inhibit = true;
  active = false;
  spinupDeadlineUs = 0;
  gpio_set_level((gpio_num_t)PIN_EN, 1);
  portEXIT_CRITICAL(&mux);
  consumed = true;
  phase = DONE;
  takeoverEnabled = false;
  diagnosticFrozen = true;
  driverFreewheel();
  if (success) {
    state = ST_IDLE_STOPPED;
    stateEnteredMs = millis();
  } else if (state != ST_FAULT_LATCHED) {
    enterFault(FC_SELFSPIN_ABORT, reason);
  }
  Serial.printf("# SELFSPIN %s reason=%s pulses=%lu EN=%d capture_started=%d result=%s; consumed\n",
      success ? "SEQUENCE_DONE" : "ABORT", reason, (unsigned long)pulses,
      digitalRead(PIN_EN), spin.takeoverRevS > 0.0f, resultName(spin.result));
}

static bool accountPulses() {
  if (!active || !stepper) return true;
  const int32_t current = stepper->getCurrentPosition();
  const int64_t advance = (int64_t)current - (int64_t)lastSteps;
  if (advance < 0 || advance > MAX_PULSES ||
      (uint64_t)pulses + (uint64_t)advance > MAX_PULSES) {
    finish(false, "pulse budget or unexpected position reset");
    return false;
  }
  pulses += (uint32_t)advance;
  lastSteps = current;
  return true;
}

static bool driverHealthy() {
  const uint32_t drv = driver.DRV_STATUS();
  const uint8_t gst = (uint8_t)driver.GSTAT();
  return drv != 0 && drv != 0xFFFFFFFFUL && !(drv & 0x1E000000UL) && !(gst & 0x07);
}

static void start(int requestedDir) {
  const uint32_t now = millis();
  if (!PW_SELFSPIN_MOTION_ENABLE || !timerReady || consumed || active ||
      state != ST_IDLE_STOPPED || faultCode != FC_NONE || !stepper ||
      stepper->isRunning() || currentStage != CS_FREEWHEEL ||
      digitalRead(PIN_EN) != HIGH || !encoderMotionReady() ||
      fabsf(omega) > STILL_REV_S || !idleSinceMs || now - idleSinceMs < 1000 ||
      !motorDirectionCalibrated || !tmcOk || !diagnosticBuffer ||
      !diagnosticCapture || diagnosticFrozen || diagnosticDumpActive) {
    Serial.println(F("# SELFSPIN refused: disabled build, consumed, or idle/health/recorder prerequisite"));
    return;
  }
  if (driver.version() != 0x30 || driver.microsteps() != 16 || !driverHealthy()) {
    finish(false, "initial driver health/readback");
    return;
  }
  int fasSign = fasSignForEncoderDirection(requestedDir);
  if (!fasSign) { finish(false, "direction calibration"); return; }
  consumed = true; // one accepted attempt, including subsequent setup failures
  diagnosticSawMotion = true; // retain/dump even an early powered-start failure
  dir = requestedDir;
  startMs = peakMs = now;
  lastEncoderCounts = encoderCountsMT;
  encoderTravelCounts = 0;
  pulses = 0;
  lastSteps = 0;
  cruiseSinceMs = releaseMs = lastHealthMs = mismatchSinceMs = 0;
  peak = 0;
  takeoverEnabled = false;
  stepper->setJumpStart(0);
  stepper->setCurrentPosition(0); // not running; accounted start of pulse epoch
  stepper->setDirectionPin(PIN_DIR, fasSign > 0 ? INVERT_DIR : !INVERT_DIR);
  phase = SPINUP;
  state = ST_SELFSPIN;
  stateEnteredMs = now;
  const uint64_t beginUs = (uint64_t)esp_timer_get_time();
  portENTER_CRITICAL(&mux);
  totalDeadlineUs = beginUs + (uint64_t)TOTAL_MS * 1000;
  spinupDeadlineUs = beginUs + (uint64_t)SPINUP_MS * 1000;
  deadlineReason = 0;
  active = true;
  inhibit = false;
  portEXIT_CRITICAL(&mux);
  Serial.printf("# SELFSPIN START encoder_dir=%+d target_hz=%lu accel=%lu finite_steps=%lu\n",
      dir, (unsigned long)HZ, (unsigned long)ACCEL, (unsigned long)FINITE_STEPS);
  setCurrentStage(CS_PRECHARGE);
  setCurrentStage(CS_CAPTURE); // existing 2200 mA request, never a higher level
  if (killed() || !fasSetSpeedHz(HZ) || !fasSetAcceleration(ACCEL) ||
      stepper->move((int32_t)FINITE_STEPS) != MoveResultCode::OK) {
    finish(false, "spin-up setup/move rejected");
  }
}
} // namespace PwSelfspin

bool pwSelfspinEnableOutputs() {
  using namespace PwSelfspin;
  // Upstream FAS 1.2.7, MCPWM with ordinary GPIO7: enableOutputs only writes
  // GPIO levels. Reverify the INSTALLED library before permitting motion.
  portENTER_CRITICAL(&mux);
  bool allowed = PW_SELFSPIN_MOTION_ENABLE && active && !inhibit;
  bool ok = allowed && stepper && stepper->enableOutputs();
  if (!ok) gpio_set_level((gpio_num_t)PIN_EN, 1);
  portEXIT_CRITICAL(&mux);
  return ok;
}

bool pwSelfspinBeforePositionReset() {
  if (PwSelfspin::active) {
    if (!PwSelfspin::accountPulses()) return false;
    PwSelfspin::lastSteps = 0;
  }
  return true;
}

void pwSelfspinFaultDisable() {
  using namespace PwSelfspin;
  portENTER_CRITICAL(&mux);
  if (active) {
    inhibit = true;
    gpio_set_level((gpio_num_t)PIN_EN, 1);
  }
  portEXIT_CRITICAL(&mux);
}

bool pwSelfspinAllowControl() {
  using namespace PwSelfspin;
  return active && phase == HANDOFF && !killed() &&
      millis() - releaseMs >= COAST_MS;
}

void pwSelfspinBegin() {
  using namespace PwSelfspin;
  takeoverEnabled = false;
  gpio_set_level((gpio_num_t)PIN_EN, 1);
  esp_timer_create_args_t args = {};
  args.callback = watchdog;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "pw_selfspin";
  timerReady = esp_timer_create(&args, &timer) == ESP_OK &&
      esp_timer_start_periodic(timer, 10000) == ESP_OK;
  Serial.printf("# SELFSPIN candidate motion_compiled=%d timer_ready=%d; no boot motion\n",
      PW_SELFSPIN_MOTION_ENABLE, timerReady);
}

bool pwSelfspinCommand(char c) {
  using namespace PwSelfspin;
  if (c == '[' || c == ']') { start(c == '[' ? -1 : 1); return true; }
  if (c == '!') { finish(false, "explicit abort"); return true; }
  if (active && c != 's' && c != '\r' && c != '\n') {
    Serial.println(F("# SELFSPIN busy: status or abort only; recorder left intact"));
    return true;
  }
  // Candidate exposes only read/record commands in addition to its one shot.
  // No serial path can clear a fault, change calibration, or start other motion.
  if (c == 's' || c == 'f' || c == 'm' || c == '?' || c == 'd' || c == 'D' ||
      c == '\r' || c == '\n') return false;
  Serial.println(F("# SELFSPIN candidate: command refused; read/record/one-shot/abort only"));
  return true;
}

void pwSelfspinPrintStatus() {
  using namespace PwSelfspin;
  portENTER_CRITICAL(&mux);
  const bool activeNow = active, inhibitNow = inhibit;
  const uint8_t reason = deadlineReason;
  portEXIT_CRITICAL(&mux);
  Serial.printf("# selfspin motion_compiled=%d timer_ready=%d consumed=%d active=%d inhibit=%d "
      "phase=%u idle_ms=%lu diag_buffer=%d diag_armed=%d diag_frozen=%d dump_active=%d deadline_reason=%u\n",
      PW_SELFSPIN_MOTION_ENABLE, timerReady, consumed, activeNow, inhibitNow,
      (unsigned)phase, (unsigned long)(idleSinceMs ? millis() - idleSinceMs : 0),
      diagnosticBuffer != nullptr, diagnosticCapture, diagnosticFrozen,
      diagnosticDumpActive, (unsigned)reason);
}

void pwSelfspinService() {
  using namespace PwSelfspin;
  const uint32_t now = millis();
  if (!active) {
    bool idle = !consumed && state == ST_IDLE_STOPPED && faultCode == FC_NONE &&
        encoderMotionReady() && fabsf(omega) <= STILL_REV_S &&
        currentStage == CS_FREEWHEEL && digitalRead(PIN_EN) == HIGH &&
        stepper && !stepper->isRunning();
    if (!idle) idleSinceMs = 0;
    else if (!idleSinceMs ||
        fabsf(degreesForCounts(encoderCountsMT - idleAnchorCounts)) > 1.0f) {
      idleSinceMs = now;
      idleAnchorCounts = encoderCountsMT;
    }
    return;
  }
  if (state == ST_FAULT_LATCHED) { finish(false, "normal controller fault"); return; }
  if (killed()) { finish(false, deadlineReason == 1 ? "spin-up/drain deadline" : "test deadline"); return; }
  if (!accountPulses()) return;
  const float forward = omega * (float)dir;
  const int64_t delta = (int64_t)encoderCountsMT - (int64_t)lastEncoderCounts;
  encoderTravelCounts += (uint64_t)(delta < 0 ? -delta : delta);
  lastEncoderCounts = encoderCountsMT;
  const float travel = (float)encoderTravelCounts * 360.0f / 4096.0f;
  if (!encoderMotionReady() || !isfinite(forward) || fabsf(omega) > MAX_SPEED ||
      forward < -OPPOSITE_ABORT_REV_S || travel > MAX_TRAVEL_DEG) {
    finish(false, "encoder freshness/direction/speed/travel limit"); return;
  }
  if (now - lastHealthMs >= 100) {
    lastHealthMs = now;
    if (!driverHealthy()) { finish(false, "driver status"); return; }
  }
  if (phase == SPINUP) {
    if (forward > peak) { peak = forward; peakMs = now; }
    const float fas = fasWheelRevS();
    bool mismatch = now - startMs > 1200 &&
        (forward < fas * 0.45f || forward > fas + 0.07f);
    if (!mismatch) mismatchSinceMs = 0;
    else if (!mismatchSinceMs) mismatchSinceMs = now;
    if ((mismatchSinceMs && now - mismatchSinceMs >= 100) ||
        pulses >= FINITE_STEPS || travel > 360.0f || !stepper->isRunning()) {
      finish(false, "spin-up tracking or finite-motion limit"); return;
    }
    bool cruise = fas >= 0.185f && fas <= 0.215f && fabsf(forward - fas) <= 0.025f;
    if (!cruise) cruiseSinceMs = 0;
    else if (!cruiseSinceMs) cruiseSinceMs = now;
    else if (now - cruiseSinceMs >= CRUISE_MS) {
      driverFreewheel(); // actual EN-off first, then queue clear
      if (!accountPulses()) return;
      if (digitalRead(PIN_EN) != HIGH) {
        finish(false, "freewheel handoff failed"); return;
      }
      // FAS forceStop() is asynchronous: remain disabled until its queue drains.
      portENTER_CRITICAL(&mux);
      spinupDeadlineUs = (uint64_t)esp_timer_get_time() + 500000;
      portEXIT_CRITICAL(&mux);
      phase = DRAIN;
      Serial.printf("# SELFSPIN EN_OFF omega=%.4f pulse_total=%lu; draining disabled queue\n",
          omega, (unsigned long)pulses);
    }
  } else if (phase == DRAIN) {
    if (digitalRead(PIN_EN) != HIGH) { finish(false, "EN during queue drain"); return; }
    if (stepper->isRunning()) return;
    portENTER_CRITICAL(&mux);
    spinupDeadlineUs = 0;
    portEXIT_CRITICAL(&mux);
    releaseMs = now;
    phase = HANDOFF;
    startSpinEvent(dir, now);
    spin.pushStartMs = startMs;
    spin.peakRevS = fmaxf(peak, forward);
    lastPeakMs = peakMs;
    state = ST_SPIN_PUSH;
    stateEnteredMs = now;
    takeoverEnabled = true;
    Serial.printf("# SELFSPIN RELEASE queue_empty=1 omega=%.4f; synthetic PUSH, min_coast_ms=%lu\n",
        omega, (unsigned long)COAST_MS);
  } else if (phase == HANDOFF) {
    if (now - releaseMs < COAST_MS &&
        (digitalRead(PIN_EN) != HIGH || stepper->isRunning())) {
      finish(false, "minimum freewheel dwell violated"); return;
    }
    if (state == ST_SOFT_HOLD) finish(true, "normal landing finished; diagnostic releases hold");
    else if (state == ST_IDLE_STOPPED && !spinOpen) finish(true, "coast ended without capture");
  }
}
