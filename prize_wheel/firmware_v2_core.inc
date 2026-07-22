/*
 * Prize Wheel firmware v2
 *
 * ESP32-WROOM-32 + BTT TMC2209 + NEMA17 + 2:1 belt + AS5600.
 *
 * Design goals:
 *   - freewheel during ordinary guest spins
 *   - one prediction decision per spin, with causal event logging
 *   - intervene only when the predicted stop overlaps dare wedge 1 or 5
 *   - continuous same-direction electronic drag; never command a reversal
 *   - encoder-grounded target and final landing verification
 *   - explicit emergency recovery, logged separately from the natural landing
 *   - persistent wedge-zero and motor-direction calibration
 *
 * Build target:
 *   arduino-cli compile --fqbn esp32:esp32:esp32 prize_wheel
 *   ESP32 Arduino core 3.3.10
 *   Libraries: TMCStepper, FastAccelStepper
 */

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>

/* ----------------------------- Hardware --------------------------------- */
#define TMC_SERIAL Serial2
#define TMC_RX_PIN 16
#define TMC_TX_PIN 17
#define TMC_ADDR   0b00
#define R_SENSE    0.11f

#define PIN_EN     25
#define PIN_STEP   26
#define PIN_DIR    27
#define PIN_SDA    21
#define PIN_SCL    22

#define AS5600_ADDR 0x36
#define AS5600_RAW  0x0C

static const uint16_t RMS_CURRENT_MA = 1450;
static const bool INVERT_DIR = true;

static const int MOTOR_FULL_STEPS = 200;
static const int MICROSTEPS = 16;
static const float GEAR_RATIO = 2.0f;
static const float WHEEL_USTEPS_PER_REV =
    MOTOR_FULL_STEPS * MICROSTEPS * GEAR_RATIO;  // 6400

static const int NUM_WEDGES = 12;
static const float WEDGE_DEG = 360.0f / NUM_WEDGES;
static const uint16_t DARE_MASK = (1u << 1) | (1u << 5);

// Wedges increase clockwise. Keep this fixed.
static const int ENCODER_DIR_SIGN = -1;

/* ------------------------------- Sensing -------------------------------- */
static const uint32_t ENCODER_SAMPLE_PERIOD_US = 1000;
static const uint32_t ENCODER_MAX_GOOD_GAP_US = 20000;
static const uint32_t ENCODER_FRESH_US = 50000;
static const float ENCODER_MAX_PLAUSIBLE_REV_S = 8.0f;
static const int32_t ENCODER_DELTA_MARGIN_COUNTS = 16;
static const uint32_t VELOCITY_WINDOW_US = 30000;
static const uint32_t VELOCITY_MIN_WINDOW_US = 20000;
static const uint32_t VELOCITY_FILTER_TAU_US = 25000;
static const uint8_t VELOCITY_HISTORY_LEN = 64;

/* --------------------------- Spin detection ----------------------------- */
static const float SPIN_DETECT_REV_S = 0.12f;
static const uint16_t SPIN_CONFIRM_MS = 60;
static const uint16_t SPIN_CONFIRM_TIMEOUT_MS = 1200;
static const float SPIN_CONFIRM_TRAVEL_DEG = 6.0f;
static const float SPIN_CANCEL_BACKTRACK_DEG = 2.0f;
static const float GUEST_OVERRIDE_REV_S = 0.80f;
static const uint16_t GUEST_OVERRIDE_CONFIRM_MS = 60;
static const float STILL_REV_S = 0.020f;
static const uint16_t SETTLE_MS = 500;

/* ---------------------- Prediction and intervention --------------------- */
static const float DECISION_REV_S = 0.22f;
static const float DECISION_MIN_REV_S = 0.065f;
static const float DECISION_MIN_PEAK_REV_S = 0.16f;
static const float PREDICTION_DARE_MARGIN_DEG = 8.0f;

// Seed values only. The per-spin logs are intended to support fitting these
// separately for clockwise and counter-clockwise motion.
static float cw_c = 0.30f;
static float cw_b = 0.15f;
static float ccw_c = 0.30f;
static float ccw_b = 0.15f;

/* -------------------------- Electronic drag ----------------------------- */
static const uint16_t PRECHARGE_CURRENT_MA = 100;
static const uint16_t DRAG_CURRENT_START_MA = 180;
static const uint16_t DRAG_CURRENT_MA = 300;
static const uint16_t HOLD_CURRENT_MA = 450;
static const uint16_t PRECHARGE_MS = 80;
static const uint16_t DRAG_CURRENT_RISE_MS = 220;

// FastAccelStepper acceleration is used as a slew limit for speed changes,
// not as a substitute for the wheel's physical braking model.
static const uint32_t DRAG_SLEW_SPS2 = 650;
static const float DRAG_ENTRY_FRACTION = 0.94f;
static const float DRAG_FINAL_FRACTION = 0.84f;
static const uint16_t DRAG_BLEND_MS = 700;
static const float DRAG_MAX_REV_S = 0.24f;
static const float DRAG_MIN_REV_S = 0.025f;
static const uint16_t DRAG_CONTROL_PERIOD_MS = 10;
static const float DRAG_MIN_RUNWAY_MARGIN_DEG = 22.0f;
static const float DRAG_MAX_RUNWAY_DEG = 180.0f;
static const float TARGET_CAPTURE_DEG = 4.0f;
static const float TARGET_OVERSHOOT_ABORT_DEG = 5.0f;
static const float OPPOSITE_ABORT_REV_S = 0.05f;
static const uint16_t OPPOSITE_ABORT_MS = 70;
static const float SPEEDUP_ABORT_REV_S = 0.025f;
static const uint16_t SPEEDUP_ABORT_MS = 80;
static const uint16_t DRAG_TIMEOUT_MS = 6000;
static const uint16_t SAFE_HOLD_AFTER_SETTLE_MS = 350;

/* -------------------------- Emergency recovery -------------------------- */
static const bool EMERGENCY_RECOVERY_DEFAULT = true;
static const uint16_t RECOVERY_PRECHARGE_MS = 80;
static const uint16_t RECOVERY_CURRENT_MA = 350;
static const uint32_t RECOVERY_SPEED_HZ = 110;
static const uint32_t RECOVERY_ACCEL_SPS2 = 300;
static const float RECOVERY_STOP_LEAD_DEG = 2.5f;
static const uint16_t RECOVERY_TIMEOUT_MS = 6500;
static const uint8_t RECOVERY_MAX_ATTEMPTS = 2;

/* -------------------------- Direction probe ----------------------------- */
static const uint16_t DIR_PROBE_CURRENT_MA = 350;
static const uint32_t DIR_PROBE_SPEED_HZ = 100;
static const uint32_t DIR_PROBE_ACCEL_SPS2 = 300;
static const int32_t DIR_PROBE_USTEPS = 160;  // 9 wheel degrees
static const float DIR_PROBE_MIN_DEG = 2.0f;
static const uint16_t DIR_PROBE_TIMEOUT_MS = 5000;

/* ------------------------------ Objects --------------------------------- */
TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, TMC_ADDR);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper* stepper = nullptr;
Preferences preferences;

/* ------------------------------- Helpers -------------------------------- */
static int32_t positiveMod(int32_t value, int32_t modulus) {
  int32_t result = value % modulus;
  if (result < 0) result += modulus;
  return result;
}

static float positiveFmod(float value, float modulus) {
  float result = fmodf(value, modulus);
  if (result < 0.0f) result += modulus;
  return result;
}

static int32_t countsForDegrees(float degrees) {
  return (int32_t)lroundf(degrees * 4096.0f / 360.0f);
}

static int32_t ustepsForDegrees(float degrees) {
  return (int32_t)lroundf(degrees * WHEEL_USTEPS_PER_REV / 360.0f);
}

static bool isDare(int wedge) {
  int normalized = positiveMod(wedge, NUM_WEDGES);
  return ((DARE_MASK >> normalized) & 1u) != 0;
}

static int wedgeAtAngle(float angleDeg) {
  return ((int)(positiveFmod(angleDeg, 360.0f) / WEDGE_DEG)) % NUM_WEDGES;
}

static float forwardDistanceDeg(int direction, float fromDeg, float toDeg) {
  float distance = direction > 0 ? toDeg - fromDeg : fromDeg - toDeg;
  return positiveFmod(distance, 360.0f);
}

/* ------------------------- Encoder and velocity ------------------------- */
struct EncoderRead {
  bool ok;
  uint16_t raw;
  uint32_t doneUs;
  uint16_t i2cUs;
  uint8_t txStatus;
  uint8_t requested;
  uint8_t available;
};

struct VelocityPoint {
  uint32_t timeUs;
  int32_t counts;
};

EncoderRead encoderRead;
VelocityPoint velocityHistory[VELOCITY_HISTORY_LEN];
uint8_t velocityHistoryHead = 0;
uint8_t velocityHistoryCount = 0;

bool encoderPrimed = false;
bool encoderVelocityValid = false;
uint16_t lastGoodRaw = 0;
uint32_t lastGoodUs = 0;
int32_t encoderCountsMT = 0;  // logical clockwise-positive multi-turn counts
float omega = 0.0f;          // clockwise-positive wheel rev/s
uint32_t lastVelocityUpdateUs = 0;

bool samplerScheduled = false;
uint32_t nextSampleDueUs = 0;

bool zeroCalibrated = false;
int32_t wedgeZeroLogicalCounts = 0;  // modulo 4096, stable across reboot

static int32_t logicalCountFromRaw(uint16_t raw) {
  return positiveMod(ENCODER_DIR_SIGN * (int32_t)raw, 4096);
}

static void clearVelocityHistory() {
  velocityHistoryHead = 0;
  velocityHistoryCount = 0;
  lastVelocityUpdateUs = 0;
}

static void pushVelocityPoint(uint32_t timeUs, int32_t counts) {
  velocityHistory[velocityHistoryHead].timeUs = timeUs;
  velocityHistory[velocityHistoryHead].counts = counts;
  velocityHistoryHead = (velocityHistoryHead + 1) % VELOCITY_HISTORY_LEN;
  if (velocityHistoryCount < VELOCITY_HISTORY_LEN) ++velocityHistoryCount;
}

static uint8_t historyIndexFromNewest(uint8_t samplesBack) {
  return (velocityHistoryHead + VELOCITY_HISTORY_LEN - 1 - samplesBack) %
         VELOCITY_HISTORY_LEN;
}

static void invalidateVelocity() {
  encoderVelocityValid = false;
  omega = 0.0f;
  clearVelocityHistory();
}

static bool readRawSample() {
  EncoderRead& read = encoderRead;
  uint32_t startedUs = micros();
  read.ok = false;
  read.raw = 0;
  read.txStatus = 0;
  read.requested = 0;
  read.available = 0;

  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(AS5600_RAW);
  read.txStatus = Wire.endTransmission(false);

  if (read.txStatus == 0) {
    size_t requested = Wire.requestFrom((uint8_t)AS5600_ADDR, (size_t)2);
    read.requested = requested > 255 ? 255 : (uint8_t)requested;
    int available = Wire.available();
    read.available = available > 255 ? 255 : (uint8_t)available;
    if (requested == 2 && available >= 2) {
      uint16_t hi = (uint16_t)Wire.read();
      uint16_t lo = (uint16_t)Wire.read();
      read.raw = ((hi << 8) | lo) & 0x0FFF;
      read.doneUs = micros();
      uint32_t duration = read.doneUs - startedUs;
      read.i2cUs = duration > 65535U ? 65535U : (uint16_t)duration;
      read.ok = true;
      return true;
    }
    while (Wire.available()) Wire.read();
  }

  read.doneUs = micros();
  uint32_t duration = read.doneUs - startedUs;
  read.i2cUs = duration > 65535U ? 65535U : (uint16_t)duration;
  return false;
}

static void primeEncoder(uint16_t raw, uint32_t doneUs, bool preserveNearestTurn) {
  int32_t logical = logicalCountFromRaw(raw);
  if (!encoderPrimed || !preserveNearestTurn) {
    encoderCountsMT = logical;
  } else {
    int32_t turnBase = encoderCountsMT - positiveMod(encoderCountsMT, 4096);
    int32_t candidate = turnBase + logical;
    while (candidate - encoderCountsMT > 2048) candidate -= 4096;
    while (candidate - encoderCountsMT < -2048) candidate += 4096;
    encoderCountsMT = candidate;
  }

  encoderPrimed = true;
  lastGoodRaw = raw;
  lastGoodUs = doneUs;
  invalidateVelocity();
  pushVelocityPoint(doneUs, encoderCountsMT);
}

static bool velocityFromWindow(uint32_t nowUs, float& velocityRevS) {
  if (velocityHistoryCount < 2) return false;

  int chosen = -1;
  uint32_t chosenAgeUs = 0;
  for (uint8_t back = 1; back < velocityHistoryCount; ++back) {
    uint8_t index = historyIndexFromNewest(back);
    uint32_t ageUs = nowUs - velocityHistory[index].timeUs;
    if (ageUs >= VELOCITY_WINDOW_US) {
      chosen = index;
      chosenAgeUs = ageUs;
      break;
    }
  }

  if (chosen < 0) {
    chosen = historyIndexFromNewest(velocityHistoryCount - 1);
    chosenAgeUs = nowUs - velocityHistory[chosen].timeUs;
  }
  if (chosenAgeUs < VELOCITY_MIN_WINDOW_US) return false;

  int32_t deltaCounts = encoderCountsMT - velocityHistory[chosen].counts;
  velocityRevS = ((float)deltaCounts / 4096.0f) *
                 (1000000.0f / (float)chosenAgeUs);
  return true;
}

static void updateVelocityEstimate(uint32_t nowUs) {
  float windowVelocity = 0.0f;
  if (!velocityFromWindow(nowUs, windowVelocity)) {
    encoderVelocityValid = false;
    omega = 0.0f;
    return;
  }

  if (!encoderVelocityValid || lastVelocityUpdateUs == 0) {
    omega = windowVelocity;
  } else {
    uint32_t dtUs = nowUs - lastVelocityUpdateUs;
    float alpha = 1.0f - expf(-(float)dtUs / (float)VELOCITY_FILTER_TAU_US);
    omega += alpha * (windowVelocity - omega);
  }
  lastVelocityUpdateUs = nowUs;
  encoderVelocityValid = true;
}

static bool encoderPositionFresh() {
  return encoderPrimed && ((uint32_t)(micros() - lastGoodUs) <= ENCODER_FRESH_US);
}

static bool encoderMotionReady() {
  return encoderPositionFresh() && encoderVelocityValid;
}

/* ----------------------- High-rate diagnostic capture ------------------- */
enum DiagFlag : uint8_t {
  DIAG_VALID = 1 << 0,
  DIAG_TX_ERROR = 1 << 1,
  DIAG_SHORT_READ = 1 << 2,
  DIAG_LONG_GAP = 1 << 3,
  DIAG_ALIAS = 1 << 4,
  DIAG_RATE = 1 << 5,
  DIAG_PRIMED = 1 << 6
};

struct DiagnosticSample {
  uint32_t doneUs;
  uint32_t dtGoodUs;
  int32_t counts;
  uint16_t raw;
  int16_t delta;
  uint16_t i2cUs;
  int16_t omegaMilli;
  uint8_t flags;
  uint8_t state;
};

static const uint16_t DIAG_CAPACITY = 2048;
DiagnosticSample diagnosticBuffer[DIAG_CAPACITY];
uint16_t diagnosticHead = 0;
uint16_t diagnosticCount = 0;
bool diagnosticCapture = false;
bool diagnosticSawMotion = false;

static uint8_t currentStateByte();

static void recordDiagnostic(uint32_t dtGoodUs, int16_t delta, uint8_t flags) {
  if (!diagnosticCapture) return;
  DiagnosticSample& sample = diagnosticBuffer[diagnosticHead];
  sample.doneUs = encoderRead.doneUs;
  sample.dtGoodUs = dtGoodUs;
  sample.counts = encoderCountsMT;
  sample.raw = encoderRead.ok ? encoderRead.raw : 0xFFFF;
  sample.delta = delta;
  sample.i2cUs = encoderRead.i2cUs;
  float scaled = omega * 1000.0f;
  if (scaled > 32767.0f) scaled = 32767.0f;
  if (scaled < -32768.0f) scaled = -32768.0f;
  sample.omegaMilli = (int16_t)lroundf(scaled);
  sample.flags = flags;
  sample.state = currentStateByte();

  diagnosticHead = (diagnosticHead + 1) % DIAG_CAPACITY;
  if (diagnosticCount < DIAG_CAPACITY) ++diagnosticCount;
}

static void updateEncoder() {
  uint32_t nowUs = micros();
  if (!samplerScheduled) {
    samplerScheduled = true;
    nextSampleDueUs = nowUs;
  }
  if ((int32_t)(nowUs - nextSampleDueUs) < 0) return;

  nextSampleDueUs += ENCODER_SAMPLE_PERIOD_US;
  if ((int32_t)(nowUs - nextSampleDueUs) >=
      (int32_t)ENCODER_SAMPLE_PERIOD_US) {
    nextSampleDueUs = nowUs + ENCODER_SAMPLE_PERIOD_US;
  }

  readRawSample();
  uint32_t dtGoodUs = encoderPrimed ? encoderRead.doneUs - lastGoodUs : 0;
  int16_t logicalDelta = 0;
  uint8_t flags = 0;

  if (!encoderRead.ok) {
    flags = encoderRead.txStatus != 0 ? DIAG_TX_ERROR : DIAG_SHORT_READ;
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, 0, flags);
    return;
  }

  if (!encoderPrimed) {
    primeEncoder(encoderRead.raw, encoderRead.doneUs, false);
    recordDiagnostic(0, 0, DIAG_PRIMED);
    return;
  }

  if (dtGoodUs == 0 || dtGoodUs > ENCODER_MAX_GOOD_GAP_US) {
    primeEncoder(encoderRead.raw, encoderRead.doneUs, true);
    recordDiagnostic(dtGoodUs, 0, DIAG_LONG_GAP | DIAG_PRIMED);
    return;
  }

  int16_t rawDelta = (int16_t)encoderRead.raw - (int16_t)lastGoodRaw;
  if (rawDelta > 2048) rawDelta -= 4096;
  if (rawDelta < -2048) rawDelta += 4096;

  int32_t absRawDelta = rawDelta < 0 ? -(int32_t)rawDelta : (int32_t)rawDelta;
  if (absRawDelta == 2048) {
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, 0, DIAG_ALIAS);
    return;
  }

  int32_t maxAllowed = ENCODER_DELTA_MARGIN_COUNTS + (int32_t)ceilf(
      ENCODER_MAX_PLAUSIBLE_REV_S * 4096.0f * (float)dtGoodUs / 1000000.0f);
  if (maxAllowed > 2047) maxAllowed = 2047;
  if (absRawDelta > maxAllowed) {
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, 0, DIAG_RATE);
    return;
  }

  logicalDelta = (int16_t)(ENCODER_DIR_SIGN * rawDelta);
  encoderCountsMT += logicalDelta;
  lastGoodRaw = encoderRead.raw;
  lastGoodUs = encoderRead.doneUs;
  pushVelocityPoint(encoderRead.doneUs, encoderCountsMT);
  updateVelocityEstimate(encoderRead.doneUs);
  if (encoderVelocityValid && fabsf(omega) > 0.10f) diagnosticSawMotion = true;
  recordDiagnostic(dtGoodUs, logicalDelta, DIAG_VALID);
}

static float wheelAngleDeg() {
  if (!zeroCalibrated) {
    return positiveMod(encoderCountsMT, 4096) * 360.0f / 4096.0f;
  }
  int32_t relative = positiveMod(encoderCountsMT - wedgeZeroLogicalCounts, 4096);
  return (float)relative * 360.0f / 4096.0f;
}

static int currentWedge() {
  return wedgeAtAngle(wheelAngleDeg());
}

