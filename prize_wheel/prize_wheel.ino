/* ============================================================================
 * prize_wheel.ino - Prize wheel firmware
 *
 * v2 adaptive revision (Claude variant, on top of the P1 sensing build)
 * ---------------------------------------------------------------------
 * Adds four things aimed at the open issues in README.md:
 *  1. Friction auto-calibration.  Every free coast fits decel = c + b*omega
 *     per direction and blends it into the model (NVS-persisted), and every
 *     unsteered landing measures real prediction error, which now sets the
 *     dare margin instead of a guessed constant.  'c' prints, 'C' resets.
 *  2. Target-first steering.  When a dare stop is predicted, a target wedge
 *     is drawn UNIFORMLY from all ten safe wedges while the wheel is still
 *     fast, and the takeover fires at the one instant per revolution when
 *     stopping on that wedge needs a decel of only ~1.1-1.7x natural
 *     friction.  This removes the structural landing bias (wedges 11/0/2/3/4
 *     were unreachable) because reachability now comes from timing, not from
 *     whichever wedges happen to sit 150-210 deg ahead at a fixed speed.
 *  3. Disguised braking.  A planned takeover is one continuous deceleration
 *     ramp pinned just above the wheel's own measured friction, entered at
 *     97% speed match, so the catch reads as slightly heavier coasting
 *     rather than cruise-then-brake.
 *  4. Housekeeping.  Attended 'a' staircase probe measures the true clean
 *     accel ceiling (persisted); after a held landing the coils float after
 *     2.5 s so an idle guest touch feels a free wheel, not a motor detent.
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
#include <Preferences.h>
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
// FAS polarity is not a wheel-direction calibration.  The p command below
// measures and persists the actual sign with the wheel encoder before any
// automatic recovery move is permitted.
bool INVERT_DIR = true;

// The p probe persists the physical FAS-to-wheel sign.  Only a calibrated
// build may enter the high-speed path; it never guesses from INVERT_DIR.
const bool ENABLE_MOTOR_TAKEOVER = true;
const bool ENABLE_DARE_RECOVERY = true;

#define TAKEOVER_REV_S     0.26f  // intercept while there is still real runway
#define SPIN_DETECT_REV_S  0.12f  // deliberate weak hand spins must enter FREE_SPIN
#define SPIN_CONFIRM_MS    60
const uint16_t SPIN_CONFIRM_TIMEOUT_MS = 1200;
const float SPIN_CONFIRM_TRAVEL_DEG = 6.0f;
const float SPIN_CANCEL_BACKTRACK_DEG = 2.0f;
const float GUEST_OVERRIDE_REV_S = 0.80f;
const uint16_t GUEST_OVERRIDE_CONFIRM_MS = 60;
#define ACCEL_CEILING_SPS2 650    // conservative attended-test value
// v2: raised so the planner can catch at up to ~0.31 rev/s (38 motor RPM)
// without an audible speed step at engagement.
#define TAKEOVER_MAX_REV_S 0.320f
const float TAKEOVER_MIN_REV_S = 0.070f;
// 0.28 gated gentle spins out of the disguised takeover entirely, funneling
// exactly those spins into stop-on-dare -> visible recovery.  Let them steer.
const float TAKEOVER_MIN_PEAK_REV_S = 0.16f;
const float TAKEOVER_MATCH_FRACTION = 0.90f; // motor trails the wheel: brake, never lead
const float TAKEOVER_RUNWAY_MARGIN_DEG = 35.0f;
const float TAKEOVER_GUARD_COAST_DEG = 60.0f;
const float TAKEOVER_MAX_RUNWAY_DEG = 210.0f;
const float TAKEOVER_TARGET_TOL_DEG = 4.0f;
const float SAFE_WEDGE_EDGE_MARGIN_DEG = 8.0f;
const float SAFE_TARGET_JITTER_DEG = 3.0f;
const float PREDICTION_DARE_MARGIN_DEG = 8.0f;
const float TAKEOVER_OPPOSITE_ABORT_REV_S = 0.050f;
const uint16_t TAKEOVER_OPPOSITE_ABORT_MS = 75;
const uint16_t TAKEOVER_PRECHARGE_CURRENT_MA = 100;
const uint16_t TAKEOVER_BRAKE_CURRENT_MA = 300;
const uint16_t TAKEOVER_PRECHARGE_MS = 80;
const uint16_t TAKEOVER_PICKUP_TO_BRAKE_MS = 250;
const float TAKEOVER_SPEEDUP_ABORT_REV_S = 0.015f;
const uint16_t TAKEOVER_SPEEDUP_ABORT_MS = 30;
// Fight watchdog.  A motor driving against the wheel (dead DIR line, phase
// fight, belt tooth-jump) cannot reverse a 0.9 kg wheel fast enough to trip
// the opposite-motion abort, but it collapses forward speed far faster than
// friction ever does.  Compare wheel speed against the live FAS command.
const uint16_t TAKEOVER_FIGHT_GRACE_MS = 120;
const float TAKEOVER_FIGHT_SPEED_FRACTION = 0.45f;
const uint16_t RECOVERY_CURRENT_MA = 450;
const uint16_t RECOVERY_HOLD_CURRENT_MA = 650;
// The old 120 Hz crawl left a dare wedge at ~7 deg/s: 4-7 s of visibly
// robotic creep.  ~50 deg/s at the wheel reads as one decisive extra notch,
// has large torque margin at 450 mA (17 motor RPM), and the accel stays
// well under the ~1000-1200 sps2 bench-proven clean ceiling.
const uint32_t RECOVERY_SPEED_HZ = 900;
const uint32_t RECOVERY_ACCEL_SPS2 = 900;
const float RECOVERY_STOP_LEAD_DEG = 2.5f;
const float RECOVERY_TARGET_TOL_DEG = 2.0f;
const uint16_t RECOVERY_HOLD_MS = 750;
const uint16_t RECOVERY_TIMEOUT_MS = 7000;
const uint16_t DIR_PROBE_CURRENT_MA = 350;
const uint32_t DIR_PROBE_SPEED_HZ = 100;
const uint32_t DIR_PROBE_ACCEL_SPS2 = 300;
const int32_t DIR_PROBE_USTEPS = 160;  // 9 degrees at the wheel
const float DIR_PROBE_MIN_DEG = 2.0f;
// Bench-established clockwise mapping. Keep this sign fixed; 'i' changes only
// the motor's electrical direction, never wedge numbering.
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

/* ---------------------- ADAPTIVE VARIANT (v2) TUNING --------------------- */
/* Friction auto-calibration: (omega, decel) pairs are sampled ~8x/s during
 * every freewheeling coast and least-squares fitted per direction. */
const float FRICTION_FIT_MIN_REV_S = 0.06f;
const float FRICTION_FIT_MAX_REV_S = 3.0f;
const uint32_t FRICTION_PAIR_SPACING_US = 120000;
const float FRICTION_MAX_DECEL_REV_S2 = 0.60f;  // above this = hand contact
const uint8_t FRICTION_MIN_PAIRS = 24;
const float FRICTION_BLEND = 0.25f;             // per-spin blend into model
const float FRICTION_C_MIN = 0.02f, FRICTION_C_MAX = 2.5f;  // rad/s^2
const float FRICTION_B_MIN = 0.00f, FRICTION_B_MAX = 3.0f;  // 1/s
/* Target-first steering planner. */
const float PLAN_ARM_REV_S = 0.60f;       // plan once prediction is usable
const float PLAN_FIRE_MAX_REV_S = 0.31f;  // fastest catch; 38 motor RPM
const float PLAN_FALLBACK_REV_S = 0.14f;  // below this the old picker may act
const float PLAN_MIN_FWD_DEG = 40.0f;
const float PLAN_CANCEL_CLEAR_FACTOR = 2.0f;
const uint16_t PLAN_CANCEL_SUSTAIN_MS = 200;
const float DISGUISE_DECEL_MIN_RATIO = 1.08f;   // vs natural friction decel
const float DISGUISE_DECEL_MAX_RATIO = 1.70f;
const uint32_t DISGUISE_MIN_ACCEL_SPS2 = 60;
const float PLANNED_MATCH_FRACTION = 0.97f;     // soft pickup, still trailing
const float PLANNED_BRAKE_GATE_BUFFER_DEG = 2.0f;
/* Coil release after a held landing (no locked-wheel tell between spins). */
const uint16_t SAFE_HOLD_RELEASE_MS = 2500;
/* Attended accel-ceiling staircase probe ('a'). */
const uint16_t ACCEL_PROBE_CURRENT_MA = 350;
const uint32_t ACCEL_PROBE_SPEED_HZ = 1600;
const float ACCEL_PROBE_MOVE_DEG = 120.0f;
const float ACCEL_PROBE_LOSS_LIMIT_DEG = 4.0f;

/* --------------------------- STATE --------------------------------------- */
TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, TMC_ADDR);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper* stepper = nullptr;
Preferences preferences;

enum Mode : uint8_t {
  IDLE, FREE_SPIN, PRECHARGE, TAKEOVER, SETTLE,
  RECOVERY_PRECHARGE, DARE_RECOVERY, RECOVERY_HOLD, SAFE_HOLD, DIR_PROBE, DONE,
  ACCEL_PROBE  // appended after DONE so mode ordinals in old captures hold
};
Mode mode = IDLE;

double wedge0OffsetDeg = 0.0;
float omega = 0.0f;                 // signed wheel rev/s; valid only when flagged
bool debugLog = false;

uint32_t spinAboveMs = 0;
int32_t spinAboveStartCounts = 0;
int spinAboveDir = 1;
uint32_t guestOverrideAboveMs = 0;
float takeoverTargetDeg = 0.0f;
float takeoverMinimumRunwayDeg = 0.0f;
int32_t takeoverTargetU = 0;
int takeoverDir = 1;
int spinDir = 1;
int32_t takeoverStartCounts = 0;
int32_t takeoverTravelCounts = 0;
uint32_t takeoverOppositeSinceMs = 0;
uint32_t takeoverMotorEnableMs = 0;
uint8_t takeoverCurrentStage = 0;
float takeoverLowestForwardRevS = 0.0f;
uint32_t takeoverSpeedupSinceMs = 0;
uint32_t takeoverStepStartMs = 0;
uint32_t settleT0 = 0;
bool sawSpinThisCycle = false;

// The sign of a FAS count-up move expressed in encoder/wheel coordinates.
// It is unknown after flash until the attended p probe verifies it.
bool motorDirectionCalibrated = false;
int motorPositiveEncoderSign = 0;

int recoveryDir = 1;
float recoveryTargetDeg = 0.0f;
int32_t recoveryStartCounts = 0;
int32_t recoveryTravelCounts = 0;
uint32_t recoveryStartedMs = 0;
uint32_t recoveryHoldStartedMs = 0;
bool recoveryStopRequested = false;
uint8_t recoveryAttempts = 0;

int32_t directionProbeStartCounts = 0;
uint32_t directionProbeStartedMs = 0;
uint8_t directionProbeLeg = 0;         // 1 = FAS+ leg, 2 = FAS- return leg
int directionProbePlusSign = 0;        // encoder sign measured on the FAS+ leg

// One immutable causal record per spin.  These values are deliberately
// captured at the decision instant, then echoed after the true final stop.
uint32_t spinCounter = 0;
uint32_t activeSpinNumber = 0;
float activeSpinPeakOmega = 0.0f;
bool activeSpinHasDecision = false;
bool activeSpinSteered = false;
float activeSpinPredAngle = 0.0f;
int activeSpinPredWedge = -1;
int activeSpinTargetWedge = -1;
float activeSpinTargetErrorDeg = 0.0f;
bool activeSpinTargetErrorValid = false;

// Friction model: decel(rad/s^2) = c + b * omega(rad/s), per direction.  The
// seeds below only survive until the auto-calibrator has fitted this wheel;
// fitted values are loaded from NVS at boot.
float cw_c = 0.30f, cw_b = 0.15f;
float ccw_c = 0.30f, ccw_b = 0.15f;

/* ---------------------- ADAPTIVE VARIANT (v2) STATE ---------------------- */
// Runtime accel ceiling; seeded from ACCEL_CEILING_SPS2, replaced by the
// attended 'a' probe result persisted in NVS.
uint32_t accelCeilingSps2 = ACCEL_CEILING_SPS2;

// Per-spin least-squares accumulators for decel = c + b*omega (rad units),
// index 0 = CW (+1), 1 = CCW (-1).  Plain global scalars, so the Arduino
// prototype generator never needs a user struct name.
double fricSx[2], fricSy[2], fricSxx[2], fricSxy[2];
uint16_t fricN[2];
float fricPrevSpeed = 0.0f;
uint32_t fricPrevUs = 0;
bool fricPairPrimed = false;

// Rolling |prediction error| (deg) per direction, seeded at the old fixed
// margin and updated from every unsteered landing.
float predMaeDeg[2] = {PREDICTION_DARE_MARGIN_DEG, PREDICTION_DARE_MARGIN_DEG};

// Reference prediction snapshotted mid-coast (~0.45 rev/s); comparing it to
// the true landing measures model accuracy at a speed where it matters.
float refPredAngleDeg = 0.0f;
bool refPredValid = false;

// Target-first steering plan for the active spin.
bool planActive = false;
float planTargetDeg = 0.0f;
int planTargetWedge = -1;
uint32_t planSafeSinceMs = 0;

// When set, launchTakeover() shapes the move as one continuous deceleration
// ramp pinned just above natural friction instead of cruise-then-brake.
bool takeoverDecelProfile = false;

uint32_t safeHoldStillSinceMs = 0;

// Attended accel probe state.
const uint16_t ACCEL_PROBE_STAGES[5] = {800, 1000, 1200, 1500, 1800};
uint8_t accelProbeStage = 0;
uint32_t accelProbeStartedMs = 0;
int32_t accelProbeStartCounts = 0;
int32_t accelProbeMoveU = 0;
uint16_t accelProbeLastPass = 0;

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

bool encoderPositionFresh() {
  return encoderPrimed &&
         ((uint32_t)(micros() - lastGoodUs) <= ENCODER_FRESH_US);
}

bool encoderMotionReady() {
  return encoderPositionFresh() && encoderVelocityValid;
}

bool encoderHealthy() {
  // Existing callers use this for velocity-sensitive spin detection/control.
  return encoderMotionReady();
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
  if (stepper) {
    stepper->forceStop();
    // A jump start is only for the one low-speed pickup that configured it.
    // Reset it before the next freewheel-to-active transition.
    stepper->setJumpStart(0);
    stepper->disableOutputs();
  } else {
    digitalWrite(PIN_EN, HIGH);
  }
  driver.freewheel(1);
  driver.ihold(0);
}

// A fully floating motor has no stored electrical rotor phase.  Enabling it
// directly at 1.45 A produces the audible phase-capture snap.  Bring it back
// with a low pickup current first, then raise current only after STEP pulses
// have been running in the wheel's already-latched direction.
void driverActive(uint16_t currentMa) {
  driver.freewheel(0);
  driver.rms_current(currentMa, 1.0);
  if (stepper) stepper->enableOutputs();
  else digitalWrite(PIN_EN, LOW);
}

void updateTakeoverCurrent() {
  uint32_t elapsedMs = millis() - takeoverMotorEnableMs;
  if (takeoverCurrentStage == 0 && elapsedMs >= TAKEOVER_PICKUP_TO_BRAKE_MS) {
    // Keep the takeover in a low-torque braking regime.  Restoring 1.45 A
    // mid-spin was visibly pulling the wheel forward after the soft catch.
    driver.rms_current(TAKEOVER_BRAKE_CURRENT_MA, 1.0);
    takeoverCurrentStage = 1;
  }
}

bool takeoverSensedSpeedup() {
  float forwardRevS = omega * takeoverDir;
  if (forwardRevS <= 0.0f) return false;
  if (forwardRevS < takeoverLowestForwardRevS) {
    takeoverLowestForwardRevS = forwardRevS;
    takeoverSpeedupSinceMs = 0;
    return false;
  }
  if (forwardRevS <= takeoverLowestForwardRevS + TAKEOVER_SPEEDUP_ABORT_REV_S) {
    takeoverSpeedupSinceMs = 0;
    return false;
  }
  if (takeoverSpeedupSinceMs == 0) takeoverSpeedupSinceMs = millis();
  return millis() - takeoverSpeedupSinceMs >= TAKEOVER_SPEEDUP_ABORT_MS;
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

int wedgeAtAngle(float angle) {
  return (int)(angle / WEDGE_DEG) % NUM_WEDGES;
}

int predictStopWedge() {
  return wedgeAtAngle(predictStopAngle());
}

/* ---------------- FRICTION AUTO-CALIBRATION (v2) ------------------------- */
inline int dirIndex(int dir) { return dir > 0 ? 0 : 1; }

// Natural coast deceleration (rev/s^2) at a given speed, per direction.
float frictionDecelRevS2(int dir, float speedRevS) {
  float c = (dir > 0) ? cw_c : ccw_c;
  float b = (dir > 0) ? cw_b : ccw_b;
  return (c + b * speedRevS * TWO_PI) / TWO_PI;
}

void frictionResetSpin() {
  for (int i = 0; i < 2; ++i) {
    fricSx[i] = 0.0; fricSy[i] = 0.0; fricSxx[i] = 0.0; fricSxy[i] = 0.0;
    fricN[i] = 0;
  }
  fricPairPrimed = false;
}

// Called each loop of a freewheeling FREE_SPIN coast, so every pair is pure
// wheel friction: no motor state ever reaches this sampler.
void frictionSample() {
  if (!encoderMotionReady()) { fricPairPrimed = false; return; }
  float speed = fabsf(omega);
  int dir = (omega >= 0.0f) ? 1 : -1;
  if (dir != spinDir || speed < FRICTION_FIT_MIN_REV_S ||
      speed > FRICTION_FIT_MAX_REV_S) {
    fricPairPrimed = false;
    return;
  }
  uint32_t nowUs = micros();
  if (!fricPairPrimed) {
    fricPrevSpeed = speed;
    fricPrevUs = nowUs;
    fricPairPrimed = true;
    return;
  }
  uint32_t dtUs = nowUs - fricPrevUs;
  if (dtUs < FRICTION_PAIR_SPACING_US) return;
  float dtS = (float)dtUs / 1000000.0f;
  float decel = (fricPrevSpeed - speed) / dtS;  // rev/s^2, >0 while coasting
  float mid = 0.5f * (fricPrevSpeed + speed);
  fricPrevSpeed = speed;
  fricPrevUs = nowUs;
  // Speed-ups and implausible drops are hand contact or noise, not friction.
  if (decel <= 0.0f || decel > FRICTION_MAX_DECEL_REV_S2) return;
  int i = dirIndex(spinDir);
  double x = (double)(mid * TWO_PI);    // rad/s
  double y = (double)(decel * TWO_PI);  // rad/s^2
  fricSx[i] += x; fricSy[i] += y; fricSxx[i] += x * x; fricSxy[i] += x * y;
  ++fricN[i];
}

// At the true end of a spin, fit this coast and blend it into the model.
void frictionFinalizeSpin() {
  for (int i = 0; i < 2; ++i) {
    if (fricN[i] < FRICTION_MIN_PAIRS) continue;
    double n = (double)fricN[i];
    double denom = n * fricSxx[i] - fricSx[i] * fricSx[i];
    if (denom < 1e-6) continue;
    float bFit = (float)((n * fricSxy[i] - fricSx[i] * fricSy[i]) / denom);
    float cFit = (float)((fricSy[i] - (double)bFit * fricSx[i]) / n);
    if (cFit < FRICTION_C_MIN || cFit > FRICTION_C_MAX) continue;
    if (bFit < FRICTION_B_MIN || bFit > FRICTION_B_MAX) continue;
    float& c = (i == 0) ? cw_c : ccw_c;
    float& b = (i == 0) ? cw_b : ccw_b;
    c += FRICTION_BLEND * (cFit - c);
    b += FRICTION_BLEND * (bFit - b);
    Serial.printf("SPIN#%lu FRICTION dir=%s pairs=%u fit c=%.3f b=%.3f -> model c=%.3f b=%.3f\n",
                  (unsigned long)activeSpinNumber, i == 0 ? "+1" : "-1",
                  (unsigned)fricN[i], cFit, bFit, c, b);
  }
  frictionResetSpin();
}

void persistCalibration() {
  preferences.putFloat("cw_c", cw_c);
  preferences.putFloat("cw_b", cw_b);
  preferences.putFloat("ccw_c", ccw_c);
  preferences.putFloat("ccw_b", ccw_b);
  preferences.putFloat("mae_cw", predMaeDeg[0]);
  preferences.putFloat("mae_ccw", predMaeDeg[1]);
}

// Dare margin justified by the measured prediction error of THIS wheel.
float predictionDareMarginDeg() {
  float mae = fmaxf(predMaeDeg[0], predMaeDeg[1]);
  float margin = 1.6f * mae;
  if (margin < 6.0f) margin = 6.0f;
  if (margin > 22.0f) margin = 22.0f;
  return margin;
}

// Every unsteered landing measures the friction model directly: fold the
// signed along-track miss of the mid-coast reference prediction into a
// per-direction rolling MAE.
void updatePredictionError() {
  if (activeSpinSteered || !refPredValid) return;
  float landed = wheelAngleDeg();
  float err = forwardDistanceDeg(spinDir, refPredAngleDeg, landed);
  if (err > 180.0f) err -= 360.0f;
  int i = dirIndex(spinDir);
  predMaeDeg[i] += 0.25f * (fabsf(err) - predMaeDeg[i]);
  Serial.printf("SPIN#%lu PRED-ERR dir=%+d refPred=%.1f landed=%.1f errDeg=%+.1f mae=%.1f margin=%.1f\n",
                (unsigned long)activeSpinNumber, spinDir, refPredAngleDeg,
                landed, err, predMaeDeg[i], predictionDareMarginDeg());
}

float requiredTakeoverRunwayDeg(float speedRevS) {
  float wheelAccel = (float)accelCeilingSps2 / WHEEL_USTEPS_PER_REV;
  return (speedRevS * speedRevS) / (2.0f * wheelAccel) * 360.0f
       + TAKEOVER_RUNWAY_MARGIN_DEG;
}

float forwardDistanceDeg(int dir, float fromAngle, float toAngle) {
  float forward = (dir > 0) ? (toAngle - fromAngle) : (fromAngle - toAngle);
  forward = fmodf(forward, 360.0f);
  if (forward < 0.0f) forward += 360.0f;
  return forward;
}

// Does the low-speed coast envelope enter either dare wedge?  This is an
// encoder-space safety envelope, not the old friction landing prediction.
bool forwardSweepHitsDare(int dir, float currentAngle, float sweepDeg) {
  if (isDare(wedgeAtAngle(currentAngle))) return true;

  for (int wedge = 0; wedge < NUM_WEDGES; ++wedge) {
    if (!isDare(wedge)) continue;
    float entryAngle = (dir > 0)
        ? wedge * WEDGE_DEG
        : (wedge + 1) * WEDGE_DEG;
    if (forwardDistanceDeg(dir, currentAngle, entryAngle) <= sweepDeg) {
      return true;
    }
  }
  return false;
}

// The friction model is only a decision aid.  Treat predictions close to a
// dare boundary as unsafe, but leave predictions well inside a safe wedge
// entirely untouched.
bool predictedStopCouldBeDare(float angle) {
  int wedge = wedgeAtAngle(angle);
  if (isDare(wedge)) return true;

  float within = fmodf(angle, WEDGE_DEG);
  if (within < predictionDareMarginDeg()) {
    int previous = (wedge + NUM_WEDGES - 1) % NUM_WEDGES;
    if (isDare(previous)) return true;
  }
  if (within > WEDGE_DEG - predictionDareMarginDeg()) {
    int next = (wedge + 1) % NUM_WEDGES;
    if (isDare(next)) return true;
  }
  return false;
}

// Select a SAFE wedge center in the current revolution.  Never add a whole
// turn to manufacture runway: a powered extra revolution was the source of
// the 600-degree, visibly motor-driven capture in the returned log.
bool chooseSafeTargetAngle(int dir, float currentAngle, float minRunwayDeg,
                            float& targetAngle, float& runwayDeg) {
  float bestRunway = 1.0e9f;
  float bestTarget = 0.0f;

  for (int wedge = 0; wedge < NUM_WEDGES; ++wedge) {
    if (isDare(wedge)) continue;

    float target = (wedge + 0.50f) * WEDGE_DEG;
    float forward = forwardDistanceDeg(dir, currentAngle, target);
    if (forward < minRunwayDeg || forward > TAKEOVER_MAX_RUNWAY_DEG) continue;

    if (forward < bestRunway) {
      bestRunway = forward;
      bestTarget = target;
    }
  }

  if (bestRunway == 1.0e9f) return false;
  targetAngle = bestTarget;
  runwayDeg = bestRunway;
  return true;
}

// Randomize among reachable safe wedge interiors.  Targets are deliberately
// kept at least 8 degrees from either edge, so ordinary stop error cannot turn
// a nominally safe target into an edge landing.
bool chooseRandomSafeTargetAngle(int dir, float currentAngle, float minRunwayDeg,
                                  float& targetAngle, float& runwayDeg) {
  float candidates[NUM_WEDGES];
  float candidateRunways[NUM_WEDGES];
  uint8_t candidateCount = 0;

  for (int wedge = 0; wedge < NUM_WEDGES; ++wedge) {
    if (isDare(wedge)) continue;

    long jitterCentiDeg = lroundf(SAFE_TARGET_JITTER_DEG * 100.0f);
    float jitter = (float)random(-jitterCentiDeg, jitterCentiDeg + 1) / 100.0f;
    float maxJitter = WEDGE_DEG * 0.5f - SAFE_WEDGE_EDGE_MARGIN_DEG;
    if (jitter > maxJitter) jitter = maxJitter;
    if (jitter < -maxJitter) jitter = -maxJitter;
    float target = (wedge + 0.50f) * WEDGE_DEG + jitter;
    float forward = forwardDistanceDeg(dir, currentAngle, target);
    if (forward < minRunwayDeg || forward > TAKEOVER_MAX_RUNWAY_DEG) continue;

    candidates[candidateCount] = target;
    candidateRunways[candidateCount] = forward;
    ++candidateCount;
  }

  if (candidateCount == 0) return false;
  uint8_t chosen = (uint8_t)random((long)candidateCount);
  targetAngle = candidates[chosen];
  runwayDeg = candidateRunways[chosen];
  return true;
}

// Uniform choice among ALL safe wedges - the target-first planner's picker.
// Deliberately no runway filter: the alignment scheduler waits for the
// revolution moment when this target becomes naturally reachable, which is
// what removes the structural landing bias of the runway-filtered pickers.
float uniformRandomSafeTargetAngle(int& chosenWedge) {
  int safeWedges[NUM_WEDGES];
  uint8_t count = 0;
  for (int wedge = 0; wedge < NUM_WEDGES; ++wedge) {
    if (!isDare(wedge)) safeWedges[count++] = wedge;
  }
  int wedge = safeWedges[random((long)count)];
  long jitterCentiDeg = lroundf(SAFE_TARGET_JITTER_DEG * 100.0f);
  float jitter = (float)random(-jitterCentiDeg, jitterCentiDeg + 1) / 100.0f;
  float maxJitter = WEDGE_DEG * 0.5f - SAFE_WEDGE_EDGE_MARGIN_DEG;
  if (jitter > maxJitter) jitter = maxJitter;
  if (jitter < -maxJitter) jitter = -maxJitter;
  chosenWedge = wedge;
  return (float)(wedge + 0.50f) * WEDGE_DEG + jitter;
}

int32_t curUstepFromWheel() {
  return (int32_t)lround((angleDegMT / 360.0) * WHEEL_USTEPS_PER_REV);
}

int32_t countsForDegrees(float degrees) {
  return (int32_t)lroundf(degrees * 4096.0f / 360.0f);
}

int32_t ustepsForDegrees(float degrees) {
  return (int32_t)lroundf(degrees * WHEEL_USTEPS_PER_REV / 360.0f);
}

// Converts an encoder-space direction (+CW / -CCW) to the fixed FAS sign
// established by the attended p calibration.  This is deliberately not tied
// to INVERT_DIR: that flag only selects the electrical DIR-pin polarity.
int fasSignForEncoderDirection(int encoderDir) {
  if (!motorDirectionCalibrated || motorPositiveEncoderSign == 0) return 0;
  return encoderDir == motorPositiveEncoderSign ? 1 : -1;
}

bool probeStartIsSafe() {
  int wedge = currentWedge();
  if (isDare(wedge) || isDare((wedge + NUM_WEDGES - 1) % NUM_WEDGES) ||
      isDare((wedge + 1) % NUM_WEDGES)) return false;
  float within = fmodf(wheelAngleDeg(), WEDGE_DEG);
  float edgeMargin = fminf(within, WEDGE_DEG - within);
  return edgeMargin >= 11.0f;
}

void startDirectionProbe() {
  if (!stepper || !encoderHealthy()) {
    Serial.println(F("# DIR PROBE refused: encoder/stepper unavailable"));
    return;
  }
  if (mode != IDLE && mode != DONE) {
    Serial.println(F("# DIR PROBE refused: wait for a fully stopped wheel"));
    return;
  }
  if (!probeStartIsSafe()) {
    Serial.println(F("# DIR PROBE refused: center the pointer in safe wedge 3 or 7-11 first"));
    return;
  }

  // This is an attended, at-rest calibration move.  It is intentionally only
  // 9 degrees and never commands a compensating reverse move.
  stepper->forceStopAndNewPosition(0);
  driverActive(DIR_PROBE_CURRENT_MA);
  stepper->setSpeedInHz(DIR_PROBE_SPEED_HZ);
  stepper->setAcceleration(DIR_PROBE_ACCEL_SPS2);
  stepper->setJumpStart(0);
  directionProbeStartCounts = encoderCountsMT;
  directionProbeStartedMs = millis();
  directionProbeLeg = 1;
  directionProbePlusSign = 0;
  stepper->move(DIR_PROBE_USTEPS);
  mode = DIR_PROBE;
  Serial.printf("# DIR PROBE leg 1/2: moving FAS+ %ld usteps; keep hands clear\n",
                (long)DIR_PROBE_USTEPS);
}

void serviceDirectionProbe() {
  if (stepper && stepper->isRunning()) {
    if (millis() - directionProbeStartedMs <= RECOVERY_TIMEOUT_MS) return;
    stepper->forceStopAndNewPosition(0);
    driverFreewheel();
    mode = DONE;
    Serial.println(F("# DIR PROBE FAILED: timeout; automatic recovery remains locked"));
    return;
  }

  int32_t delta = encoderCountsMT - directionProbeStartCounts;
  float degrees = delta * 360.0f / 4096.0f;
  if (fabsf(degrees) < DIR_PROBE_MIN_DEG) {
    driverFreewheel();
    mode = DONE;
    Serial.printf("# DIR PROBE FAILED (leg %u): encoder moved only %.2f deg; if this was leg 2, suspect a stuck DIR line; recovery remains locked\n",
                  (unsigned)directionProbeLeg, degrees);
    return;
  }

  int legSign = degrees > 0.0f ? 1 : -1;

  if (directionProbeLeg == 1) {
    // Leg 2 retraces the same distance with FAS- and must move the encoder
    // the opposite way.  This is the only place FAS- is ever validated, so a
    // dead/stuck DIR line is caught here instead of during a guest spin.
    directionProbePlusSign = legSign;
    stepper->forceStopAndNewPosition(0);
    directionProbeStartCounts = encoderCountsMT;
    directionProbeStartedMs = millis();
    directionProbeLeg = 2;
    stepper->move(-DIR_PROBE_USTEPS);
    Serial.printf("# DIR PROBE leg 2/2: FAS+ moved encoder %+.2f deg; now moving FAS- %ld usteps back\n",
                  degrees, (long)DIR_PROBE_USTEPS);
    return;
  }

  if (legSign == directionProbePlusSign) {
    // Both electrical directions moved the wheel the SAME way: the DIR pin
    // (GPIO27 -> driver DIR) is not switching.  Clear any stale calibration
    // so no automatic move can run until the wiring is fixed and p passes.
    motorDirectionCalibrated = false;
    motorPositiveEncoderSign = 0;
    preferences.putBool("dir_ok", false);
    preferences.putInt("pos_sign", 0);
    driverFreewheel();
    mode = DONE;
    Serial.println(F("# DIR PROBE FAILED: FAS+ and FAS- moved the wheel the SAME direction. DIR line fault (check GPIO27 wiring/solder joints). Takeover+recovery LOCKED."));
    return;
  }

  motorPositiveEncoderSign = directionProbePlusSign;
  motorDirectionCalibrated = true;
  preferences.putBool("dir_ok", true);
  preferences.putInt("pos_sign", motorPositiveEncoderSign);
  driverFreewheel();
  mode = DONE;
  Serial.printf("# DIR PROBE PASS: both legs verified; FAS+ is encoder dir=%+d, FAS- opposite; takeover+recovery ENABLED\n",
                motorPositiveEncoderSign);
}

/* ---------------- ATTENDED ACCEL-CEILING PROBE (v2) ---------------------- */
// Staircase 800 -> 1800 sps2.  Each stage is one 120-deg move at 1600 Hz and
// 350 mA; commanded vs encoder travel disagreeing by more than 4 deg means
// step loss, and the persisted ceiling becomes 75% of the last clean stage.
// Attended only: the wheel sweeps through dares, so run it on the bench like
// the p probe.

void beginAccelProbeStage() {
  stepper->forceStopAndNewPosition(0);
  driverActive(ACCEL_PROBE_CURRENT_MA);
  stepper->setSpeedInHz(ACCEL_PROBE_SPEED_HZ);
  stepper->setAcceleration(ACCEL_PROBE_STAGES[accelProbeStage]);
  stepper->setJumpStart(0);
  accelProbeStartCounts = encoderCountsMT;
  accelProbeStartedMs = millis();
  accelProbeMoveU = ustepsForDegrees(ACCEL_PROBE_MOVE_DEG);
  stepper->move(accelProbeMoveU);
  Serial.printf("# ACCEL PROBE stage %u/%u: accel=%u sps2\n",
                (unsigned)(accelProbeStage + 1),
                (unsigned)(sizeof(ACCEL_PROBE_STAGES) / sizeof(ACCEL_PROBE_STAGES[0])),
                (unsigned)ACCEL_PROBE_STAGES[accelProbeStage]);
}

void startAccelProbe() {
  if (!stepper || !encoderHealthy()) {
    Serial.println(F("# ACCEL PROBE refused: encoder/stepper unavailable"));
    return;
  }
  if (mode != IDLE && mode != DONE) {
    Serial.println(F("# ACCEL PROBE refused: wait for a fully stopped wheel"));
    return;
  }
  accelProbeStage = 0;
  accelProbeLastPass = 0;
  mode = ACCEL_PROBE;
  beginAccelProbeStage();
}

void finishAccelProbe(bool lossDetected, float lossDeg) {
  driverFreewheel();
  mode = DONE;
  if (accelProbeLastPass == 0) {
    Serial.printf("# ACCEL PROBE FAILED: first stage already lost %.1f deg; ceiling unchanged (%u)\n",
                  lossDeg, (unsigned)accelCeilingSps2);
    return;
  }
  uint32_t ceiling = (uint32_t)(0.75f * (float)accelProbeLastPass);
  if (ceiling < ACCEL_CEILING_SPS2) ceiling = ACCEL_CEILING_SPS2;
  accelCeilingSps2 = ceiling;
  preferences.putUInt("accel", accelCeilingSps2);
  Serial.printf("# ACCEL PROBE DONE: last clean=%u sps2%s; ceiling=%u sps2 persisted\n",
                (unsigned)accelProbeLastPass,
                lossDetected ? " (next stage lost steps)" : " (all stages clean)",
                (unsigned)accelCeilingSps2);
}

void serviceAccelProbe() {
  if (stepper && stepper->isRunning()) {
    if (millis() - accelProbeStartedMs <= RECOVERY_TIMEOUT_MS) return;
    stepper->forceStopAndNewPosition(0);
    driverFreewheel();
    mode = DONE;
    Serial.println(F("# ACCEL PROBE FAILED: timeout"));
    return;
  }
  int32_t travelled = encoderCountsMT - accelProbeStartCounts;
  float travelledDeg = fabsf(travelled * 360.0f / 4096.0f);
  float lossDeg = ACCEL_PROBE_MOVE_DEG - travelledDeg;
  Serial.printf("# ACCEL PROBE stage %u result: commanded=%.0f measured=%.1f loss=%.1f deg\n",
                (unsigned)(accelProbeStage + 1), ACCEL_PROBE_MOVE_DEG,
                travelledDeg, lossDeg);
  if (lossDeg > ACCEL_PROBE_LOSS_LIMIT_DEG) {
    finishAccelProbe(true, lossDeg);
    return;
  }
  accelProbeLastPass = ACCEL_PROBE_STAGES[accelProbeStage];
  ++accelProbeStage;
  if (accelProbeStage >= sizeof(ACCEL_PROBE_STAGES) / sizeof(ACCEL_PROBE_STAGES[0])) {
    finishAccelProbe(false, 0.0f);
    return;
  }
  beginAccelProbeStage();
}

void startDareRecovery() {
  if (!ENABLE_DARE_RECOVERY || !motorDirectionCalibrated || !stepper) {
    Serial.println(F("# DARE RECOVERY LOCKED: run attended p direction probe before guest use"));
    return;
  }

  float currentAngle = wheelAngleDeg();
  int current = currentWedge();
  if (!isDare(current)) return;

  recoveryDir = spinDir >= 0 ? 1 : -1;
  float runway = 0.0f;
  if (!chooseSafeTargetAngle(recoveryDir, currentAngle, 0.0f,
                             recoveryTargetDeg, runway)) {
    Serial.println(F("# DARE RECOVERY FAILED: no forward safe target"));
    return;
  }

  int fasSign = fasSignForEncoderDirection(recoveryDir);
  if (fasSign == 0) {
    Serial.println(F("# DARE RECOVERY LOCKED: motor direction is unknown"));
    return;
  }

  recoveryStopRequested = false;
  recoveryStartedMs = millis();
  ++recoveryAttempts;
  float predAngle = predictStopAngle();
  if (!activeSpinHasDecision) {
    recordSpinDecision(predAngle, wedgeAtAngle(predAngle), current,
                       true, wedgeAtAngle(recoveryTargetDeg));
  }
  activeSpinSteered = true;
  activeSpinTargetWedge = wedgeAtAngle(recoveryTargetDeg);
  // Restore rotor torque at a harmless current before issuing a pulse train.
  // This avoids a 450mA phase-capture snap and the resulting false velocity
  // invalidation seen in the returned recovery log.
  driverActive(TAKEOVER_PRECHARGE_CURRENT_MA);
  mode = RECOVERY_PRECHARGE;
  Serial.printf("SPIN#%lu RECOVERY PRECHARGE dir=%+d targetAngle=%.1f targetWedge=%d runwayDeg=%.1f attempt=%u\n",
                (unsigned long)activeSpinNumber, recoveryDir, recoveryTargetDeg,
                activeSpinTargetWedge, runway, recoveryAttempts);
}

void launchDareRecoveryMove() {
  if (!stepper || !encoderPositionFresh()) return;

  float runway = forwardDistanceDeg(recoveryDir, wheelAngleDeg(), recoveryTargetDeg);
  if (runway > TAKEOVER_MAX_RUNWAY_DEG) {
    if (!chooseSafeTargetAngle(recoveryDir, wheelAngleDeg(), 0.0f,
                               recoveryTargetDeg, runway)) {
      driverActive(RECOVERY_HOLD_CURRENT_MA);
      mode = RECOVERY_HOLD;
      return;
    }
    activeSpinTargetWedge = wedgeAtAngle(recoveryTargetDeg);
  }

  recoveryStartCounts = encoderCountsMT;
  recoveryTravelCounts = countsForDegrees(runway);
  int32_t moveUsteps = ustepsForDegrees(runway);
  int fasSign = fasSignForEncoderDirection(recoveryDir);
  if (moveUsteps < 1 || recoveryTravelCounts < 1 || fasSign == 0) {
    driverActive(RECOVERY_HOLD_CURRENT_MA);
    mode = RECOVERY_HOLD;
    return;
  }

  stepper->forceStopAndNewPosition(0);
  driverActive(RECOVERY_CURRENT_MA);
  stepper->setSpeedInHz(RECOVERY_SPEED_HZ);
  stepper->setAcceleration(RECOVERY_ACCEL_SPS2);
  stepper->setJumpStart(0);
  recoveryStopRequested = false;
  recoveryStartedMs = millis();
  stepper->move(fasSign * moveUsteps);
  mode = DARE_RECOVERY;
  Serial.printf("SPIN#%lu RECOVERY dir=%+d fasDir=%+d runwayDeg=%.1f\n",
                (unsigned long)activeSpinNumber, recoveryDir, fasSign, runway);
}

void serviceDareRecovery() {
  if (!stepper) return;
  int32_t travelled = recoveryDir * (encoderCountsMT - recoveryStartCounts);
  int32_t oppositeTol = countsForDegrees(1.5f);
  // At 900 Hz the FAS ramp-down alone covers ~25 deg of wheel travel, so the
  // fixed lead sized for the old 120 Hz crawl would overshoot the safe
  // target into the next wedge.  Ask FAS for its live stopping distance.
  int32_t stopLead = countsForDegrees(
      stepper->stepsToStop() * 360.0f / WHEEL_USTEPS_PER_REV +
      RECOVERY_STOP_LEAD_DEG);

  // A calibrated recovery must never make the wheel travel opposite to the
  // original spin.  Cut output before it can become a visible reverse.
  if (travelled < -oppositeTol) {
    stepper->forceStopAndNewPosition(0);
    driverFreewheel();
    mode = DONE;
    Serial.printf("# DARE RECOVERY ABORT: opposite motion %.1f deg; re-run p calibration\n",
                  travelled * 360.0f / 4096.0f);
    return;
  }

  if (!recoveryStopRequested && travelled >= recoveryTravelCounts - stopLead) {
    recoveryStopRequested = true;
    stepper->stopMove();
  }

  if (stepper->isRunning() && millis() - recoveryStartedMs <= RECOVERY_TIMEOUT_MS) {
    return;
  }

  if (stepper->isRunning()) stepper->forceStopAndNewPosition(0);
  activeSpinTargetErrorDeg = (recoveryTravelCounts - travelled) * 360.0f / 4096.0f;
  activeSpinTargetErrorValid = true;
  driverActive(RECOVERY_HOLD_CURRENT_MA);
  recoveryHoldStartedMs = 0;
  mode = RECOVERY_HOLD;
}

void printLandedEvent() {
  int wedge = currentWedge();
  Serial.printf("SPIN#%lu LANDED wedge=%d angle=%.1f isDare=%d steered=%d predWedgeWas=%d predAngleWas=%.1f targetWedgeWas=%d targetErrorDeg=%.1f\n",
                (unsigned long)activeSpinNumber, wedge, wheelAngleDeg(),
                isDare(wedge) ? 1 : 0, activeSpinSteered ? 1 : 0,
                activeSpinPredWedge, activeSpinPredAngle,
                activeSpinTargetWedge,
                activeSpinTargetErrorValid ? activeSpinTargetErrorDeg : 0.0f);
}

void startSpinEvent(int confirmedDir) {
  ++spinCounter;
  activeSpinNumber = spinCounter;
  activeSpinPeakOmega = fabsf(omega);
  spinDir = confirmedDir;
  // Consume the weak-spin candidate.  FREE_SPIN must not immediately arm the
  // same motion a second time while it is still above the arm threshold.
  spinAboveMs = 0;
  activeSpinHasDecision = false;
  activeSpinSteered = false;
  activeSpinPredAngle = 0.0f;
  activeSpinPredWedge = -1;
  activeSpinTargetWedge = -1;
  activeSpinTargetErrorDeg = 0.0f;
  activeSpinTargetErrorValid = false;
  recoveryAttempts = 0;
  resetSteeringPlan();
  frictionResetSpin();
  refPredValid = false;
  takeoverDecelProfile = false;

  Serial.printf("SPIN#%lu START dir=%+d omegaPeak=%.3f\n",
                (unsigned long)activeSpinNumber, spinDir,
                activeSpinPeakOmega);
}

void recordSpinDecision(float predictedAngle, int predictedWedge, int currentWedge,
                        bool steer, int targetWedge) {
  activeSpinHasDecision = true;
  activeSpinPredAngle = predictedAngle;
  activeSpinPredWedge = predictedWedge;
  activeSpinTargetWedge = targetWedge;
  activeSpinSteered = steer;

  if (steer) {
    Serial.printf("SPIN#%lu DECISION omega=%.3f curWedge=%d predStopAngle=%.1f predStopWedge=%d action=STEER targetWedge=%d\n",
                  (unsigned long)activeSpinNumber, omega, currentWedge,
                  predictedAngle, predictedWedge, targetWedge);
  } else {
    Serial.printf("SPIN#%lu DECISION omega=%.3f curWedge=%d predStopAngle=%.1f predStopWedge=%d action=LEAVE\n",
                  (unsigned long)activeSpinNumber, omega, currentWedge,
                  predictedAngle, predictedWedge);
  }
}

/* --------------------------- TAKEOVER ------------------------------------ */
int32_t takeoverTravelledCounts() {
  return takeoverDir * (encoderCountsMT - takeoverStartCounts);
}

float takeoverTargetErrorDeg() {
  return (float)(takeoverTravelCounts - takeoverTravelledCounts())
       * 360.0f / 4096.0f;
}

void finishTakeover(const char* reason) {
  activeSpinTargetErrorDeg = takeoverTargetErrorDeg();
  activeSpinTargetErrorValid = true;
  if (debugLog && !diagnosticCapture) {
    Serial.printf("# TK-END reason=%s travelled=%.1f target=%.1f error=%.1f\n",
                  reason,
                  takeoverTravelledCounts() * 360.0f / 4096.0f,
                  takeoverTravelCounts * 360.0f / 4096.0f,
                  activeSpinTargetErrorDeg);
  }
  // Do not freewheel at the nominal FAS endpoint: that was the direct cause
  // of target-to-dare coasts in the returned runs.  Hold softly, let the
  // encoder prove a safe settled wedge, then keep that wedge safe until the
  // next deliberate spin releases the coils.
  driverActive(RECOVERY_HOLD_CURRENT_MA);
  recoveryHoldStartedMs = 0;
  recoveryAttempts = 0;
  mode = RECOVERY_HOLD;
}

bool launchTakeover(float forwardDeg) {
  int dir = takeoverDir;
  int fasSign = fasSignForEncoderDirection(dir);
  if (fasSign == 0) return false;
  float measuredForwardRevS = omega * dir;
  // Start at the wheel's measured rolling speed, slightly below it.  Starting
  // FastAccelStepper at zero here made the motor arrive long after the wheel
  // had almost stopped; starting above it produces an obvious forward pull.
  float commandedRevS = measuredForwardRevS *
      (takeoverDecelProfile ? PLANNED_MATCH_FRACTION : TAKEOVER_MATCH_FRACTION);
  if (commandedRevS <= 0.0f) return false;
  if (commandedRevS > TAKEOVER_MAX_REV_S) commandedRevS = TAKEOVER_MAX_REV_S;

  takeoverStartCounts = encoderCountsMT;
  takeoverTravelCounts = (int32_t)lroundf(forwardDeg / 360.0f * 4096.0f);
  takeoverLowestForwardRevS = measuredForwardRevS;
  takeoverSpeedupSinceMs = 0;

  // The FAS coordinate is unrelated to the motor rotor after freewheel.  A
  // calibrated, signed *relative* move is the only safe use here: FAS+ is
  // encoder dir=-1 on this wheel (measured by p), so CW uses FAS-.
  takeoverTargetU = fasSign * ustepsForDegrees(forwardDeg);

  uint32_t maxHz = (uint32_t)floorf(commandedRevS * WHEEL_USTEPS_PER_REV);
  if (maxHz == 0) return false;

  // v2: a profiled takeover is one continuous deceleration ramp whose rate
  // was scheduled to sit just above this wheel's own friction decel, so the
  // catch reads as slightly heavier coasting rather than cruise-then-brake.
  uint32_t accelSps2 = accelCeilingSps2;
  if (takeoverDecelProfile) {
    float rampSteps = (float)ustepsForDegrees(
        fmaxf(forwardDeg - PLANNED_BRAKE_GATE_BUFFER_DEG, 15.0f));
    accelSps2 = (uint32_t)ceilf(((float)maxHz * (float)maxHz) /
                                (2.0f * rampSteps));
    if (accelSps2 < DISGUISE_MIN_ACCEL_SPS2) accelSps2 = DISGUISE_MIN_ACCEL_SPS2;
    if (accelSps2 > accelCeilingSps2) accelSps2 = accelCeilingSps2;
  }
  stepper->forceStopAndNewPosition(0);
  stepper->setSpeedInHz(maxHz);
  stepper->setAcceleration(accelSps2);
  // FastAccelStepper expects a ramp-step count here, not Hz.  Calculate the
  // requested matched start speed explicitly rather than relying on clamping.
  uint32_t jumpStep = (uint32_t)lroundf(
      ((float)maxHz * (float)maxHz) / (2.0f * (float)accelSps2));
  stepper->setJumpStart(jumpStep);
  // One finite hardware-timed, calibrated relative move.  Do not issue any
  // additional planner commands while this trajectory is active.
  stepper->move(takeoverTargetU);
  takeoverStepStartMs = millis();

  Serial.printf("SPIN#%lu TAKEOVER dir=%+d fasDir=%+d targetAngle=%.1f runwayDeg=%.1f relU=%ld wheelRevS=%.3f matchRevS=%.3f accel=%u brakeMa=%u\n",
                 (unsigned long)activeSpinNumber, dir, fasSign, takeoverTargetDeg,
                 forwardDeg, (long)takeoverTargetU, measuredForwardRevS, commandedRevS,
                 (unsigned)accelSps2,
                 TAKEOVER_BRAKE_CURRENT_MA);
  if (debugLog && !diagnosticCapture) {
    Serial.printf("# TK-START dir=%d w0=%.3f target=%.1f runway=%.1f tgtU=%ld brakeOnly=1\n",
                  dir, measuredForwardRevS, takeoverTargetDeg, forwardDeg,
                  (long)takeoverTargetU);
  }
  mode = TAKEOVER;
  return true;
}

bool launchPrechargedTakeover() {
  float speed0 = fabsf(omega);
  float guardedSpeed = speed0;
  if (guardedSpeed < TAKEOVER_MIN_REV_S) guardedSpeed = TAKEOVER_MIN_REV_S;
  // A profiled (planner-scheduled) move already proved its decel fits inside
  // the ceiling at its shorter runway; re-imposing the worst-case ceiling
  // runway here would throw the chosen wedge away after every precharge.
  float minimumRunwayDeg = takeoverDecelProfile
      ? takeoverMinimumRunwayDeg
      : fmaxf(takeoverMinimumRunwayDeg,
              requiredTakeoverRunwayDeg(guardedSpeed));
  float forwardDeg = forwardDistanceDeg(takeoverDir, wheelAngleDeg(),
                                         takeoverTargetDeg);

  // The 80 ms low-current precharge may let the wheel advance a fraction of a
  // degree.  Re-select only if that would make the already-chosen target too
  // close to stop naturally; this is not a per-tick planner rewrite.
  if (forwardDeg < minimumRunwayDeg || forwardDeg > TAKEOVER_MAX_RUNWAY_DEG) {
    float replacementTarget = 0.0f;
    if (!chooseRandomSafeTargetAngle(takeoverDir, wheelAngleDeg(), minimumRunwayDeg,
                                      replacementTarget, forwardDeg)) {
      return false;
    }
    takeoverTargetDeg = replacementTarget;
    activeSpinTargetWedge = wedgeAtAngle(replacementTarget);
  }

  return launchTakeover(forwardDeg);
}

void beginTakeover(int dir, float target, float minimumRunwayDeg) {
  if (!motorDirectionCalibrated || fasSignForEncoderDirection(dir) == 0) {
    Serial.println(F("# TAKEOVER LOCKED: run p direction probe"));
    return;
  }
  // The low-speed guard latches direction while the velocity signal is still
  // well above its near-zero noise floor.  Every commanded move uses that
  // direction; this routine never asks FastAccelStepper to reverse.
  takeoverDir = dir;
  takeoverTargetDeg = target;
  takeoverMinimumRunwayDeg = minimumRunwayDeg;
  takeoverOppositeSinceMs = 0;
  takeoverLowestForwardRevS = fmaxf(0.0f, omega * dir);
  takeoverSpeedupSinceMs = 0;
  takeoverMotorEnableMs = millis();
  takeoverCurrentStage = 0;

  // A floating belt back-drives the motor to an arbitrary electrical phase.
  // Energize that phase at just 100 mA first, wait non-blockingly for the weak
  // detent to settle, then start the one finite FAS move at the same low
  // current.  It rises only to a modest braking current after STEP pulses have
  // established motion; it never restores the full 1.45 A mid-spin.
  driverActive(TAKEOVER_PRECHARGE_CURRENT_MA);
  if (debugLog && !diagnosticCapture) {
    Serial.printf("# TK-PRECHARGE dir=%d target=%.1f ma=%u\n", dir, target,
                  TAKEOVER_PRECHARGE_CURRENT_MA);
  }
  mode = PRECHARGE;
}

void takeoverStep() {
  // A genuine wheel speed-up means the motor has begun helping it; release.
  // Do not treat a speed match as a fault: the old test did exactly that and
  // aborted the planned brake at the instant it finally caught the wheel.
  if (takeoverSensedSpeedup()) {
    finishTakeover("wheel-speed-up");
    return;
  }
  updateTakeoverCurrent();

  // Fight watchdog.  The belt is toothed, so the wheel can only fall far
  // below the commanded step rate through motor step loss, belt tooth-jump,
  // or a DIR-line fault -- exactly the failures that must end this move.  A
  // fight decelerates the wheel long before it can reverse it, so the
  // opposite-motion abort below never fires in time on a 0.9 kg wheel.
  if (millis() - takeoverStepStartMs >= TAKEOVER_FIGHT_GRACE_MS &&
      stepper->isRunning()) {
    float commandedRevS =
        fabsf((float)stepper->getCurrentSpeedInMilliHz(true)) /
        (1000.0f * WHEEL_USTEPS_PER_REV);
    float wheelForwardRevS = fmaxf(0.0f, omega * takeoverDir);
    if (commandedRevS > TAKEOVER_MIN_REV_S &&
        wheelForwardRevS < commandedRevS * TAKEOVER_FIGHT_SPEED_FRACTION) {
      stepper->forceStopAndNewPosition(0);
      driverFreewheel();
      sawSpinThisCycle = true;
      settleT0 = 0;
      mode = FREE_SPIN;
      Serial.printf("SPIN#%lu TAKEOVER ABORT: motor fighting wheel (wheel=%.3f commanded=%.3f rev/s); check DIR wiring/belt, re-run p\n",
                    (unsigned long)activeSpinNumber, wheelForwardRevS,
                    commandedRevS);
      return;
    }
  }

  // If the measured wheel reverses for a sustained interval, release instead
  // of trying to correct it.  A powered reverse is both visible and the known
  // mechanical cause of skipped steps.
  if (omega * takeoverDir < -TAKEOVER_OPPOSITE_ABORT_REV_S) {
    if (takeoverOppositeSinceMs == 0) takeoverOppositeSinceMs = millis();
    if (millis() - takeoverOppositeSinceMs >= TAKEOVER_OPPOSITE_ABORT_MS) {
      finishTakeover("opposite-motion");
      return;
    }
  } else {
    takeoverOppositeSinceMs = 0;
  }

  int32_t travelled = takeoverTravelledCounts();
  int32_t toleranceCounts = (int32_t)lroundf(
      TAKEOVER_TARGET_TOL_DEG / 360.0f * 4096.0f);

  // The encoder, not FAS's virtual coordinate, decides when braking starts.
  // FAS's queued stop distance plus an 8-degree interior buffer leaves room
  // for the wheel to settle without passing a safe target into a dare.
  float stopLeadDeg = stepper->stepsToStop() * 360.0f / WHEEL_USTEPS_PER_REV +
      (takeoverDecelProfile ? PLANNED_BRAKE_GATE_BUFFER_DEG : 8.0f);
  int32_t brakeGateCounts = takeoverTravelCounts - countsForDegrees(stopLeadDeg);
  if (travelled >= brakeGateCounts && !stepper->isStopping()) {
    stepper->stopMove();
  }

  // If physical travel nevertheless reaches the target, never queue extra
  // forward motion.  The held final state/recovery backstop owns the outcome.
  if (travelled >= takeoverTravelCounts + toleranceCounts &&
      !stepper->isStopping()) stepper->stopMove();

  if (!stepper->isRunning()) {
    int32_t remainingCounts = takeoverTravelCounts - travelled;
    // Do not launch a second motor move from rest to chase a shortfall.  It
    // makes the wheel visibly accelerate after the natural coast has ended.
    finishTakeover(remainingCounts > toleranceCounts ? "shortfall" : "complete");
    return;
  }

  static uint32_t lastLogMs = 0;
  if (debugLog && !diagnosticCapture && millis() - lastLogMs > 200) {
    lastLogMs = millis();
    float remain = forwardDistanceDeg(takeoverDir, wheelAngleDeg(), takeoverTargetDeg);
    Serial.printf("# TK dir=%d angle=%.1f target=%.1f remain=%.1f omega=%.3f actual=%.1f/%.1f pos=%ld tgtU=%ld\n",
                  takeoverDir, wheelAngleDeg(), takeoverTargetDeg, remain, omega,
                  travelled * 360.0f / 4096.0f,
                  takeoverTravelCounts * 360.0f / 4096.0f,
                  (long)stepper->getCurrentPosition(), (long)takeoverTargetU);
  }
}

/* ---------------- TARGET-FIRST STEERING PLANNER (v2) --------------------- */
// The runway-filtered pickers can only reach wedges sitting 150-210 deg
// ahead of wherever the wheel happens to be at a fixed decision speed, which
// is why landings clustered and wedges 11/0/2/3/4 were structurally
// unreachable.  The planner inverts that: pick ANY safe wedge uniformly
// while the wheel is still fast, then let the wheel itself rotate the
// geometry into place and fire at the one instant per revolution when
// stopping on that wedge needs a deceleration only slightly above natural
// friction - which is also exactly the move that is hardest to perceive.

void resetSteeringPlan() {
  planActive = false;
  planTargetDeg = 0.0f;
  planTargetWedge = -1;
  planSafeSinceMs = 0;
}

void maybeUpdateSteeringPlan() {
  if (activeSpinHasDecision || !sawSpinThisCycle) return;
  if (!ENABLE_MOTOR_TAKEOVER || !motorDirectionCalibrated) return;
  if (!encoderMotionReady()) return;
  float speed = fabsf(omega);
  if (speed > PLAN_ARM_REV_S || speed < TAKEOVER_MIN_REV_S) return;

  float predAngle = predictStopAngle();
  bool predDare = predictedStopCouldBeDare(predAngle);

  if (!planActive) {
    if (!predDare) return;
    planTargetDeg = uniformRandomSafeTargetAngle(planTargetWedge);
    planActive = true;
    planSafeSinceMs = 0;
    Serial.printf("SPIN#%lu PLAN omega=%.3f predStopAngle=%.1f predStopWedge=%d targetWedge=%d targetAngle=%.1f\n",
                  (unsigned long)activeSpinNumber, omega, predAngle,
                  wedgeAtAngle(predAngle), planTargetWedge, planTargetDeg);
    return;
  }

  // Cancel only if the refining prediction is now comfortably clear of both
  // dares for a sustained interval; flip-flopping is worse than one steer.
  float clearMargin = predictionDareMarginDeg() * PLAN_CANCEL_CLEAR_FACTOR;
  int predWedge = wedgeAtAngle(predAngle);
  float within = fmodf(predAngle, WEDGE_DEG);
  bool comfortablyClear = !isDare(predWedge);
  if (comfortablyClear && within < clearMargin &&
      isDare((predWedge + NUM_WEDGES - 1) % NUM_WEDGES)) comfortablyClear = false;
  if (comfortablyClear && within > WEDGE_DEG - clearMargin &&
      isDare((predWedge + 1) % NUM_WEDGES)) comfortablyClear = false;
  if (!comfortablyClear) {
    planSafeSinceMs = 0;
    return;
  }
  if (planSafeSinceMs == 0) {
    planSafeSinceMs = millis();
  } else if (millis() - planSafeSinceMs >= PLAN_CANCEL_SUSTAIN_MS) {
    Serial.printf("SPIN#%lu PLAN-CANCEL predStopAngle=%.1f now clear\n",
                  (unsigned long)activeSpinNumber, predAngle);
    resetSteeringPlan();
  }
}

// Fire the planned takeover when geometry and physics line up: the decel
// needed to stop exactly on the chosen wedge is a small multiple of the
// wheel's own natural friction decel, and inside the proven accel ceiling.
bool tryPlannedTakeover() {
  if (!planActive || activeSpinHasDecision) return false;
  if (!encoderHealthy() || !ENABLE_MOTOR_TAKEOVER || !motorDirectionCalibrated) {
    return false;
  }
  float speed = fabsf(omega);
  if (speed < TAKEOVER_MIN_REV_S || speed > PLAN_FIRE_MAX_REV_S) return false;
  if (activeSpinPeakOmega < TAKEOVER_MIN_PEAK_REV_S) return false;

  float fwd = forwardDistanceDeg(spinDir, wheelAngleDeg(), planTargetDeg);
  if (fwd < PLAN_MIN_FWD_DEG || fwd > TAKEOVER_MAX_RUNWAY_DEG) return false;

  float vSps = speed * WHEEL_USTEPS_PER_REV;
  float dSteps = (float)ustepsForDegrees(
      fmaxf(fwd - PLANNED_BRAKE_GATE_BUFFER_DEG, 15.0f));
  float aReq = (vSps * vSps) / (2.0f * dSteps);
  float aNat = frictionDecelRevS2(spinDir, speed) * WHEEL_USTEPS_PER_REV;
  if (aNat < 1.0f) aNat = 1.0f;
  if (aReq < aNat * DISGUISE_DECEL_MIN_RATIO) return false;  // motor would lead
  if (aReq > aNat * DISGUISE_DECEL_MAX_RATIO) return false;  // visible grab
  if (aReq > (float)accelCeilingSps2) return false;

  float predAngle = predictStopAngle();
  recordSpinDecision(predAngle, wedgeAtAngle(predAngle), currentWedge(), true,
                     planTargetWedge);
  takeoverDecelProfile = true;
  beginTakeover(spinDir, planTargetDeg, PLAN_MIN_FWD_DEG * 0.5f);
  return true;
}

// Calibrated high-speed capture.  Intervene while there is usable runway only
// when the predicted stop lies in, or close to, a dare; choose a random safe
// interior for that intervention.
bool trySlowDareGuard() {
  if (!sawSpinThisCycle || activeSpinHasDecision || !encoderHealthy()) {
    return false;
  }

  if (!ENABLE_MOTOR_TAKEOVER || !motorDirectionCalibrated) return false;

  float speed = fabsf(omega);
  if (activeSpinPeakOmega < TAKEOVER_MIN_PEAK_REV_S) return false;
  if (speed > TAKEOVER_REV_S || speed < TAKEOVER_MIN_REV_S) return false;
  // Defer to the target-first planner until its alignment window is nearly
  // spent; this old always-reachable picker stays as the last resort.
  if (planActive && speed > PLAN_FALLBACK_REV_S) return false;

  float predAngle = predictStopAngle();
  int predWedge = wedgeAtAngle(predAngle);
  if (!predictedStopCouldBeDare(predAngle)) return false;

  float minForwardDeg = requiredTakeoverRunwayDeg(speed);
  float targetAngle = 0.0f;
  float runwayDeg = 0.0f;
  if (!chooseRandomSafeTargetAngle(spinDir, wheelAngleDeg(), minForwardDeg,
                                   targetAngle, runwayDeg)) {
    if (debugLog && !diagnosticCapture) {
      Serial.printf("# TK-SKIP no random safe runway cur=%.1f min=%.1f dir=%d\n",
                    wheelAngleDeg(), minForwardDeg, spinDir);
    }
    return false;
  }

  int curWedge = currentWedge();
  int targetWedge = wedgeAtAngle(targetAngle);
  recordSpinDecision(predAngle, predWedge, curWedge, true, targetWedge);
  takeoverDecelProfile = true;  // shape the fallback catch as one ramp too
  beginTakeover(spinDir, targetAngle, minForwardDeg);
  return true;
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
  bool safelyStopped = (mode == DONE || mode == IDLE || mode == SAFE_HOLD) && encoderHealthy() &&
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
    " p  attended two-leg direction probe: 9 deg FAS+ then FAS- back (safe wedge center only)\n"
    " s  print angle / wedge / velocity / sensor health\n"
    " d  arm high-rate RAM encoder capture (auto-dump after true stop)\n"
    " v  toggle live takeover logs (disabled while d capture is armed)\n"
    " m  print dare mask\n"
    " a  attended accel-ceiling probe (wheel sweeps 120 deg per stage)\n"
    " c  print calibration (friction fit, prediction error, accel ceiling)\n"
    " C  reset calibration to seed values\n"
    " ?  show this help"));
}

void status() {
  uint32_t ageUs = encoderPrimed ? (uint32_t)(micros() - lastGoodUs) : 0;
  Serial.printf("# sensor_pos=%s velocity=%s age_us=%lu angle=%.2f wedge=%d omega=%.4f predStop=%.1f(w%d) mode=%u\n",
                encoderPositionFresh() ? "FRESH" : "STALE",
                encoderVelocityValid ? "VALID" : "REPRIME",
                (unsigned long)ageUs, wheelAngleDeg(), currentWedge(), omega,
                predictStopAngle(), predictStopWedge(), (unsigned)mode);
  Serial.printf("# cal cw(c=%.3f b=%.3f) ccw(c=%.3f b=%.3f) maeDeg=%.1f/%.1f margin=%.1f accel=%u plan=%d tgtW=%d\n",
                cw_c, cw_b, ccw_c, ccw_b, predMaeDeg[0], predMaeDeg[1],
                predictionDareMarginDeg(), (unsigned)accelCeilingSps2,
                planActive ? 1 : 0, planTargetWedge);
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
    case 'p':
      startDirectionProbe();
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
      Serial.println(F("# INVERT_DIR is fixed; use p to measure and persist the physical motor direction"));
      break;
    case 'a':
      startAccelProbe();
      break;
    case 'c':
      Serial.printf("# CAL friction cw(c=%.3f b=%.3f) ccw(c=%.3f b=%.3f) | maeDeg cw=%.1f ccw=%.1f margin=%.1f | accelCeiling=%u sps2\n",
                    cw_c, cw_b, ccw_c, ccw_b, predMaeDeg[0], predMaeDeg[1],
                    predictionDareMarginDeg(), (unsigned)accelCeilingSps2);
      break;
    case 'C':
      cw_c = 0.30f; cw_b = 0.15f; ccw_c = 0.30f; ccw_b = 0.15f;
      predMaeDeg[0] = PREDICTION_DARE_MARGIN_DEG;
      predMaeDeg[1] = PREDICTION_DARE_MARGIN_DEG;
      accelCeilingSps2 = ACCEL_CEILING_SPS2;
      persistCalibration();
      preferences.putUInt("accel", accelCeilingSps2);
      Serial.println(F("# CAL reset to seed values and persisted"));
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

  preferences.begin("prizewheel", false);
  motorPositiveEncoderSign = preferences.getInt("pos_sign", 0);
  motorDirectionCalibrated = preferences.getBool("dir_ok", false) &&
                             (motorPositiveEncoderSign == 1 || motorPositiveEncoderSign == -1);
  cw_c = preferences.getFloat("cw_c", cw_c);
  cw_b = preferences.getFloat("cw_b", cw_b);
  ccw_c = preferences.getFloat("ccw_c", ccw_c);
  ccw_b = preferences.getFloat("ccw_b", ccw_b);
  predMaeDeg[0] = preferences.getFloat("mae_cw", predMaeDeg[0]);
  predMaeDeg[1] = preferences.getFloat("mae_ccw", predMaeDeg[1]);
  accelCeilingSps2 = preferences.getUInt("accel", accelCeilingSps2);
  frictionResetSpin();

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

  Serial.printf("# high-speed takeover lockout=%d; recovery direction=%s (FAS+ encoder dir=%+d)\n",
                ENABLE_MOTOR_TAKEOVER ? 0 : 1,
                motorDirectionCalibrated ? "CALIBRATED" : "UNSET",
                motorPositiveEncoderSign);

  help();
  driverFreewheel();
  mode = IDLE;
}

void loop() {
  updateEncoder();
  handleSerial();

  bool sensorOK = encoderHealthy();
  uint32_t nowMs = millis();
  int32_t minSpinTravelCounts = (int32_t)lroundf(
      SPIN_CONFIRM_TRAVEL_DEG / 360.0f * 4096.0f);
  int32_t cancelBacktrackCounts = (int32_t)lroundf(
      SPIN_CANCEL_BACKTRACK_DEG / 360.0f * 4096.0f);
  bool spinConfirmed = false;

  // A weak throw only has to cross the arm threshold once.  Keep its candidate
  // alive through the slow coast, then confirm from real encoder travel in the
  // latched direction.  Requiring omega to stay above the threshold discarded
  // exactly the gentle spins this firmware needs to protect.
  bool mayArmWeakSpin = (mode == IDLE || mode == DONE);
  if (!sensorOK || !mayArmWeakSpin) {
    spinAboveMs = 0;
  } else if (spinAboveMs == 0) {
    if (fabsf(omega) >= SPIN_DETECT_REV_S) {
      spinAboveMs = nowMs;
      spinAboveStartCounts = encoderCountsMT;
      spinAboveDir = (omega >= 0.0f) ? 1 : -1;
    }
  } else {
    int32_t signedTravel = spinAboveDir *
        (encoderCountsMT - spinAboveStartCounts);
    if (signedTravel < -cancelBacktrackCounts ||
        nowMs - spinAboveMs > SPIN_CONFIRM_TIMEOUT_MS) {
      spinAboveMs = 0;
    } else if (nowMs - spinAboveMs >= SPIN_CONFIRM_MS &&
               signedTravel >= minSpinTravelCounts) {
      spinConfirmed = true;
    }
  }

  bool guestOverrideMoving = sensorOK &&
      fabsf(omega) > GUEST_OVERRIDE_REV_S;
  if (guestOverrideMoving) {
    if (guestOverrideAboveMs == 0) guestOverrideAboveMs = nowMs;
  } else {
    guestOverrideAboveMs = 0;
  }
  bool guestOverride = guestOverrideMoving &&
      nowMs - guestOverrideAboveMs >= GUEST_OVERRIDE_CONFIRM_MS;

  switch (mode) {
    case IDLE:
    case DONE:
      if (spinConfirmed) {
        sawSpinThisCycle = true;
        startSpinEvent(spinAboveDir);
        driverFreewheel();
        mode = FREE_SPIN;
      }
      break;

    case FREE_SPIN: {
      // Never predict or intervene from stale/invalid velocity.  A fresh
      // 20-30 ms valid window is required before control sees a new omega.
      if (!sensorOK) break;
      if (sawSpinThisCycle && !activeSpinHasDecision &&
          fabsf(omega) > activeSpinPeakOmega) {
        activeSpinPeakOmega = fabsf(omega);
      }
      float speed = fabsf(omega);
      frictionSample();
      if (!refPredValid && sawSpinThisCycle && speed <= 0.45f &&
          speed >= 2.0f * TAKEOVER_MIN_REV_S) {
        refPredAngleDeg = predictStopAngle();
        refPredValid = true;
      }
      maybeUpdateSteeringPlan();
      if (tryPlannedTakeover()) break;
      if (trySlowDareGuard()) break;

      // Defer LEAVE until after the true settle window.  This keeps the guard
      // alive through a low-speed creep instead of declaring a safe outcome
      // from a single near-zero velocity sample.
      if (sawSpinThisCycle && speed <= STILL_REV_S) {
        mode = SETTLE;
        settleT0 = 0;
      }
      break;
    }

    case PRECHARGE:
      if (!sensorOK) {
        // A valid position with a re-priming velocity window is transient;
        // retain the harmless 100mA precharge rather than cancelling capture.
        if (encoderPositionFresh()) break;
        driverFreewheel();
        sawSpinThisCycle = true;
        mode = FREE_SPIN;
        settleT0 = 0;
        break;
      }
      if (guestOverride) {
        driverFreewheel();
        sawSpinThisCycle = true;
        startSpinEvent((omega >= 0.0f) ? 1 : -1);
        mode = FREE_SPIN;
        break;
      }
      if (takeoverSensedSpeedup()) {
        // Even the low-current phase catch was helping the wheel.  Do not
        // issue a single STEP pulse; return to a fully floating free coast.
        driverFreewheel();
        mode = FREE_SPIN;
        settleT0 = 0;
        break;
      }
      if (millis() - takeoverMotorEnableMs < TAKEOVER_PRECHARGE_MS) break;
      if (!launchPrechargedTakeover()) {
        // No short, safe forward target remains after the precharge.  Release
        // rather than invent a longer powered rotation.
        driverFreewheel();
        mode = SETTLE;
        settleT0 = 0;
      }
      break;

    case TAKEOVER:
      if (!sensorOK) {
        // A valid position sample with an invalid velocity window is a normal
        // 20-30 ms re-prime, not an encoder loss.  Keep the one planner move
        // intact until motion feedback returns.  A genuinely stale position
        // fails closed into holding torque rather than coasting through a dare.
        if (!encoderPositionFresh()) {
          stepper->forceStopAndNewPosition(0);
          driverActive(RECOVERY_HOLD_CURRENT_MA);
          recoveryHoldStartedMs = 0;
          mode = RECOVERY_HOLD;
          Serial.println(F("# TAKEOVER HOLD: position stale"));
        }
        break;
      }
      if (guestOverride) {
        driverFreewheel();
        sawSpinThisCycle = true;
        startSpinEvent((omega >= 0.0f) ? 1 : -1);
        mode = FREE_SPIN;
        break;
      }
      takeoverStep();
      break;

    case SETTLE: {
      if (!sensorOK) {
        settleT0 = 0;
        break;
      }
      if (guestOverride) {
        driverFreewheel();
        sawSpinThisCycle = true;
        startSpinEvent((omega >= 0.0f) ? 1 : -1);
        mode = FREE_SPIN;
        break;
      }
      float settleSpeed = fabsf(omega);
      if (trySlowDareGuard()) break;

      if (settleSpeed > STILL_REV_S) {
        settleT0 = 0;
      } else {
        if (settleT0 == 0) settleT0 = millis();
        if (millis() - settleT0 > SETTLE_MS) {
          int wedge = currentWedge();
          if (isDare(wedge)) {
            // A dare is never accepted as a final outcome.  The wheel is at a
            // true stop, so recovery can crawl in the already-latched spin
            // direction without the dangerous mid-spin phase catch.
            if (motorDirectionCalibrated && ENABLE_DARE_RECOVERY) {
              startDareRecovery();
            } else {
              Serial.println(F("# DARE BLOCKED: run attended p probe; no unsafe automatic move was made"));
              driverFreewheel();
              sawSpinThisCycle = false;
              mode = DONE;
            }
            break;
          }
          if (!activeSpinHasDecision) {
            float predAngle = predictStopAngle();
            recordSpinDecision(predAngle, wedgeAtAngle(predAngle),
                               wedge, false, -1);
          }
          updatePredictionError();
          frictionFinalizeSpin();
          persistCalibration();
          printLandedEvent();
          driverFreewheel();
          sawSpinThisCycle = false;
          mode = DONE;
        }
      }
      break;
    }

    case RECOVERY_PRECHARGE:
      if (!encoderPositionFresh()) {
        driverActive(RECOVERY_HOLD_CURRENT_MA);
        recoveryHoldStartedMs = 0;
        mode = RECOVERY_HOLD;
        Serial.println(F("# RECOVERY PRECHARGE HOLD: position stale"));
        break;
      }
      if (millis() - recoveryStartedMs >= TAKEOVER_PRECHARGE_MS) {
        launchDareRecoveryMove();
      }
      break;

    case DARE_RECOVERY:
      // Recovery needs fresh position, not a freshly-computed velocity.  One
      // rejected I2C sample invalidates omega by design but must not abandon a
      // verified forward crawl with the wheel still on a dare.
      if (!encoderPositionFresh()) {
        stepper->forceStopAndNewPosition(0);
        driverActive(RECOVERY_HOLD_CURRENT_MA);
        recoveryHoldStartedMs = 0;
        mode = RECOVERY_HOLD;
        Serial.println(F("# DARE RECOVERY HOLD: position stale; outputs held until sensing returns"));
        break;
      }
      serviceDareRecovery();
      break;

    case RECOVERY_HOLD: {
      if (!encoderMotionReady()) {
        recoveryHoldStartedMs = 0;
        break;
      }
      if (fabsf(omega) > STILL_REV_S) {
        recoveryHoldStartedMs = 0;
        break;
      }
      if (recoveryHoldStartedMs == 0) {
        recoveryHoldStartedMs = millis();
        break;
      }
      if (millis() - recoveryHoldStartedMs < RECOVERY_HOLD_MS) break;

      if (isDare(currentWedge())) {
        if (recoveryAttempts < 3) {
          startDareRecovery();
        } else {
          // Holding is safer than declaring a dare outcome or issuing a
          // reverse correction.  This is a hardware/calibration fault.
          Serial.println(F("# DARE RECOVERY FAILED: held; do not use until inspected"));
          recoveryHoldStartedMs = millis();
        }
        break;
      }

      updatePredictionError();
      frictionFinalizeSpin();
      persistCalibration();
      printLandedEvent();
      sawSpinThisCycle = false;
      safeHoldStillSinceMs = millis();
      mode = SAFE_HOLD;
      break;
    }

    case SAFE_HOLD:
      // Hold the verified safe wedge only briefly, then float the coils: a
      // guest idly rocking the wheel between spins must feel a free wheel,
      // not a motor detent.  A stationary balanced wheel does not drift.
      if (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S) {
        driverFreewheel();
        spinAboveMs = 0;
        settleT0 = 0;
        mode = IDLE;
        break;
      }
      if (safeHoldStillSinceMs != 0 &&
          millis() - safeHoldStillSinceMs >= SAFE_HOLD_RELEASE_MS) {
        driverFreewheel();
        safeHoldStillSinceMs = 0;
        mode = DONE;
      }
      break;

    case DIR_PROBE:
      serviceDirectionProbe();
      break;

    case ACCEL_PROBE:
      serviceAccelProbe();
      break;
  }

  serviceDiagnosticCapture();
}
