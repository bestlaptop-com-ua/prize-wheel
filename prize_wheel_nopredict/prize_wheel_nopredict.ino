/*
 * Prize Wheel firmware - mid-spin-only intervention
 *
 * Objective:
 *   - Guest spins the wheel freely in either direction.
 *   - Safe predicted landings are untouched.
 *   - If a dare landing is predicted, the motor synchronizes to the existing
 *     direction and speed, then smoothly decelerates toward a safe wedge.
 *   - The firmware never reverses and never starts a second powered move after
 *     the wheel has stopped.
 *
 * v2 additions (2026-07-29):
 *   - Static label-true wedge frame anchored on rawZero (NVS "rawZero").
 *     Reboots and blind-gap re-primes can no longer corrupt wedge identity.
 *   - Automatic friction-model calibration: every free coast is fitted to
 *     domega/dt = -(c + b*omega) per direction and blended into NVS-persisted
 *     coefficients. Prediction error is logged on every unsteered landing.
 *   - Safe-target choice is uniform-random among all reachable safe wedges
 *     instead of nearest-first.
 *
 * Hardware:
 *   ESP32-WROOM-32, BTT TMC2209 V1.3 over UART, NEMA17, 2:1 GT2 belt,
 *   AS5600 on the wheel shaft.
 *
 * Build target:
 *   ESP32 Arduino core 3.3.10, FQBN esp32:esp32:esp32
 *   Library: TMCStepper
 */

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <TMCStepper.h>
#include <math.h>

#define TMC_SERIAL Serial2
#define TMC_RX_PIN 16
#define TMC_TX_PIN 17
#define TMC_ADDR 0b00
#define R_SENSE 0.11f
#define PIN_EN 25
#define PIN_STEP 26
#define PIN_DIR 27
#define PIN_SDA 21
#define PIN_SCL 22
#define AS5600_ADDR 0x36
#define AS5600_RAW 0x0C

constexpr int MOTOR_FULLSTEPS = 200;
constexpr int MICROSTEPS = 16;
constexpr float GEAR_RATIO = 2.0f;
constexpr float WHEEL_USTEPS_PER_REV = (float)MOTOR_FULLSTEPS * (float)MICROSTEPS * GEAR_RATIO;
constexpr int NUM_WEDGES = 12;
constexpr float WEDGE_DEG = 360.0f / (float)NUM_WEDGES;
constexpr int ENCODER_DIR_SIGN = -1;

enum Mode : uint8_t { IDLE, FREE_SPIN, SYNC_FIELD, SYNC_CAPTURE, TAKEOVER, SETTLE, DIR_PROBE, DONE, FAULT };
uint16_t dareMask = (1U << 1) | (1U << 5);
bool isDare(int wedge) {
  wedge %= NUM_WEDGES;
  if (wedge < 0) wedge += NUM_WEDGES;
  return ((dareMask >> wedge) & 1U) != 0;
}

constexpr uint16_t RMS_CURRENT_MA = 1450;
constexpr float SPIN_DETECT_REV_S = 0.12f;
constexpr uint32_t SPIN_CONFIRM_MS = 60;
constexpr uint32_t SPIN_CONFIRM_TIMEOUT_MS = 1200;
constexpr float SPIN_CONFIRM_TRAVEL_DEG = 6.0f;
constexpr float SPIN_CANCEL_BACKTRACK_DEG = 2.0f;
constexpr bool ENABLE_MOTOR_TAKEOVER = true;
constexpr float TAKEOVER_TRIGGER_REV_S = 0.28f;
constexpr float TAKEOVER_MIN_REV_S = 0.075f;
constexpr float TAKEOVER_MIN_PEAK_REV_S = 0.30f;
constexpr float TAKEOVER_MAX_START_REV_S = 0.30f;
constexpr float PREDICTION_DARE_MARGIN_DEG = 10.0f;
constexpr float TAKEOVER_DECEL_REV_S2 = 650.0f / WHEEL_USTEPS_PER_REV;
constexpr float TAKEOVER_RUNWAY_MARGIN_DEG = 18.0f;
constexpr float TAKEOVER_MAX_RUNWAY_DEG = 540.0f;  // VARIANT: >= min-runway + 360 so every safe wedge stays reachable (uniform distribution)
constexpr float ENGAGE_AT_PEAK_FRACTION = 0.50f;   // VARIANT: engage once omega decays to half of spin peak
constexpr float SAFE_EDGE_MARGIN_DEG = 9.0f;
constexpr float SAFE_TARGET_JITTER_DEG = 2.0f;
constexpr float TAKEOVER_RELEASE_REMAIN_DEG = 2.2f;
constexpr float TAKEOVER_RELEASE_REV_S = 0.035f;
constexpr float TAKEOVER_EARLY_STOP_REV_S = 0.018f;
constexpr float TAKEOVER_EARLY_STOP_REMAIN_DEG = 8.0f;
constexpr uint32_t SYNC_FIELD_LEAD_MS = 25;
constexpr uint32_t SYNC_LOW_CURRENT_MS = 65;
constexpr uint16_t SYNC_CURRENT_MA = 80;
constexpr uint16_t TAKEOVER_INITIAL_CURRENT_MA = 180;
constexpr uint16_t TAKEOVER_BRAKE_CURRENT_MA = 280;
constexpr uint32_t TAKEOVER_CURRENT_RAMP_MS = 220;
constexpr float OPPOSITE_ABORT_REV_S = 0.040f;
constexpr uint32_t OPPOSITE_ABORT_MS = 35;
constexpr float SLIP_ABORT_REV_S = 0.080f;
constexpr uint32_t SLIP_ABORT_MS = 75;
constexpr float GUEST_OVERRIDE_REV_S = 0.80f;
constexpr uint32_t GUEST_OVERRIDE_MS = 60;
constexpr float STILL_REV_S = 0.020f;
constexpr uint32_t SETTLE_MS = 550;
constexpr uint16_t DIR_PROBE_CURRENT_MA = 280;
constexpr uint32_t DIR_PROBE_SPEED_HZ = 100;
constexpr float DIR_PROBE_TRAVEL_DEG = 9.0f;
constexpr float DIR_PROBE_MIN_DEG = 3.0f;
constexpr uint32_t DIR_PROBE_TIMEOUT_MS = 3500;
constexpr uint32_t ENCODER_SAMPLE_PERIOD_US = 1000;
constexpr uint32_t ENCODER_MAX_GOOD_GAP_US = 20000;
constexpr uint32_t ENCODER_FRESH_US = 50000;
constexpr float ENCODER_MAX_PLAUSIBLE_REV_S = 8.0f;
constexpr int32_t ENCODER_DELTA_MARGIN_COUNTS = 16;
constexpr uint32_t VELOCITY_WINDOW_US = 30000;
constexpr uint32_t VELOCITY_MIN_WINDOW_US = 20000;
constexpr uint32_t VELOCITY_FILTER_TAU_US = 25000;
constexpr uint8_t VELOCITY_HISTORY_LEN = 64;
constexpr uint32_t DIRECTION_CAL_VERSION = 0x00020001UL;

// Friction auto-calibration (free-coast fit of domega/dt = -(c + b*omega)).
constexpr uint32_t FIT_SAMPLE_PERIOD_MS = 120;
constexpr uint8_t FIT_MAX_SAMPLES = 96;
constexpr uint8_t FIT_PAIR_STRIDE = 4;
constexpr uint8_t FIT_MIN_SAMPLES = 26;
constexpr float FIT_MIN_SPAN_RAD_S = 1.2f;
constexpr float FIT_MIN_SPEED_REV_S = 0.045f;
constexpr float FIT_MAX_SPEED_REV_S = 3.0f;
constexpr float FIT_C_MIN = 0.02f, FIT_C_MAX = 3.0f;
constexpr float FIT_B_MIN = 0.005f, FIT_B_MAX = 1.5f;
constexpr float FIT_BLEND = 0.35f;

TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, TMC_ADDR);
Preferences preferences;

Mode mode = IDLE;
bool debugLog = false;
bool takeoverEnabled = ENABLE_MOTOR_TAKEOVER;
// Label-true frame anchor: AS5600 raw count at the leading edge of wedge 0.
// Owner-measured 2026-07-28 for this wheel; 'z' re-anchors and persists.
uint16_t rawZero = 3807;
bool motorDirectionCalibrated = false;
int dirHighEncoderSign = 0;
uint32_t spinCounter = 0;
uint32_t activeSpinNumber = 0;
int spinDir = 1;
float activeSpinPeakOmega = 0.0f;
bool activeSpinSteered = false;
bool activeSpinHasDecision = false;
float activeSpinPredAngle = 0.0f;
int activeSpinPredWedge = -1;
int activeSpinTargetWedge = -1;
uint32_t spinCandidateMs = 0;
int32_t spinCandidateStartCounts = 0;
int spinCandidateDir = 1;
uint32_t settleStartMs = 0;
uint32_t guestOverrideStartMs = 0;
float takeoverTargetDeg = 0.0f;
int32_t takeoverStartCounts = 0;
int32_t takeoverTargetTravelCounts = 0;
int takeoverDir = 1;
float takeoverCommandRevS = 0.0f;
float takeoverStartRevS = 0.0f;
uint32_t takeoverStateStartedMs = 0;
uint32_t takeoverLastUpdateUs = 0;
uint32_t takeoverOppositeStartMs = 0;
uint32_t takeoverSlipStartMs = 0;
uint32_t takeoverCurrentRaisedMs = 0;
uint32_t directionProbeStartedMs = 0;
int32_t directionProbeStartCounts = 0;
bool directionProbeOutputsEnabled = false;
float cwC = 0.30f, cwB = 0.15f;
float ccwC = 0.30f, ccwB = 0.15f;
uint16_t cwFitCount = 0, ccwFitCount = 0;

struct FitSample { uint32_t ms; float omegaRadS; };
FitSample fitSamples[FIT_MAX_SAMPLES];
uint8_t fitSampleCount = 0;
int fitDir = 0;
uint32_t fitLastSampleMs = 0;
bool fitFinished = false;

struct EncoderRead {
  bool ok;
  uint16_t raw;
  uint32_t doneUs;
  uint16_t i2cUs;
  uint8_t txStatus;
  uint8_t requested;
  uint8_t available;
};
struct VelocityPoint { uint32_t timeUs; int32_t counts; };
EncoderRead encoderRead;
VelocityPoint velocityHistory[VELOCITY_HISTORY_LEN];
bool encoderPrimed = false;
bool encoderVelocityValid = false;
uint16_t lastGoodRaw = 0;
uint32_t lastGoodUs = 0;
int32_t encoderCountsMT = 0;
double angleDegMT = 0.0;
float omega = 0.0f;
uint32_t lastVelocityUpdateUs = 0;
uint8_t velocityHistoryHead = 0;
uint8_t velocityHistoryCount = 0;
bool samplerScheduled = false;
uint32_t nextSampleDueUs = 0;

void clearVelocityHistory() { velocityHistoryHead = 0; velocityHistoryCount = 0; lastVelocityUpdateUs = 0; }
void pushVelocityPoint(uint32_t timeUs, int32_t counts) {
  velocityHistory[velocityHistoryHead].timeUs = timeUs;
  velocityHistory[velocityHistoryHead].counts = counts;
  velocityHistoryHead = (velocityHistoryHead + 1U) % VELOCITY_HISTORY_LEN;
  if (velocityHistoryCount < VELOCITY_HISTORY_LEN) ++velocityHistoryCount;
}
uint8_t historyIndexFromNewest(uint8_t samplesBack) {
  return (velocityHistoryHead + VELOCITY_HISTORY_LEN - 1U - samplesBack) % VELOCITY_HISTORY_LEN;
}
void invalidateVelocity() { encoderVelocityValid = false; omega = 0.0f; clearVelocityHistory(); }
// Static label-true frame: raw -> wheel counts, invariant across reboots.
// ENCODER_DIR_SIGN is -1, so the wheel angle grows as raw decreases; the
// count for a given raw is therefore (rawZero - raw) folded into [0, 4096).
int32_t staticCountsFromRaw(uint16_t raw) {
  int32_t counts = (int32_t)rawZero - (int32_t)raw;
  counts %= 4096;
  if (counts < 0) counts += 4096;
  return counts;
}
void primeEncoder(uint16_t raw, uint32_t doneUs, bool preserveNearestTurn) {
  int32_t staticCounts = staticCountsFromRaw(raw);
  if (!encoderPrimed || !preserveNearestTurn) encoderCountsMT = staticCounts;
  else {
    // Snap to the multiple of 4096 nearest the running multiturn count so a
    // blind gap keeps the turn number while restoring the label-true phase.
    int32_t diff = encoderCountsMT - staticCounts;
    int32_t turns = (int32_t)lroundf((float)diff / 4096.0f);
    encoderCountsMT = staticCounts + turns * 4096;
  }
  encoderPrimed = true;
  lastGoodRaw = raw;
  lastGoodUs = doneUs;
  angleDegMT = (double)encoderCountsMT * 360.0 / 4096.0;
  invalidateVelocity();
  pushVelocityPoint(doneUs, encoderCountsMT);
}
bool readRawSample() {
  uint32_t startUs = micros();
  encoderRead.ok = false; encoderRead.raw = 0; encoderRead.txStatus = 0; encoderRead.requested = 0; encoderRead.available = 0;
  Wire.beginTransmission(AS5600_ADDR); Wire.write(AS5600_RAW); encoderRead.txStatus = Wire.endTransmission(false);
  if (encoderRead.txStatus == 0) {
    size_t requested = Wire.requestFrom((uint8_t)AS5600_ADDR, (size_t)2);
    encoderRead.requested = requested > 255U ? 255U : (uint8_t)requested;
    int available = Wire.available(); encoderRead.available = available > 255 ? 255 : (uint8_t)available;
    if (requested == 2U && available >= 2) {
      uint16_t highByte = (uint16_t)Wire.read(); uint16_t lowByte = (uint16_t)Wire.read();
      encoderRead.raw = ((highByte << 8) | lowByte) & 0x0FFFU;
      encoderRead.doneUs = micros();
      uint32_t elapsed = encoderRead.doneUs - startUs;
      encoderRead.i2cUs = elapsed > 65535U ? 65535U : (uint16_t)elapsed;
      encoderRead.ok = true; return true;
    }
    while (Wire.available()) (void)Wire.read();
  }
  encoderRead.doneUs = micros();
  uint32_t elapsed = encoderRead.doneUs - startUs;
  encoderRead.i2cUs = elapsed > 65535U ? 65535U : (uint16_t)elapsed;
  return false;
}
bool velocityFromWindow(uint32_t nowUs, float& velocityRevS) {
  if (velocityHistoryCount < 2U) return false;
  int chosen = -1; uint32_t chosenAgeUs = 0;
  for (uint8_t back = 1; back < velocityHistoryCount; ++back) {
    uint8_t index = historyIndexFromNewest(back);
    uint32_t ageUs = nowUs - velocityHistory[index].timeUs;
    if (ageUs >= VELOCITY_WINDOW_US) { chosen = index; chosenAgeUs = ageUs; break; }
  }
  if (chosen < 0) { chosen = historyIndexFromNewest(velocityHistoryCount - 1U); chosenAgeUs = nowUs - velocityHistory[chosen].timeUs; }
  if (chosenAgeUs < VELOCITY_MIN_WINDOW_US) return false;
  int32_t deltaCounts = encoderCountsMT - velocityHistory[chosen].counts;
  velocityRevS = ((float)deltaCounts / 4096.0f) * (1000000.0f / (float)chosenAgeUs);
  return true;
}
void updateVelocityEstimate(uint32_t nowUs) {
  float windowVelocity = 0.0f;
  if (!velocityFromWindow(nowUs, windowVelocity)) { encoderVelocityValid = false; omega = 0.0f; return; }
  if (!encoderVelocityValid || lastVelocityUpdateUs == 0U) omega = windowVelocity;
  else {
    uint32_t dtUs = nowUs - lastVelocityUpdateUs;
    float alpha = 1.0f - expf(-(float)dtUs / (float)VELOCITY_FILTER_TAU_US);
    omega += alpha * (windowVelocity - omega);
  }
  lastVelocityUpdateUs = nowUs; encoderVelocityValid = true;
}
bool encoderPositionFresh() { return encoderPrimed && ((uint32_t)(micros() - lastGoodUs) <= ENCODER_FRESH_US); }
bool encoderMotionReady() { return encoderPositionFresh() && encoderVelocityValid; }
void updateEncoder() {
  uint32_t nowUs = micros();
  if (!samplerScheduled) { samplerScheduled = true; nextSampleDueUs = nowUs; }
  if ((int32_t)(nowUs - nextSampleDueUs) < 0) return;
  nextSampleDueUs += ENCODER_SAMPLE_PERIOD_US;
  if ((int32_t)(nowUs - nextSampleDueUs) >= (int32_t)ENCODER_SAMPLE_PERIOD_US) nextSampleDueUs = nowUs + ENCODER_SAMPLE_PERIOD_US;
  readRawSample();
  if (!encoderRead.ok) { invalidateVelocity(); return; }
  if (!encoderPrimed) { primeEncoder(encoderRead.raw, encoderRead.doneUs, false); return; }
  uint32_t dtGoodUs = encoderRead.doneUs - lastGoodUs;
  if (dtGoodUs == 0U || dtGoodUs > ENCODER_MAX_GOOD_GAP_US) { primeEncoder(encoderRead.raw, encoderRead.doneUs, true); return; }
  int16_t delta = (int16_t)encoderRead.raw - (int16_t)lastGoodRaw;
  if (delta > 2048) delta -= 4096;
  if (delta < -2048) delta += 4096;
  int32_t absDelta = delta < 0 ? -(int32_t)delta : (int32_t)delta;
  if (absDelta == 2048) { invalidateVelocity(); return; }
  int32_t maxAllowed = ENCODER_DELTA_MARGIN_COUNTS + (int32_t)ceilf(ENCODER_MAX_PLAUSIBLE_REV_S * 4096.0f * (float)dtGoodUs / 1000000.0f);
  if (maxAllowed > 2047) maxAllowed = 2047;
  if (absDelta > maxAllowed) { invalidateVelocity(); return; }
  encoderCountsMT += ENCODER_DIR_SIGN * delta;
  lastGoodRaw = encoderRead.raw; lastGoodUs = encoderRead.doneUs;
  angleDegMT = (double)encoderCountsMT * 360.0 / 4096.0;
  pushVelocityPoint(encoderRead.doneUs, encoderCountsMT);
  updateVelocityEstimate(encoderRead.doneUs);
}

float wheelAngleDeg() { double angle = fmod(angleDegMT, 360.0); if (angle < 0.0) angle += 360.0; return (float)angle; }
int wedgeAtAngle(float angle) { angle = fmodf(angle, 360.0f); if (angle < 0.0f) angle += 360.0f; return ((int)(angle / WEDGE_DEG)) % NUM_WEDGES; }
int currentWedge() { return wedgeAtAngle(wheelAngleDeg()); }
float forwardDistanceDeg(int dir, float fromAngle, float toAngle) { float distance = dir > 0 ? (toAngle - fromAngle) : (fromAngle - toAngle); distance = fmodf(distance, 360.0f); if (distance < 0.0f) distance += 360.0f; return distance; }
int32_t countsForDegrees(float degrees) { return (int32_t)lroundf(degrees * 4096.0f / 360.0f); }

void driverConfig() {
  driver.begin(); driver.I_scale_analog(false); driver.toff(4); driver.blank_time(24); driver.microsteps(MICROSTEPS);
  driver.en_spreadCycle(true); driver.pwm_autoscale(true); driver.rms_current(RMS_CURRENT_MA, 1.0f);
  driver.TCOOLTHRS(0); driver.semin(0); driver.semax(0); driver.iholddelay(0); driver.TPOWERDOWN(255);
}
void stopStepClock() { ledcWriteTone(PIN_STEP, 0); ledcWrite(PIN_STEP, 0); }
uint32_t setStepClockHz(uint32_t requestedHz) { if (requestedHz < 1U) { stopStepClock(); return 0; } return ledcWriteTone(PIN_STEP, requestedHz); }
uint32_t setStepClockRevS(float wheelRevS) {
  if (wheelRevS <= 0.0f) { stopStepClock(); return 0; }
  float requested = wheelRevS * WHEEL_USTEPS_PER_REV;
  if (requested < 1.0f) requested = 1.0f; if (requested > 20000.0f) requested = 20000.0f;
  return setStepClockHz((uint32_t)lroundf(requested));
}
void driverDisableOutputs() { digitalWrite(PIN_EN, HIGH); }
void driverFreewheel() { stopStepClock(); driver.freewheel(1); driver.ihold(0); driverDisableOutputs(); }
void driverActive(uint16_t currentMa) { driver.freewheel(0); driver.rms_current(currentMa, 1.0f); digitalWrite(PIN_EN, LOW); }
bool setMotorDirectionForEncoderSign(int desiredEncoderSign) {
  if (!motorDirectionCalibrated || (dirHighEncoderSign != 1 && dirHighEncoderSign != -1)) return false;
  bool dirHigh = desiredEncoderSign == dirHighEncoderSign;
  digitalWrite(PIN_DIR, dirHigh ? HIGH : LOW); return true;
}

uint32_t directionCalibrationSignature() {
  uint32_t signature = 2166136261UL;
  auto mix = [&signature](uint32_t value) { signature ^= value; signature *= 16777619UL; };
  mix(DIRECTION_CAL_VERSION); mix((uint32_t)(ENCODER_DIR_SIGN + 2)); mix((uint32_t)MICROSTEPS);
  mix((uint32_t)MOTOR_FULLSTEPS); mix((uint32_t)lroundf(GEAR_RATIO * 1000.0f)); mix((uint32_t)PIN_DIR); mix((uint32_t)PIN_STEP);
  return signature;
}
void invalidateDirectionCalibration(const char* reason) {
  motorDirectionCalibrated = false; dirHighEncoderSign = 0;
  preferences.putBool("dir_ok", false); preferences.putInt("dir_hi_sign", 0); preferences.putUInt("dir_sig", 0U);
  Serial.printf("# direction calibration cleared: %s\n", reason);
}
bool directionProbeStartIsSafe() {
  int wedge = currentWedge();
  if (isDare(wedge) || isDare((wedge + NUM_WEDGES - 1) % NUM_WEDGES) || isDare((wedge + 1) % NUM_WEDGES)) return false;
  float within = fmodf(wheelAngleDeg(), WEDGE_DEG); float edgeDistance = fminf(within, WEDGE_DEG - within);
  return edgeDistance >= 10.0f;
}
void startDirectionProbe() {
  if (!encoderPositionFresh()) { Serial.println(F("# DIR PROBE refused: encoder position is not fresh")); return; }
  if (mode != IDLE && mode != DONE && mode != FAULT) { Serial.println(F("# DIR PROBE refused: wheel controller is busy")); return; }
  if (!directionProbeStartIsSafe()) { Serial.println(F("# DIR PROBE refused: center pointer in safe wedge 3 or 7-11")); return; }
  driverFreewheel(); digitalWrite(PIN_DIR, HIGH); setStepClockHz(DIR_PROBE_SPEED_HZ);
  directionProbeStartedMs = millis(); directionProbeStartCounts = encoderCountsMT; directionProbeOutputsEnabled = false; mode = DIR_PROBE;
  Serial.println(F("# DIR PROBE: DIR=HIGH pulse train started; keep hands clear"));
}
void serviceDirectionProbe() {
  uint32_t elapsedMs = millis() - directionProbeStartedMs;
  if (!directionProbeOutputsEnabled) {
    if (elapsedMs < SYNC_FIELD_LEAD_MS) return;
    driverActive(DIR_PROBE_CURRENT_MA); directionProbeOutputsEnabled = true; directionProbeStartCounts = encoderCountsMT;
  }
  int32_t deltaCounts = encoderCountsMT - directionProbeStartCounts; float deltaDeg = (float)deltaCounts * 360.0f / 4096.0f;
  if (fabsf(deltaDeg) >= DIR_PROBE_TRAVEL_DEG) {
    driverFreewheel();
    if (fabsf(deltaDeg) < DIR_PROBE_MIN_DEG) { invalidateDirectionCalibration("probe movement too small"); mode = FAULT; return; }
    dirHighEncoderSign = deltaDeg > 0.0f ? 1 : -1; motorDirectionCalibrated = true;
    preferences.putInt("dir_hi_sign", dirHighEncoderSign); preferences.putUInt("dir_sig", directionCalibrationSignature()); preferences.putBool("dir_ok", true);
    mode = DONE; Serial.printf("# DIR PROBE PASS: DIR=HIGH produces encoder dir=%+d; takeover enabled\n", dirHighEncoderSign); return;
  }
  if (elapsedMs >= DIR_PROBE_TIMEOUT_MS) { driverFreewheel(); invalidateDirectionCalibration("probe timeout"); mode = FAULT; }
}

float predictStopAngle() {
  int dir = omega >= 0.0f ? 1 : -1; float speed = fabsf(omega); if (speed < 0.001f) return wheelAngleDeg();
  float c = dir > 0 ? cwC : ccwC; float b = dir > 0 ? cwB : ccwB; float speedRadS = speed * TWO_PI;
  float travelRad = (1.0f / b) * speedRadS - (c / (b * b)) * logf(1.0f + b * speedRadS / c);
  float stop = wheelAngleDeg() + (float)dir * travelRad * RAD_TO_DEG; stop = fmodf(stop, 360.0f); if (stop < 0.0f) stop += 360.0f; return stop;
}
bool predictedStopCouldBeDare(float predictedAngle) {
  int wedge = wedgeAtAngle(predictedAngle); if (isDare(wedge)) return true;
  float within = fmodf(predictedAngle, WEDGE_DEG);
  if (within < PREDICTION_DARE_MARGIN_DEG && isDare((wedge + NUM_WEDGES - 1) % NUM_WEDGES)) return true;
  if (within > WEDGE_DEG - PREDICTION_DARE_MARGIN_DEG && isDare((wedge + 1) % NUM_WEDGES)) return true;
  return false;
}
float requiredRunwayDeg(float speedRevS) { float stoppingRev = (speedRevS * speedRevS) / (2.0f * TAKEOVER_DECEL_REV_S2); return stoppingRev * 360.0f + TAKEOVER_RUNWAY_MARGIN_DEG; }
// Uniform-random choice among ALL reachable safe wedges. Nearest-first was a
// deterministic pattern (same entry conditions always produced the same
// wedge); a uniform pick among qualifying candidates removes that tell while
// keeping the identical runway feasibility checks.
bool chooseSafeTarget(int dir, float currentAngle, float minimumRunwayDeg, float& targetAngle, float& runwayDeg) {
  float candTarget[NUM_WEDGES]; float candRunway[NUM_WEDGES]; uint8_t candCount = 0;
  for (int wedge = 0; wedge < NUM_WEDGES; ++wedge) {
    if (isDare(wedge)) continue;
    float jitterLimit = fminf(SAFE_TARGET_JITTER_DEG, WEDGE_DEG * 0.5f - SAFE_EDGE_MARGIN_DEG); float jitter = 0.0f;
    if (jitterLimit > 0.0f) { long centiLimit = lroundf(jitterLimit * 100.0f); jitter = (float)random(-centiLimit, centiLimit + 1) / 100.0f; }
    float target = ((float)wedge + 0.5f) * WEDGE_DEG + jitter; float forward = forwardDistanceDeg(dir, currentAngle, target);
    if (forward < minimumRunwayDeg) forward += 360.0f;  // VARIANT: extend by one lap instead of excluding (kills wedge starvation)
    if (forward > TAKEOVER_MAX_RUNWAY_DEG) continue;
    candTarget[candCount] = target; candRunway[candCount] = forward; ++candCount;
  }
  if (candCount == 0U) return false;
  uint8_t pick = (uint8_t)random((long)candCount);
  targetAngle = candTarget[pick]; runwayDeg = candRunway[pick]; return true;
}

// --- Friction auto-calibration -------------------------------------------
// Every free coast is a measurement of this wheel's own physics. Samples of
// (t, omega) collected while the coils float are fitted to
//   domega/dt = -(c + b*omega)
// by least squares over stride-spaced pairs, then blended into the running
// per-direction coefficients and persisted to NVS. The seeded values only
// matter until the first few real spins have been observed.
void resetFrictionCapture(int dir) {
  fitSampleCount = 0; fitDir = dir; fitLastSampleMs = 0; fitFinished = false;
}
void captureFrictionSample(uint32_t nowMs) {
  if (fitFinished || fitDir == 0 || fitSampleCount >= FIT_MAX_SAMPLES) return;
  if (!encoderVelocityValid) return;
  float forward = omega * (float)fitDir;
  if (forward < FIT_MIN_SPEED_REV_S || forward > FIT_MAX_SPEED_REV_S) return;
  if (fitSampleCount > 0U && nowMs - fitLastSampleMs < FIT_SAMPLE_PERIOD_MS) return;
  fitSamples[fitSampleCount].ms = nowMs;
  fitSamples[fitSampleCount].omegaRadS = forward * TWO_PI;
  ++fitSampleCount; fitLastSampleMs = nowMs;
}
void finishFrictionCapture(const char* reason) {
  if (fitFinished) return;
  fitFinished = true;
  if (fitDir == 0 || fitSampleCount < FIT_MIN_SAMPLES) return;
  float span = fitSamples[0].omegaRadS - fitSamples[fitSampleCount - 1U].omegaRadS;
  if (span < FIT_MIN_SPAN_RAD_S) return;
  float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f; int pairs = 0;
  for (uint8_t i = 0; i + FIT_PAIR_STRIDE < fitSampleCount; ++i) {
    uint8_t j = i + FIT_PAIR_STRIDE;
    float dtS = (float)(fitSamples[j].ms - fitSamples[i].ms) / 1000.0f;
    if (dtS < 0.2f || dtS > 3.0f) continue;
    float dropRadS = fitSamples[i].omegaRadS - fitSamples[j].omegaRadS;
    if (dropRadS <= 0.0f) continue;
    float alpha = dropRadS / dtS;
    float x = 0.5f * (fitSamples[i].omegaRadS + fitSamples[j].omegaRadS);
    sx += x; sy += alpha; sxx += x * x; sxy += x * alpha; ++pairs;
  }
  if (pairs < (int)(FIT_MIN_SAMPLES - FIT_PAIR_STRIDE)) return;
  float denom = (float)pairs * sxx - sx * sx;
  if (fabsf(denom) < 1e-3f) return;
  float fitB = ((float)pairs * sxy - sx * sy) / denom;
  float fitC = (sy - fitB * sx) / (float)pairs;
  if (fitC < FIT_C_MIN || fitC > FIT_C_MAX || fitB < FIT_B_MIN || fitB > FIT_B_MAX) {
    Serial.printf("SPIN#%lu FRICTION_REJECT dir=%+d fitC=%.4f fitB=%.4f pairs=%d reason=%s\n",
                  (unsigned long)activeSpinNumber, fitDir, fitC, fitB, pairs, reason);
    return;
  }
  float keep = 1.0f - FIT_BLEND;
  if (fitDir > 0) {
    cwC = keep * cwC + FIT_BLEND * fitC; cwB = keep * cwB + FIT_BLEND * fitB; ++cwFitCount;
    preferences.putFloat("cwC", cwC); preferences.putFloat("cwB", cwB); preferences.putUShort("cwN", cwFitCount);
    Serial.printf("SPIN#%lu FRICTION dir=+1 fitC=%.4f fitB=%.4f pairs=%d -> cwC=%.4f cwB=%.4f fits=%u reason=%s\n",
                  (unsigned long)activeSpinNumber, fitC, fitB, pairs, cwC, cwB, cwFitCount, reason);
  } else {
    ccwC = keep * ccwC + FIT_BLEND * fitC; ccwB = keep * ccwB + FIT_BLEND * fitB; ++ccwFitCount;
    preferences.putFloat("ccwC", ccwC); preferences.putFloat("ccwB", ccwB); preferences.putUShort("ccwN", ccwFitCount);
    Serial.printf("SPIN#%lu FRICTION dir=-1 fitC=%.4f fitB=%.4f pairs=%d -> ccwC=%.4f ccwB=%.4f fits=%u reason=%s\n",
                  (unsigned long)activeSpinNumber, fitC, fitB, pairs, ccwC, ccwB, ccwFitCount, reason);
  }
}
void printFrictionStatus() {
  Serial.printf("# friction cw: c=%.4f b=%.4f fits=%u | ccw: c=%.4f b=%.4f fits=%u\n",
                cwC, cwB, cwFitCount, ccwC, ccwB, ccwFitCount);
}
void resetFrictionModel() {
  cwC = 0.30f; cwB = 0.15f; ccwC = 0.30f; ccwB = 0.15f; cwFitCount = 0; ccwFitCount = 0;
  preferences.remove("cwC"); preferences.remove("cwB"); preferences.remove("cwN");
  preferences.remove("ccwC"); preferences.remove("ccwB"); preferences.remove("ccwN");
  Serial.println(F("# friction model reset to seeds"));
}
// --------------------------------------------------------------------------

// Window prediction: the stop prediction captured the first time the wheel
// decelerates into the takeover decision band. For unsteered spins this is
// compared against the actual landing to measure real prediction error
// without influencing the steer/leave decision in any way.
bool activeSpinWindowPredValid = false;
float activeSpinWindowPredAngle = 0.0f;

void startSpinEvent(int confirmedDir) {
  finishFrictionCapture("respin");
  ++spinCounter; activeSpinNumber = spinCounter; spinDir = confirmedDir; activeSpinPeakOmega = fabsf(omega);
  activeSpinSteered = false; activeSpinHasDecision = false; activeSpinPredAngle = 0.0f; activeSpinPredWedge = -1; activeSpinTargetWedge = -1; settleStartMs = 0;
  activeSpinWindowPredValid = false; activeSpinWindowPredAngle = 0.0f;
  resetFrictionCapture(confirmedDir);
  Serial.printf("SPIN#%lu START dir=%+d omega=%.3f\n", (unsigned long)activeSpinNumber, spinDir, omega);
}
void recordDecision(float predictedAngle, int predictedWedge, bool steer, int targetWedge) {
  activeSpinHasDecision = true; activeSpinPredAngle = predictedAngle; activeSpinPredWedge = predictedWedge; activeSpinSteered = steer; activeSpinTargetWedge = targetWedge;
  if (steer) Serial.printf("SPIN#%lu DECISION omega=%.3f curWedge=%d predAngle=%.1f predWedge=%d action=STEER targetWedge=%d\n", (unsigned long)activeSpinNumber, omega, currentWedge(), predictedAngle, predictedWedge, targetWedge);
  else Serial.printf("SPIN#%lu DECISION omega=%.3f curWedge=%d predAngle=%.1f predWedge=%d action=LEAVE\n", (unsigned long)activeSpinNumber, omega, currentWedge(), predictedAngle, predictedWedge);
}
void printLandedEvent() {
  int wedge = currentWedge();
  Serial.printf("SPIN#%lu LANDED wedge=%d angle=%.1f isDare=%d steered=%d predWedgeWas=%d predAngleWas=%.1f targetWedgeWas=%d\n", (unsigned long)activeSpinNumber, wedge, wheelAngleDeg(), isDare(wedge) ? 1 : 0, activeSpinSteered ? 1 : 0, activeSpinPredWedge, activeSpinPredAngle, activeSpinTargetWedge);
  if (!activeSpinSteered && activeSpinWindowPredValid) {
    float errDeg = fmodf(wheelAngleDeg() - activeSpinWindowPredAngle + 540.0f, 360.0f) - 180.0f;
    Serial.printf("SPIN#%lu PRED_ERR deg=%+.1f windowPred=%.1f(w%d) landed=%.1f(w%d)\n",
                  (unsigned long)activeSpinNumber, errDeg, activeSpinWindowPredAngle,
                  wedgeAtAngle(activeSpinWindowPredAngle), wheelAngleDeg(), wedge);
  }
  if (isDare(wedge)) Serial.println(F("# UNSAFE FINAL: no post-stop recovery was attempted; inspect prediction/sync log"));
}

int32_t takeoverTravelledCounts() { return takeoverDir * (encoderCountsMT - takeoverStartCounts); }
float takeoverRemainingDeg() { int32_t remainingCounts = takeoverTargetTravelCounts - takeoverTravelledCounts(); return (float)remainingCounts * 360.0f / 4096.0f; }
void releaseTakeover(const char* reason, bool fault) {
  float remaining = takeoverRemainingDeg(); float travelled = (float)takeoverTravelledCounts() * 360.0f / 4096.0f;
  driverFreewheel(); settleStartMs = 0; mode = fault ? FAULT : SETTLE;
  Serial.printf("SPIN#%lu TAKEOVER_END reason=%s travelledDeg=%.1f remainingDeg=%.1f omega=%.3f\n", (unsigned long)activeSpinNumber, reason, travelled, remaining, omega);
}
bool beginTakeover(float targetAngle, float runwayDeg) {
  if (!takeoverEnabled || !motorDirectionCalibrated || !encoderMotionReady()) return false;
  float forwardSpeed = omega * spinDir; if (forwardSpeed < TAKEOVER_MIN_REV_S) return false;
  takeoverDir = spinDir; if (!setMotorDirectionForEncoderSign(takeoverDir)) return false;
  takeoverTargetDeg = targetAngle; takeoverStartCounts = encoderCountsMT; takeoverTargetTravelCounts = countsForDegrees(runwayDeg);
  takeoverStartRevS = fminf(forwardSpeed, TAKEOVER_MAX_START_REV_S); takeoverCommandRevS = takeoverStartRevS;
  takeoverStateStartedMs = millis(); takeoverLastUpdateUs = micros(); takeoverOppositeStartMs = 0; takeoverSlipStartMs = 0; takeoverCurrentRaisedMs = 0;
  driverDisableOutputs(); driver.freewheel(0); setStepClockRevS(takeoverCommandRevS); mode = SYNC_FIELD;
  Serial.printf("SPIN#%lu SYNC_START dir=%+d dirPin=%d wheelRevS=%.3f commandRevS=%.3f targetAngle=%.1f runwayDeg=%.1f\n", (unsigned long)activeSpinNumber, takeoverDir, digitalRead(PIN_DIR), forwardSpeed, takeoverCommandRevS, takeoverTargetDeg, runwayDeg);
  return true;
}
void abortToFreeSpin(const char* reason) { driverFreewheel(); mode = FREE_SPIN; settleStartMs = 0; Serial.printf("SPIN#%lu ABORT_FREE reason=%s omega=%.3f\n", (unsigned long)activeSpinNumber, reason, omega); }
bool checkTakeoverSafety() {
  if (!encoderPositionFresh()) { releaseTakeover("encoder-stale", true); return false; }
  if (!encoderVelocityValid) return true;
  float forwardSpeed = omega * takeoverDir;
  if (forwardSpeed < -OPPOSITE_ABORT_REV_S) {
    if (takeoverOppositeStartMs == 0U) takeoverOppositeStartMs = millis();
    else if (millis() - takeoverOppositeStartMs >= OPPOSITE_ABORT_MS) { releaseTakeover("opposite-motion", true); return false; }
  } else takeoverOppositeStartMs = 0;
  float slip = forwardSpeed - takeoverCommandRevS;
  if (slip > SLIP_ABORT_REV_S) {
    if (takeoverSlipStartMs == 0U) takeoverSlipStartMs = millis();
    else if (millis() - takeoverSlipStartMs >= SLIP_ABORT_MS) { releaseTakeover("phase-slip", true); return false; }
  } else takeoverSlipStartMs = 0;
  return true;
}
void serviceSyncField() {
  if (!checkTakeoverSafety() || !encoderMotionReady()) return;
  float forwardSpeed = omega * takeoverDir;
  if (forwardSpeed <= TAKEOVER_EARLY_STOP_REV_S) { releaseTakeover("stopped-before-sync", false); return; }
  takeoverCommandRevS = fminf(forwardSpeed, TAKEOVER_MAX_START_REV_S); setStepClockRevS(takeoverCommandRevS);
  if (millis() - takeoverStateStartedMs < SYNC_FIELD_LEAD_MS) return;
  driverActive(SYNC_CURRENT_MA); takeoverStateStartedMs = millis(); takeoverStartCounts = encoderCountsMT;
  takeoverTargetTravelCounts = countsForDegrees(forwardDistanceDeg(takeoverDir, wheelAngleDeg(), takeoverTargetDeg)); mode = SYNC_CAPTURE;
  if (debugLog) Serial.printf("# SYNC outputs enabled at %u mA, omega=%.3f\n", SYNC_CURRENT_MA, omega);
}
void serviceSyncCapture() {
  if (!checkTakeoverSafety() || !encoderMotionReady()) return;
  float forwardSpeed = omega * takeoverDir;
  if (forwardSpeed <= TAKEOVER_EARLY_STOP_REV_S) { releaseTakeover("stopped-during-sync", false); return; }
  takeoverCommandRevS = fminf(forwardSpeed, TAKEOVER_MAX_START_REV_S); setStepClockRevS(takeoverCommandRevS);
  if (millis() - takeoverStateStartedMs < SYNC_LOW_CURRENT_MS) return;
  driver.rms_current(TAKEOVER_INITIAL_CURRENT_MA, 1.0f); takeoverCurrentRaisedMs = millis(); takeoverLastUpdateUs = micros(); mode = TAKEOVER;
  Serial.printf("SPIN#%lu SYNC_LOCKED omega=%.3f commandRevS=%.3f currentMa=%u\n", (unsigned long)activeSpinNumber, omega, takeoverCommandRevS, TAKEOVER_INITIAL_CURRENT_MA);
}
void serviceTakeover() {
  if (!checkTakeoverSafety() || !encoderMotionReady()) return;
  uint32_t nowUs = micros(); float dt = (float)(nowUs - takeoverLastUpdateUs) / 1000000.0f;
  if (dt <= 0.0f) return; if (dt > 0.050f) dt = 0.050f; takeoverLastUpdateUs = nowUs;
  float remainingDeg = takeoverRemainingDeg(); float forwardSpeed = omega * takeoverDir;
  if (remainingDeg <= TAKEOVER_RELEASE_REMAIN_DEG && forwardSpeed <= TAKEOVER_RELEASE_REV_S) { releaseTakeover("target-release", false); return; }
  if (remainingDeg < -2.0f) { releaseTakeover("target-passed", false); return; }
  if (forwardSpeed <= TAKEOVER_EARLY_STOP_REV_S && remainingDeg > TAKEOVER_EARLY_STOP_REMAIN_DEG) { releaseTakeover("early-stop", false); return; }
  float usableRemainingDeg = fmaxf(remainingDeg - TAKEOVER_RELEASE_REMAIN_DEG, 0.0f);
  float profileRevS = sqrtf(2.0f * TAKEOVER_DECEL_REV_S2 * (usableRemainingDeg / 360.0f));
  if (profileRevS < TAKEOVER_RELEASE_REV_S) profileRevS = TAKEOVER_RELEASE_REV_S;
  float maxDrop = TAKEOVER_DECEL_REV_S2 * dt;
  if (takeoverCommandRevS > profileRevS) takeoverCommandRevS = fmaxf(profileRevS, takeoverCommandRevS - maxDrop);
  setStepClockRevS(takeoverCommandRevS);
  if (takeoverCurrentRaisedMs != 0U && millis() - takeoverCurrentRaisedMs >= TAKEOVER_CURRENT_RAMP_MS) { driver.rms_current(TAKEOVER_BRAKE_CURRENT_MA, 1.0f); takeoverCurrentRaisedMs = 0; }
  static uint32_t lastLogMs = 0;
  if (debugLog && millis() - lastLogMs >= 100U) {
    lastLogMs = millis();
    Serial.printf("# TK dir=%+d angle=%.1f target=%.1f remaining=%.1f omega=%.3f cmd=%.3f slip=%.3f current=%u\n", takeoverDir, wheelAngleDeg(), takeoverTargetDeg, remainingDeg, omega, takeoverCommandRevS, forwardSpeed - takeoverCommandRevS, takeoverCurrentRaisedMs == 0U ? TAKEOVER_BRAKE_CURRENT_MA : TAKEOVER_INITIAL_CURRENT_MA);
  }
}
bool tryBeginTakeover() {
  if (!takeoverEnabled || !motorDirectionCalibrated || activeSpinHasDecision || !encoderMotionReady()) return false;
  if (activeSpinPeakOmega < TAKEOVER_MIN_PEAK_REV_S) return false;
  float speed = fabsf(omega); if (speed > TAKEOVER_TRIGGER_REV_S || speed < TAKEOVER_MIN_REV_S) return false;
  if (speed > activeSpinPeakOmega * ENGAGE_AT_PEAK_FRACTION) return false;  // VARIANT: ~50% of spin
  if (omega * spinDir <= 0.0f) return false;
  float predictedAngle = predictStopAngle(); int predictedWedge = wedgeAtAngle(predictedAngle);
  if (!activeSpinWindowPredValid) { activeSpinWindowPredValid = true; activeSpinWindowPredAngle = predictedAngle; }
  float targetAngle = 0.0f, runwayDeg = 0.0f, minimumRunway = requiredRunwayDeg(speed);
  if (!chooseSafeTarget(spinDir, wheelAngleDeg(), minimumRunway, targetAngle, runwayDeg)) {
    if (debugLog) Serial.printf("# TK defer: no safe target cur=%.1f omega=%.3f minRunway=%.1f\n", wheelAngleDeg(), omega, minimumRunway);
    return false;
  }
  finishFrictionCapture("pre-steer");
  int targetWedge = wedgeAtAngle(targetAngle); recordDecision(predictedAngle, predictedWedge, true, targetWedge);
  if (!beginTakeover(targetAngle, runwayDeg)) { activeSpinHasDecision = false; activeSpinSteered = false; activeSpinTargetWedge = -1; return false; }
  return true;
}

const char* modeName(Mode value) {
  switch (value) {
    case IDLE: return "IDLE"; case FREE_SPIN: return "FREE_SPIN"; case SYNC_FIELD: return "SYNC_FIELD"; case SYNC_CAPTURE: return "SYNC_CAPTURE";
    case TAKEOVER: return "TAKEOVER"; case SETTLE: return "SETTLE"; case DIR_PROBE: return "DIR_PROBE"; case DONE: return "DONE"; case FAULT: return "FAULT"; default: return "?";
  }
}
void printHelp() {
  Serial.println(F("\n=== PRIZE WHEEL: NO-PREDICT VARIANT - engages EVERY spin at ~50% decay, uniform random safe wedge ===\n z  set current pointer position as wedge-0 boundary and save (wheel at rest)\n p  attended DIR=HIGH direction probe (safe wedge center only)\n s  status\n v  toggle verbose takeover logging\n e  toggle automatic takeover\n f  print friction model (auto-calibrated from free coasts)\n F  reset friction model to seed values\n x  clear stored motor direction calibration\n m  print dare mask\n ?  help\nThere is no post-stop recovery move in this build."));
}
void printStatus() {
  float predicted = predictStopAngle();
  Serial.printf("# mode=%s angle=%.2f wedge=%d omega=%.4f pred=%.1f(w%d) encoderPos=%s velocity=%s takeover=%d dirCal=%d dirHighSign=%+d rawZero=%u\n", modeName(mode), wheelAngleDeg(), currentWedge(), omega, predicted, wedgeAtAngle(predicted), encoderPositionFresh() ? "FRESH" : "STALE", encoderVelocityValid ? "VALID" : "REPRIME", takeoverEnabled ? 1 : 0, motorDirectionCalibrated ? 1 : 0, dirHighEncoderSign, rawZero);
}
void handleSerial() {
  if (!Serial.available()) return; char command = (char)Serial.read();
  switch (command) {
    case 'z':
      if (!encoderPositionFresh()) Serial.println(F("# wedge calibration ignored: encoder position stale"));
      else if (mode != IDLE && mode != DONE && mode != FAULT) Serial.println(F("# wedge calibration ignored: wheel controller is busy"));
      else if (encoderVelocityValid && fabsf(omega) > STILL_REV_S) Serial.println(F("# wedge calibration ignored: wheel must be at rest"));
      else {
        rawZero = lastGoodRaw;
        preferences.putUShort("rawZero", rawZero);
        primeEncoder(lastGoodRaw, lastGoodUs, false);
        Serial.printf("# wedge-0 anchor saved: rawZero=%u (angle now %.2f, wedge %d)\n", rawZero, wheelAngleDeg(), currentWedge());
      }
      break;
    case 'p': startDirectionProbe(); break;
    case 's': printStatus(); break;
    case 'v': debugLog = !debugLog; Serial.printf("# verbose=%d\n", debugLog ? 1 : 0); break;
    case 'e':
      takeoverEnabled = !takeoverEnabled;
      if (!takeoverEnabled && (mode == SYNC_FIELD || mode == SYNC_CAPTURE || mode == TAKEOVER)) releaseTakeover("disabled-by-user", false);
      Serial.printf("# takeoverEnabled=%d\n", takeoverEnabled ? 1 : 0); break;
    case 'f': printFrictionStatus(); break;
    case 'F':
      if (mode == IDLE || mode == DONE || mode == FAULT) resetFrictionModel();
      else Serial.println(F("# cannot reset friction model while controller is busy"));
      break;
    case 'x':
      if (mode == IDLE || mode == DONE || mode == FAULT) invalidateDirectionCalibration("serial command");
      else Serial.println(F("# cannot clear direction calibration while controller is busy"));
      break;
    case 'm':
      Serial.printf("# dareMask=0x%03X; dare wedges:", dareMask); for (int wedge = 0; wedge < NUM_WEDGES; ++wedge) if (isDare(wedge)) Serial.printf(" %d", wedge); Serial.println(); break;
    case '?': printHelp(); break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200); delay(300); randomSeed(esp_random()); preferences.begin("prizewheel", false);
  rawZero = preferences.getUShort("rawZero", 3807);
  preferences.remove("wedge0"); preferences.remove("wedge0_ok");
  cwC = preferences.getFloat("cwC", 0.30f); cwB = preferences.getFloat("cwB", 0.15f);
  ccwC = preferences.getFloat("ccwC", 0.30f); ccwB = preferences.getFloat("ccwB", 0.15f);
  cwFitCount = preferences.getUShort("cwN", 0); ccwFitCount = preferences.getUShort("ccwN", 0);
  uint32_t storedSignature = preferences.getUInt("dir_sig", 0U); dirHighEncoderSign = preferences.getInt("dir_hi_sign", 0);
  motorDirectionCalibrated = preferences.getBool("dir_ok", false) && storedSignature == directionCalibrationSignature() && (dirHighEncoderSign == 1 || dirHighEncoderSign == -1);
  if (!motorDirectionCalibrated) dirHighEncoderSign = 0;
  Wire.begin(PIN_SDA, PIN_SCL); Wire.setClock(400000); Wire.setTimeOut(3);
  if (readRawSample()) { primeEncoder(encoderRead.raw, encoderRead.doneUs, false); samplerScheduled = true; nextSampleDueUs = encoderRead.doneUs + ENCODER_SAMPLE_PERIOD_US; Serial.printf("# AS5600 primed raw=%u i2c=%u us\n", encoderRead.raw, encoderRead.i2cUs); }
  else Serial.printf("# AS5600 initial read failed tx=%u requested=%u available=%u\n", encoderRead.txStatus, encoderRead.requested, encoderRead.available);
  pinMode(PIN_EN, OUTPUT); pinMode(PIN_DIR, OUTPUT); pinMode(PIN_STEP, OUTPUT); digitalWrite(PIN_EN, LOW); digitalWrite(PIN_DIR, LOW); digitalWrite(PIN_STEP, LOW);
  TMC_SERIAL.begin(115200, SERIAL_8N1, TMC_RX_PIN, TMC_TX_PIN); driverConfig();
  bool ledcReady = ledcAttach(PIN_STEP, 100U, 8U); Serial.printf("# LEDC step clock attach=%d\n", ledcReady ? 1 : 0);
  uint8_t connection = driver.test_connection(); Serial.printf("# TMC UART test_connection (0=OK): %u\n", connection);
  Serial.printf("# direction calibration=%s dirHighSign=%+d signature=0x%08lX\n", motorDirectionCalibrated ? "VALID" : "REQUIRED", dirHighEncoderSign, (unsigned long)directionCalibrationSignature());
  Serial.printf("# frame: label-true static, rawZero=%u (wedge-0 leading edge)\n", rawZero);
  printFrictionStatus();
  printHelp(); driverFreewheel(); mode = IDLE;
}

void loop() {
  updateEncoder(); handleSerial(); bool sensorReady = encoderMotionReady(); uint32_t nowMs = millis();
  int32_t confirmTravelCounts = countsForDegrees(SPIN_CONFIRM_TRAVEL_DEG); int32_t cancelBacktrackCounts = countsForDegrees(SPIN_CANCEL_BACKTRACK_DEG); bool spinConfirmed = false;
  bool canArmSpin = mode == IDLE || mode == DONE || mode == SETTLE || mode == FAULT;
  if (!sensorReady || !canArmSpin) spinCandidateMs = 0;
  else if (spinCandidateMs == 0U) {
    if (fabsf(omega) >= SPIN_DETECT_REV_S) { spinCandidateMs = nowMs; spinCandidateStartCounts = encoderCountsMT; spinCandidateDir = omega >= 0.0f ? 1 : -1; }
  } else {
    int32_t signedTravel = spinCandidateDir * (encoderCountsMT - spinCandidateStartCounts);
    if (signedTravel < -cancelBacktrackCounts || nowMs - spinCandidateMs > SPIN_CONFIRM_TIMEOUT_MS) spinCandidateMs = 0;
    else if (nowMs - spinCandidateMs >= SPIN_CONFIRM_MS && signedTravel >= confirmTravelCounts) { spinConfirmed = true; spinCandidateMs = 0; }
  }
  bool guestOverrideMoving = sensorReady && fabsf(omega) >= GUEST_OVERRIDE_REV_S;
  if (guestOverrideMoving) { if (guestOverrideStartMs == 0U) guestOverrideStartMs = nowMs; } else guestOverrideStartMs = 0;
  bool guestOverride = guestOverrideMoving && nowMs - guestOverrideStartMs >= GUEST_OVERRIDE_MS;
  switch (mode) {
    case IDLE: case DONE: case FAULT:
      if (spinConfirmed) { driverFreewheel(); startSpinEvent(spinCandidateDir); mode = FREE_SPIN; }
      break;
    case FREE_SPIN:
      if (!sensorReady) break;
      if (fabsf(omega) > activeSpinPeakOmega) activeSpinPeakOmega = fabsf(omega);
      captureFrictionSample(nowMs);
      if (tryBeginTakeover()) break;
      if (fabsf(omega) <= STILL_REV_S) { finishFrictionCapture("coast-end"); settleStartMs = 0; mode = SETTLE; }
      break;
    case SYNC_FIELD:
      if (guestOverride) { abortToFreeSpin("guest-override-sync-field"); startSpinEvent(omega >= 0.0f ? 1 : -1); break; }
      serviceSyncField(); break;
    case SYNC_CAPTURE:
      if (guestOverride) { abortToFreeSpin("guest-override-sync-capture"); startSpinEvent(omega >= 0.0f ? 1 : -1); break; }
      serviceSyncCapture(); break;
    case TAKEOVER:
      if (guestOverride) { abortToFreeSpin("guest-override-takeover"); startSpinEvent(omega >= 0.0f ? 1 : -1); break; }
      serviceTakeover(); break;
    case SETTLE:
      if (spinConfirmed) { driverFreewheel(); startSpinEvent(spinCandidateDir); mode = FREE_SPIN; break; }
      if (!sensorReady) { settleStartMs = 0; break; }
      if (fabsf(omega) > STILL_REV_S) { settleStartMs = 0; if (!activeSpinHasDecision && tryBeginTakeover()) break; }
      else {
        if (settleStartMs == 0U) settleStartMs = nowMs;
        if (nowMs - settleStartMs >= SETTLE_MS) {
          if (!activeSpinHasDecision) { float predicted = predictStopAngle(); recordDecision(predicted, wedgeAtAngle(predicted), false, -1); }
          printLandedEvent(); driverFreewheel(); mode = DONE;
        }
      }
      break;
    case DIR_PROBE: serviceDirectionProbe(); break;
  }
}

