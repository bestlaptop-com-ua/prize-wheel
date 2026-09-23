/* ============================================================================
 * prize_wheel.ino - Prize wheel firmware
 *
 * Priority-1 sensing revision
 * ---------------------------
 * The AS5600 sample timestamp is taken after the final I2C byte, I2C failures
 * are explicit (never substituted with a stale angle), and velocity comes from
 * a time-windowed slope of accepted unwrapped samples.  Send 'd' before a
 * test spin to capture high-rate samples in RAM; the sketch prints them only
 * after the wheel has reached DONE, so Serial cannot perturb the measurement.
 *
 * Hardware: ESP32-WROOM-32, BTT TMC2209 V1.3, NEMA17, 2:1 GT2 belt, AS5600.
 * Build: ESP32 Arduino core 3.3.10, TMCStepper, FastAccelStepper, Wire.
 * ========================================================================== */

#include <Wire.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>

/* ----------------------------- PINS -------------------------------------- */
#define TMC_SERIAL   Serial2
#define TMC_RX_PIN   16
#define TMC_TX_PIN   17
#define TMC_ADDR     0b00
#define R_SENSE      0.11f

#define PIN_EN   25
#define PIN_STEP 26
#define PIN_DIR  27

#define PIN_SDA  21
#define PIN_SCL  22
#define AS5600_ADDR 0x36
#define AS5600_RAW  0x0C

/* --------------------------- MECHANICAL ---------------------------------- */
#define MOTOR_FULLSTEPS 200
#define MICROSTEPS      16
#define GEAR_RATIO      2.0f
const float WHEEL_USTEPS_PER_REV = MOTOR_FULLSTEPS * MICROSTEPS * GEAR_RATIO;
#define NUM_WEDGES 12
const float WEDGE_DEG = 360.0f / NUM_WEDGES;

/* --------------------------- DARE / SAFE --------------------------------- */
uint16_t dare_mask = (1 << 1) | (1 << 5);  // wedges 1 and 5 are avoided
inline bool isDare(int wedge) {
  return (dare_mask >> (wedge % NUM_WEDGES)) & 1;
}

/* --------------------------- TUNING -------------------------------------- */
#define RMS_CURRENT_MA     1450
bool INVERT_DIR = false;

#define TAKEOVER_REV_S     0.55f
#define SPIN_DETECT_REV_S  0.80f
#define ACCEL_CEILING_SPS2 1200
// Wedges must count CLOCKWISE. The AS5600 as mounted counts the other way, so we
// invert the accumulated encoder delta at its single source (updateEncoder). This
// flips angle, wedge index, velocity sign, and takeover direction together.
static const int ENCODER_DIR_SIGN = -1;
#define SETTLE_MS          500
#define STILL_REV_S        0.02f

/* Priority-1 sensing limits.  These are deliberately generous for a hand
 * spin, but far below an unphysical one-sample jump.  The RAM capture reports
 * observed delta and timing so these limits can be tightened from bench data. */
const uint32_t ENCODER_SAMPLE_PERIOD_US = 1000;
const uint32_t ENCODER_MAX_GOOD_GAP_US = 20000;
const uint32_t ENCODER_FRESH_US = 50000;
const float ENCODER_MAX_PLAUSIBLE_REV_S = 8.0f;
const int32_t ENCODER_DELTA_MARGIN_COUNTS = 16;
const uint32_t VELOCITY_WINDOW_US = 30000;
const uint32_t VELOCITY_MIN_WINDOW_US = 20000;
const uint32_t VELOCITY_FILTER_TAU_US = 25000;
const uint8_t VELOCITY_HISTORY_LEN = 64;

/* --------------------------- STATE --------------------------------------- */
TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, TMC_ADDR);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper* stepper = nullptr;

enum Mode : uint8_t { IDLE, FREE_SPIN, TAKEOVER, SETTLE, DONE };
Mode mode = IDLE;

double wedge0OffsetDeg = 0.0;
float omega = 0.0f;                 // signed wheel rev/s; valid only when flagged
bool debugLog = false;

uint32_t spinAboveMs = 0;
float takeoverTargetDeg = 0.0f;
int32_t takeoverTargetU = 0;
int takeoverDir = 1;
uint32_t settleT0 = 0;

// Seeded friction model.  Priority 2 will calibrate/replace this from data.
float cw_c = 0.30f, cw_b = 0.15f;
float ccw_c = 0.30f, ccw_b = 0.15f;

/* ---------------------- ENCODER / VELOCITY CORE -------------------------- */
struct EncoderRead {
  bool ok;
  uint16_t raw;
  uint32_t doneUs;       // immediately after final received byte (or failed op)
  uint16_t i2cUs;
  uint8_t txStatus;
  uint8_t requested;
  uint8_t available;
};

// One loop-owned transaction record.  Keeping it global also keeps the sketch
// a single .ino file: Arduino's automatic prototype generator never has to
// name this struct before its full definition.
EncoderRead encoderRead;

struct VelocityPoint {
  uint32_t timeUs;
  int32_t counts;
};

bool encoderPrimed = false;
bool encoderVelocityValid = false;
uint16_t lastGoodRaw = 0;
uint32_t lastGoodUs = 0;
int32_t encoderCountsMT = 0;
double angleDegMT = 0.0;
uint32_t lastVelocityUpdateUs = 0;
int8_t lastDeltaSign = 0;

VelocityPoint velocityHistory[VELOCITY_HISTORY_LEN];
uint8_t velocityHistoryHead = 0;
uint8_t velocityHistoryCount = 0;

bool samplerScheduled = false;
uint32_t nextSampleDueUs = 0;

/* ---------------------- PRIORITY-1 RAM DIAGNOSTICS ------------------------ */
enum EncoderDiagFlag : uint8_t {
  DIAG_VALID       = 1 << 0, // accepted into the unwrapped position
  DIAG_TX_ERROR    = 1 << 1,
  DIAG_SHORT_READ  = 1 << 2,
  DIAG_LONG_GAP    = 1 << 3,
  DIAG_ALIAS       = 1 << 4, // exact half-turn / unwrap ambiguity
  DIAG_RATE        = 1 << 5, // implausible counts for measured dt
  DIAG_PRIMED      = 1 << 6, // baseline reset; no velocity from this sample
  DIAG_DIR_FLIP    = 1 << 7  // accepted significant delta changed sign
};

struct DiagnosticSample {
  uint32_t doneUs;
  uint32_t dtGoodUs;
  int32_t counts;
  uint16_t raw;
  int16_t rawDiff;
  int16_t delta;
  uint16_t i2cUs;
  int16_t omegaMilliRevS;
  uint8_t flags;
  uint8_t txStatus;
  uint8_t requested;
  uint8_t available;
  uint8_t state; // low nibble Mode; bit 7 means motor takeover active
};

// About 86 KB of ESP32 RAM.  At 1 kHz it preserves the latest 3.072 seconds
// while leaving enough internal DRAM for FastAccelStepper's RMT machinery.
const uint16_t DIAG_CAPACITY = 3072;
DiagnosticSample diagnosticBuffer[DIAG_CAPACITY];
uint16_t diagnosticHead = 0;
uint16_t diagnosticCount = 0;
bool diagnosticWrapped = false;
bool diagnosticCapture = false;
bool diagnosticSawMotion = false;
uint32_t diagnosticStillSinceUs = 0;

int16_t milliRevS(float value) {
  float scaled = value * 1000.0f;
  if (scaled > 32767.0f) return 32767;
  if (scaled < -32768.0f) return -32768;
  return (int16_t)lroundf(scaled);
}

void recordDiagnostic(uint32_t dtGoodUs, int16_t rawDiff, int16_t delta,
                      uint8_t flags) {
  if (!diagnosticCapture) return;

  const EncoderRead& read = encoderRead;
  DiagnosticSample& sample = diagnosticBuffer[diagnosticHead];
  sample.doneUs = read.doneUs;
  sample.dtGoodUs = dtGoodUs;
  sample.counts = encoderCountsMT;
  sample.raw = read.ok ? read.raw : 0xFFFF;
  sample.rawDiff = rawDiff;
  sample.delta = delta;
  sample.i2cUs = read.i2cUs;
  sample.omegaMilliRevS = milliRevS(omega);
  sample.flags = flags;
  sample.txStatus = read.txStatus;
  sample.requested = read.requested;
  sample.available = read.available;
  sample.state = (uint8_t)mode | ((mode == TAKEOVER) ? 0x80 : 0x00);

  diagnosticHead = (diagnosticHead + 1) % DIAG_CAPACITY;
  if (diagnosticCount < DIAG_CAPACITY) {
    ++diagnosticCount;
  } else {
    diagnosticWrapped = true;
  }
}

void clearVelocityHistory() {
  velocityHistoryHead = 0;
  velocityHistoryCount = 0;
  lastVelocityUpdateUs = 0;
}

void pushVelocityPoint(uint32_t timeUs, int32_t counts) {
  velocityHistory[velocityHistoryHead].timeUs = timeUs;
  velocityHistory[velocityHistoryHead].counts = counts;
  velocityHistoryHead = (velocityHistoryHead + 1) % VELOCITY_HISTORY_LEN;
  if (velocityHistoryCount < VELOCITY_HISTORY_LEN) ++velocityHistoryCount;
}

uint8_t historyIndexFromNewest(uint8_t samplesBack) {
  return (velocityHistoryHead + VELOCITY_HISTORY_LEN - 1 - samplesBack) % VELOCITY_HISTORY_LEN;
}

void invalidateVelocity() {
  encoderVelocityValid = false;
  omega = 0.0f;
  lastDeltaSign = 0;
  clearVelocityHistory();
}

// Establish a fresh baseline.  A recovery after a long gap retains only the
// nearest whole-turn count; modulo-360 position remains correct, while no
// takeover can start until a new valid velocity window has been collected.
void primeEncoder(uint16_t raw, uint32_t doneUs, bool preserveNearestTurn) {
  if (!encoderPrimed || !preserveNearestTurn) {
    encoderCountsMT = raw;
  } else {
    int32_t wholeTurnBase = encoderCountsMT - (encoderCountsMT % 4096);
    int32_t candidate = wholeTurnBase + raw;
    int32_t difference = candidate - encoderCountsMT;
    if (difference > 2048) candidate -= 4096;
    if (difference < -2048) candidate += 4096;
    encoderCountsMT = candidate;
  }

  encoderPrimed = true;
  lastGoodRaw = raw;
  lastGoodUs = doneUs;
  angleDegMT = (double)encoderCountsMT * 360.0 / 4096.0;
  invalidateVelocity();
  pushVelocityPoint(doneUs, encoderCountsMT);
}

// A transaction is either an explicit success or an explicit failure.  It
// never returns a stale raw value as a fake sample.
bool readRawSample() {
  EncoderRead& read = encoderRead;
  uint32_t startUs = micros();
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
      read.doneUs = micros();  // timestamp the actual acquisition, not its start
      uint32_t elapsedUs = read.doneUs - startUs;
      read.i2cUs = elapsedUs > 65535U ? 65535U : (uint16_t)elapsedUs;
      read.ok = true;
      return true;
    }

    // Drain a partial response so a short read cannot poison the next one.
    while (Wire.available()) Wire.read();
  }

  read.doneUs = micros();
  uint32_t elapsedUs = read.doneUs - startUs;
  read.i2cUs = elapsedUs > 65535U ? 65535U : (uint16_t)elapsedUs;
  return false;
}

bool velocityFromWindow(uint32_t nowUs, float& velocityRevS) {
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
  velocityRevS = ((float)deltaCounts / 4096.0f) * (1000000.0f / (float)chosenAgeUs);
  return true;
}

void updateVelocityEstimate(uint32_t nowUs) {
  float windowVelocity = 0.0f;
  if (!velocityFromWindow(nowUs, windowVelocity)) {
    encoderVelocityValid = false;
    omega = 0.0f;
    return;
  }

  if (!encoderVelocityValid || lastVelocityUpdateUs == 0) {
    omega = windowVelocity;
  } else {
    uint32_t updateDtUs = nowUs - lastVelocityUpdateUs;
    float alpha = 1.0f - expf(-(float)updateDtUs / (float)VELOCITY_FILTER_TAU_US);
    omega += alpha * (windowVelocity - omega);
  }
  lastVelocityUpdateUs = nowUs;
  encoderVelocityValid = true;
}

bool encoderHealthy() {
  return encoderPrimed && encoderVelocityValid &&
         ((uint32_t)(micros() - lastGoodUs) <= ENCODER_FRESH_US);
}

void updateEncoder() {
  uint32_t nowUs = micros();
  if (!samplerScheduled) {
    samplerScheduled = true;
    nextSampleDueUs = nowUs;
  }
  if ((int32_t)(nowUs - nextSampleDueUs) < 0) return;

  // Keep a 1 kHz schedule when possible, but do not burst-catch-up after a
  // stall; the measured completion-to-completion dt is what matters.
  nextSampleDueUs += ENCODER_SAMPLE_PERIOD_US;
  if ((int32_t)(nowUs - nextSampleDueUs) >= (int32_t)ENCODER_SAMPLE_PERIOD_US) {
    nextSampleDueUs = nowUs + ENCODER_SAMPLE_PERIOD_US;
  }

  readRawSample();
  EncoderRead& read = encoderRead;
  uint32_t dtGoodUs = encoderPrimed ? (read.doneUs - lastGoodUs) : 0;
  int16_t rawDiff = 0;
  int16_t delta = 0;
  uint8_t flags = 0;

  if (!read.ok) {
    flags = (read.txStatus != 0) ? DIAG_TX_ERROR : DIAG_SHORT_READ;
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, rawDiff, delta, flags);
    return;
  }

  if (!encoderPrimed) {
    primeEncoder(read.raw, read.doneUs, false);
    flags = DIAG_PRIMED;
    recordDiagnostic(0, 0, 0, flags);
    return;
  }

  if (dtGoodUs == 0 || dtGoodUs > ENCODER_MAX_GOOD_GAP_US) {
    // We cannot safely choose a turn count across an extended blind interval.
    primeEncoder(read.raw, read.doneUs, true);
    flags = DIAG_LONG_GAP | DIAG_PRIMED;
    recordDiagnostic(dtGoodUs, 0, 0, flags);
    return;
  }

  rawDiff = (int16_t)read.raw - (int16_t)lastGoodRaw;
  delta = rawDiff;
  if (delta > 2048) delta -= 4096;
  if (delta < -2048) delta += 4096;

  int32_t absDelta = delta < 0 ? -(int32_t)delta : (int32_t)delta;
  if (absDelta == 2048) {
    // Exactly half a turn has no unique signed shortest-path interpretation.
    flags = DIAG_ALIAS;
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, rawDiff, delta, flags);
    return;
  }

  int32_t maxAllowed = ENCODER_DELTA_MARGIN_COUNTS + (int32_t)ceilf(
      ENCODER_MAX_PLAUSIBLE_REV_S * 4096.0f * (float)dtGoodUs / 1000000.0f);
  if (maxAllowed > 2047) maxAllowed = 2047;
  if (absDelta > maxAllowed) {
    // Leave lastGoodRaw/time untouched: the next genuine sample is compared
    // against the last trustworthy sample over its true longer interval.
    flags = DIAG_RATE;
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, rawDiff, delta, flags);
    return;
  }

  encoderCountsMT += ENCODER_DIR_SIGN * delta;   // invert to count clockwise
  lastGoodRaw = read.raw;
  lastGoodUs = read.doneUs;
  angleDegMT = (double)encoderCountsMT * 360.0 / 4096.0;
  pushVelocityPoint(read.doneUs, encoderCountsMT);
  updateVelocityEstimate(read.doneUs);

  flags = DIAG_VALID;
  int8_t sign = (delta > 1) ? 1 : ((delta < -1) ? -1 : 0);
  if (sign != 0 && lastDeltaSign != 0 && sign != lastDeltaSign) flags |= DIAG_DIR_FLIP;
  if (sign != 0) lastDeltaSign = sign;
  if (encoderVelocityValid && fabsf(omega) > 0.10f) diagnosticSawMotion = true;
  recordDiagnostic(dtGoodUs, rawDiff, delta, flags);
}

float wheelAngleDeg() {
  double angle = fmod(angleDegMT - wedge0OffsetDeg, 360.0);
  if (angle < 0) angle += 360.0;
  return (float)angle;
}

int currentWedge() {
  return (int)(wheelAngleDeg() / WEDGE_DEG) % NUM_WEDGES;
}

/* ------------------------- DRIVER HELPERS -------------------------------- */
// Kept verbatim from REFERENCE_uart_current_WORKS.ino, apart from the variable
// names.  This is the bench-proven TMC2209 configuration.
void driverConfig() {
  driver.begin();
  driver.I_scale_analog(false);   // <<< FIX: use internal Iref, NOT the Vref pot. Without this, rms_current is scaled down by the pot.
  driver.toff(4);
  driver.blank_time(24);
  driver.microsteps(16);
  driver.en_spreadCycle(true);    // SpreadCycle = torque + high RPM (was stealthChop = weak/RPM-limited)
  driver.pwm_autoscale(true);
  driver.rms_current(RMS_CURRENT_MA, 1.0);   // holdMult=1.0 -> IHOLD == IRUN
  driver.TCOOLTHRS(0);            // CoolStep OFF (explicit)
  driver.semin(0);                // <<< 0 = CoolStep fully disabled
  driver.semax(0);
  driver.iholddelay(0);
  driver.TPOWERDOWN(255);         // slow standstill power-down (keep torque at rest)
}

void driverFreewheel() {
  if (stepper) stepper->forceStop();
  driver.freewheel(1);
  driver.ihold(0);
  digitalWrite(PIN_EN, HIGH);  // active-low enable: outputs float
}

void driverActive() {
  driver.freewheel(0);
  driver.rms_current(RMS_CURRENT_MA, 1.0);
  digitalWrite(PIN_EN, LOW);
}

/* ------------------- FRICTION / LANDING PREDICTION ----------------------- */
float predictStopAngle() {
  int dir = (omega >= 0.0f) ? 1 : -1;
  float speed = fabsf(omega);
  float c = (dir > 0) ? cw_c : ccw_c;
  float b = (dir > 0) ? cw_b : ccw_b;
  if (speed < 1e-3f) return wheelAngleDeg();

  float travelRad = (1.0f / b) * (speed * TWO_PI)
                  - (c / (b * b)) * logf(1.0f + b * (speed * TWO_PI) / c);
  float stop = wheelAngleDeg() + dir * travelRad * RAD_TO_DEG;
  stop = fmodf(stop, 360.0f);
  if (stop < 0) stop += 360.0f;
  return stop;
}

int predictStopWedge() {
  return (int)(predictStopAngle() / WEDGE_DEG) % NUM_WEDGES;
}

float chooseSafeTargetAngle(int dir) {
  int startWedge = predictStopWedge();
  for (int step = 0; step < NUM_WEDGES; ++step) {
    int wedge = ((startWedge + dir * step) % NUM_WEDGES + NUM_WEDGES) % NUM_WEDGES;
    if (!isDare(wedge)) {
      float fraction = 0.30f + (float)random(0, 400) / 1000.0f;
      return wedge * WEDGE_DEG + fraction * WEDGE_DEG;
    }
  }
  return 0.0f;
}

int32_t curUstepFromWheel() {
  return (int32_t)lround((angleDegMT / 360.0) * WHEEL_USTEPS_PER_REV);
}

/* --------------------------- TAKEOVER ------------------------------------ */
void beginTakeover() {
  // Callers gate this on encoderHealthy(), so omega and angle share a valid,
  // measured time base rather than a fabricated stale sample.
  int dir = (omega >= 0.0f) ? 1 : -1;
  takeoverDir = dir;
  float speed0 = fabsf(omega);

  float target = chooseSafeTargetAngle(dir);
  float current = wheelAngleDeg();
  float forwardDeg = (dir > 0) ? (target - current) : (current - target);
  forwardDeg = fmodf(forwardDeg, 360.0f);
  if (forwardDeg < 0) forwardDeg += 360.0f;

  float wheelAccel = (float)ACCEL_CEILING_SPS2 / WHEEL_USTEPS_PER_REV;
  float minStopDeg = (speed0 * speed0) / (2.0f * wheelAccel) * 360.0f + 30.0f;
  while (forwardDeg < minStopDeg) forwardDeg += 360.0f;

  takeoverTargetDeg = target;
  int32_t currentU = curUstepFromWheel();
  takeoverTargetU = currentU + dir * (int32_t)lroundf(
      forwardDeg / 360.0f * WHEEL_USTEPS_PER_REV);

  driverActive();
  stepper->setCurrentPosition(currentU);  // only while stopped/freewheeling
  uint32_t matchedHz = (uint32_t)(speed0 * WHEEL_USTEPS_PER_REV);
  if (matchedHz < 100) matchedHz = 100;
  stepper->setSpeedInHz(matchedHz);
  stepper->setAcceleration(ACCEL_CEILING_SPS2);
  if (dir > 0) stepper->runForward(); else stepper->runBackward();
  stepper->applySpeedAcceleration();
  stepper->moveTo(takeoverTargetU);  // one hardware-timed move; never re-issued per tick

  if (debugLog && !diagnosticCapture) {
    Serial.printf("# TK-START dir=%d w0=%.3f target=%.1f runway=%.1f tgtU=%ld\n",
                  dir, speed0, target, forwardDeg, (long)takeoverTargetU);
  }
  mode = TAKEOVER;
}

void takeoverStep() {
  if (!stepper->isRunning()) {
    mode = SETTLE;
    settleT0 = 0;
    return;
  }

  static uint32_t lastLogMs = 0;
  if (debugLog && !diagnosticCapture && millis() - lastLogMs > 200) {
    lastLogMs = millis();
    float remain = (takeoverDir > 0)
                 ? (takeoverTargetDeg - wheelAngleDeg())
                 : (wheelAngleDeg() - takeoverTargetDeg);
    remain = fmodf(remain, 360.0f);
    if (remain < 0) remain += 360.0f;
    Serial.printf("# TK dir=%d angle=%.1f target=%.1f remain=%.1f omega=%.3f pos=%ld tgtU=%ld\n",
                  takeoverDir, wheelAngleDeg(), takeoverTargetDeg, remain, omega,
                  (long)stepper->getCurrentPosition(), (long)takeoverTargetU);
  }
}

/* --------------------------- DIAGNOSTIC DUMP ----------------------------- */
void dumpDiagnostics() {
  if (diagnosticCount == 0) {
    Serial.println(F("# P1-DIAG: no samples captured."));
    return;
  }

  uint32_t minDt = 0xFFFFFFFFUL;
  uint32_t maxDt = 0;
  uint16_t maxI2cUs = 0;
  int32_t maxAbsDelta = 0;
  uint16_t accepted = 0, i2cErrors = 0, gaps = 0, aliases = 0, rates = 0, flips = 0;
  uint16_t positive = 0, negative = 0;

  uint16_t first = (diagnosticHead + DIAG_CAPACITY - diagnosticCount) % DIAG_CAPACITY;
  for (uint16_t i = 0; i < diagnosticCount; ++i) {
    const DiagnosticSample& sample = diagnosticBuffer[(first + i) % DIAG_CAPACITY];
    if (sample.dtGoodUs != 0) {
      if (sample.dtGoodUs < minDt) minDt = sample.dtGoodUs;
      if (sample.dtGoodUs > maxDt) maxDt = sample.dtGoodUs;
    }
    if (sample.i2cUs > maxI2cUs) maxI2cUs = sample.i2cUs;
    int32_t absDelta = sample.delta < 0 ? -(int32_t)sample.delta : (int32_t)sample.delta;
    if (absDelta > maxAbsDelta) maxAbsDelta = absDelta;
    if (sample.flags & DIAG_VALID) ++accepted;
    if (sample.flags & (DIAG_TX_ERROR | DIAG_SHORT_READ)) ++i2cErrors;
    if (sample.flags & DIAG_LONG_GAP) ++gaps;
    if (sample.flags & DIAG_ALIAS) ++aliases;
    if (sample.flags & DIAG_RATE) ++rates;
    if (sample.flags & DIAG_DIR_FLIP) ++flips;
    if (sample.delta > 1) ++positive;
    if (sample.delta < -1) ++negative;
  }

  Serial.println(F("# P1-DIAG flags: V=accepted TX=write-error SR=short-read GAP=re-prime ALIAS=half-turn RATE=outlier PRIME=baseline FLIP=delta-sign-change"));
  Serial.printf("# P1-DIAG n=%u accepted=%u errors=%u gaps=%u alias=%u rate=%u flips=%u dir(+/-)=%u/%u dtGood_us[min/max]=%lu/%lu i2c_us[max]=%u maxAbsDelta=%ld wrapped=%d\n",
                diagnosticCount, accepted, i2cErrors, gaps, aliases, rates, flips,
                positive, negative, (unsigned long)(minDt == 0xFFFFFFFFUL ? 0 : minDt),
                (unsigned long)maxDt, maxI2cUs, (long)maxAbsDelta, diagnosticWrapped);
  Serial.println(F("# P1-DIAG columns: done_us,dt_good_us,raw,raw_diff,delta,counts,i2c_us,omega_mrev_s,flags_hex,tx_status,request_n,available_n,state_hex"));

  for (uint16_t i = 0; i < diagnosticCount; ++i) {
    const DiagnosticSample& sample = diagnosticBuffer[(first + i) % DIAG_CAPACITY];
    Serial.printf("D,%lu,%lu,%u,%d,%d,%ld,%u,%d,%02X,%u,%u,%u,%02X\n",
                  (unsigned long)sample.doneUs, (unsigned long)sample.dtGoodUs,
                  sample.raw, sample.rawDiff, sample.delta, (long)sample.counts,
                  sample.i2cUs, sample.omegaMilliRevS, sample.flags,
                  sample.txStatus, sample.requested, sample.available, sample.state);
  }
}

void startDiagnosticCapture() {
  diagnosticHead = 0;
  diagnosticCount = 0;
  diagnosticWrapped = false;
  diagnosticCapture = true;
  diagnosticSawMotion = false;
  diagnosticStillSinceUs = 0;
  Serial.println(F("# P1-DIAG armed: RAM-only 1 kHz capture. Spin the wheel; it will dump only after LANDED/freewheel."));
}

void serviceDiagnosticCapture() {
  if (!diagnosticCapture || !diagnosticSawMotion) return;

  // Never dump while a motor move might still be active.  DONE has released
  // the driver and establishes the same stopped condition used for LANDED.
  bool safelyStopped = (mode == DONE || mode == IDLE) && encoderHealthy() &&
                       fabsf(omega) <= STILL_REV_S;
  if (!safelyStopped) {
    diagnosticStillSinceUs = 0;
    return;
  }

  uint32_t nowUs = micros();
  if (diagnosticStillSinceUs == 0) {
    diagnosticStillSinceUs = nowUs;
  } else if (nowUs - diagnosticStillSinceUs >= 700000UL) {
    diagnosticCapture = false;
    dumpDiagnostics();
  }
}

/* --------------------------- SERIAL UI ----------------------------------- */
void help() {
  Serial.println(F(
    "\n=== PRIZE WHEEL (P1 sensing build) ===\n"
    " z  set current pointer position as wedge-0 boundary\n"
    " s  print angle / wedge / velocity / sensor health\n"
    " d  arm high-rate RAM encoder capture (auto-dump after true stop)\n"
    " v  toggle live takeover logs (disabled while d capture is armed)\n"
    " i  toggle motor direction inversion\n"
    " m  print dare mask\n"
    " ?  show this help"));
}

void status() {
  uint32_t ageUs = encoderPrimed ? (uint32_t)(micros() - lastGoodUs) : 0;
  Serial.printf("# sensor=%s age_us=%lu angle=%.2f wedge=%d omega=%.4f predStop=%.1f(w%d) mode=%u\n",
                encoderHealthy() ? "VALID" : "WAITING",
                (unsigned long)ageUs, wheelAngleDeg(), currentWedge(), omega,
                predictStopAngle(), predictStopWedge(), (unsigned)mode);
}

void handleSerial() {
  if (!Serial.available()) return;
  char command = (char)Serial.read();
  switch (command) {
    case 'z':
      if (!encoderPrimed) Serial.println(F("# encoder is not primed; calibration ignored"));
      else {
        wedge0OffsetDeg = angleDegMT;
        Serial.println(F("# wedge-0 boundary set at current encoder angle"));
      }
      break;
    case 's':
      if (diagnosticCapture) Serial.println(F("# P1-DIAG capture active: status suppressed to preserve timing"));
      else status();
      break;
    case 'd':
      if (!diagnosticCapture) startDiagnosticCapture();
      else Serial.println(F("# P1-DIAG already armed; it will dump after the wheel reaches DONE"));
      break;
    case 'v':
      if (diagnosticCapture) Serial.println(F("# live logging remains suppressed while P1-DIAG is armed"));
      else {
        debugLog = !debugLog;
        Serial.printf("# live takeover logging=%d\n", debugLog);
      }
      break;
    case 'i':
      INVERT_DIR = !INVERT_DIR;
      stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
      Serial.printf("# INVERT_DIR=%d\n", INVERT_DIR);
      break;
    case 'm':
      Serial.printf("# dare_mask=0x%03X; dare wedges: 1 5\n", dare_mask);
      break;
    case '?':
      help();
      break;
    default:
      break;
  }
}

/* --------------------------- SETUP / LOOP -------------------------------- */
void setup() {
  Serial.begin(115200);
  delay(300);
  randomSeed(esp_random());

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(3);  // bounded transaction; all failures are recorded

  if (readRawSample()) {
    primeEncoder(encoderRead.raw, encoderRead.doneUs, false);
    samplerScheduled = true;
    nextSampleDueUs = encoderRead.doneUs + ENCODER_SAMPLE_PERIOD_US;
    Serial.printf("# AS5600 primed: raw=%u, I2C=%uus\n", encoderRead.raw, encoderRead.i2cUs);
  } else {
    Serial.printf("# AS5600 initial read failed: tx=%u requested=%u available=%u; retrying in loop\n",
                  encoderRead.txStatus, encoderRead.requested, encoderRead.available);
  }

  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, LOW);  // hold at boot; freewheel is selected below

  TMC_SERIAL.begin(115200, SERIAL_8N1, TMC_RX_PIN, TMC_TX_PIN);
  driverConfig();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
  stepper->setEnablePin(PIN_EN, true);
  stepper->setAutoEnable(false);

  uint8_t connection = driver.test_connection();
  Serial.printf("# TMC UART test_connection (0=OK): %u\n", connection);
  if (connection != 0) {
    Serial.println(F("# >>> UART FAIL - check TX/1k/RX wiring, VIO=3V3, and common GND"));
  }

  help();
  driverFreewheel();
  mode = IDLE;
}

void loop() {
  updateEncoder();
  handleSerial();

  bool sensorOK = encoderHealthy();
  bool spinning = sensorOK && fabsf(omega) > SPIN_DETECT_REV_S;
  if (spinning) {
    if (spinAboveMs == 0) spinAboveMs = millis();
  } else {
    spinAboveMs = 0;
  }
  bool spinConfirmed = spinning && (millis() - spinAboveMs > 100);
  static bool sawSpinThisCycle = false;

  switch (mode) {
    case IDLE:
    case DONE:
      if (spinConfirmed) {
        sawSpinThisCycle = true;
        driverFreewheel();
        mode = FREE_SPIN;
      }
      break;

    case FREE_SPIN:
      // Never predict or intervene from stale/invalid velocity.  A fresh
      // 20-30 ms valid window is required before control sees a new omega.
      if (!sensorOK) break;
      if (spinConfirmed) sawSpinThisCycle = true;
      if (sawSpinThisCycle && fabsf(omega) <= TAKEOVER_REV_S && fabsf(omega) > 0.08f) {
        if (isDare(predictStopWedge())) {
          sawSpinThisCycle = false;
          beginTakeover();
        } else {
          sawSpinThisCycle = false;
          mode = SETTLE;
          settleT0 = 0;
        }
      }
      break;

    case TAKEOVER:
      if (!sensorOK) {
        // Do not continue a powered intervention when the encoder time base
        // has become untrustworthy.  Release the wheel; once sensing recovers,
        // FREE_SPIN can make a new decision from a real velocity window.
        driverFreewheel();
        sawSpinThisCycle = true;
        mode = FREE_SPIN;
        settleT0 = 0;
        break;
      }
      if (fabsf(omega) > SPIN_DETECT_REV_S && spinConfirmed) {
        driverFreewheel();
        sawSpinThisCycle = true;
        mode = FREE_SPIN;
        break;
      }
      takeoverStep();
      break;

    case SETTLE:
      if (!sensorOK) {
        settleT0 = 0;
        break;
      }
      if (fabsf(omega) > SPIN_DETECT_REV_S) {
        driverFreewheel();
        sawSpinThisCycle = true;
        mode = FREE_SPIN;
        break;
      }
      if (fabsf(omega) > STILL_REV_S) {
        settleT0 = 0;
      } else {
        if (settleT0 == 0) settleT0 = millis();
        if (millis() - settleT0 > SETTLE_MS) {
          int wedge = currentWedge();
          Serial.printf("# LANDED wedge %d%s\n", wedge,
                        isDare(wedge) ? "  <-- DARE! (report this)" : " (safe)");
          driverFreewheel();
          mode = DONE;
        }
      }
      break;
  }

  serviceDiagnosticCapture();
}


