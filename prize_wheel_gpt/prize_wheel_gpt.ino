/* ============================================================================
 * prize_wheel_gpt.ino - Prize wheel firmware, correctness redesign
 *
 * Every genuine hand spin is captured mid-coast and guided to a random safe
 * wedge interior, continuing in the guest's direction with a monotonically
 * decreasing command speed.  Wedges 1 and 5 (dares) are never a controlled
 * landing.  Manual repositioning of a stopped wheel is left untouched.
 *
 * State machine:
 *   IDLE_STOPPED -> MOTION_CANDIDATE -> (MANUAL_ADJUSTMENT | SPIN_PUSH)
 *   SPIN_PUSH -> SPIN_RELEASED -> TARGET_RESERVED -> SPEED_MATCH_CAPTURE
 *   -> CONTROLLED_DECEL -> LANDING_SETTLE -> SOFT_HOLD -> IDLE_STOPPED
 *   Any powered state -> FAULT_LATCHED on hardware/invariant failure.
 *
 * Hard rules enforced here:
 *   - Spin detection is purely kinematic; target availability never delays or
 *     reclassifies a spin.
 *   - The motor only ever brakes: command speed never increases after capture,
 *     starts below a trailing-minimum wheel speed, and chases the wheel down
 *     if the wheel is ever slower than the field.
 *   - Direction never reverses; FAS polarity comes only from the attended
 *     two-leg probe (NVS), never from INVERT_DIR.
 *   - Normal landings taper through stopMove() at the planned deceleration.
 *     forceStop() exists only in the emergency fault path.
 *   - TMC current changes only on stage transitions, never periodically.
 *   - Landing requires BOTH safe-interior position AND stillness.
 *
 * Coordinate frame (label-true, owner-verified 2026-07-29 - do not change):
 *   wheelAngle counts = (rawZero - AS5600raw) mod 4096, rawZero NVS "rawZero"
 *   (default 3807).  Raw decreases as clockwise wheel angle increases.  'z'
 *   stores the current raw as rawZero.  See HANDOFF_CHATGPT_FRAME.md.
 *
 * Hardware: ESP32-WROOM-32, BTT TMC2209 V1.3 (UART), NEMA17, 2:1 GT2 belt,
 *           AS5600 on the wheel shaft.
 * Build:    ESP32 Arduino core 3.3.10, FastAccelStepper 1.2.7, TMCStepper.
 * ========================================================================== */

#include <Wire.h>
#include <Preferences.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
// Party additions (WiFi + FX + sanctioned fixes S1/S2/S3): declarations,
// config switches and the serial mirror.  Implementations are included at the
// very bottom of this file.  See PARTY_TASK.md / DELIVERY.md.
#include "pw_party.h"

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

/* ------------------------- ENUMS (all hoisted) ---------------------------- */
enum State : uint8_t {
  ST_IDLE_STOPPED, ST_MOTION_CANDIDATE, ST_MANUAL_ADJUSTMENT,
  ST_SPIN_PUSH, ST_SPIN_RELEASED, ST_TARGET_RESERVED,
  ST_SPEED_MATCH_CAPTURE, ST_CONTROLLED_DECEL, ST_LANDING_SETTLE,
  ST_SOFT_HOLD, ST_DIR_PROBE, ST_FAULT_LATCHED
};

enum FaultCode : uint8_t {
  FC_NONE = 0,
  FC_ENCODER_STALE,        // position freshness lost during control
  FC_ENCODER_VELOCITY,     // velocity window unusable for too long in control
  FC_DIR_CAL_INVALID,      // probe failed / stuck DIR line
  FC_TMC_UART,             // driver UART check failed
  FC_MOTOR_FIGHT,          // wheel far below field speed: slip/DIR/belt fault
  FC_UNEXPECTED_REVERSAL,  // sustained motion opposite the latched spin dir
  FC_SUSTAINED_SPEEDUP,    // wheel accelerating under power beyond noise
  FC_POSITION_IMPOSSIBLE,  // remaining distance outside physical possibility
  FC_TAKEOVER_TIMEOUT,     // controlled phase exceeded its hard time budget
  FC_STEPPER_API,          // FastAccelStepper call returned an error
  FC_MONOTONIC_VIOLATION,  // internal: computed command tried to increase
  FC_TARGET_INVARIANT,     // internal: selected target was a dare
  FC_LANDING_UNSAFE        // settled on a dare after a controlled attempt
};

enum CurrentStage : uint8_t {
  CS_FREEWHEEL, CS_PRECHARGE, CS_CAPTURE, CS_BRAKE, CS_TAPER, CS_HOLD1, CS_HOLD2
};

enum SpinResult : uint8_t {
  RES_NONE = 0,
  RES_CONTROLLED_SAFE,     // settled inside the selected safe interior
  RES_EDGE_SAFE,           // settled in a safe wedge, but nearer an edge
  RES_OFF_TARGET_SAFE,     // settled in a different safe wedge (honest miss)
  RES_GUEST_STOPPED,       // external contact stopped the wheel pre-engage
  RES_GUEST_RESPUN,        // guest re-spun during control; superseded
  RES_NO_REACHABLE_SAFE,   // physics: no safe point was brake-reachable
  RES_CONTROL_LOCKED,      // calibration/driver lock: spin observed only
  RES_FAULTED              // see fault code
};

// Declared with the other types so Arduino's auto-generated prototypes for
// chooseSafeTarget() see it (core 3.3.10 inserts prototypes above the sketch
// body, before any later struct definition would be reached).
struct TargetChoice {
  bool found;
  int wedge;
  float runwayDeg;         // forward distance, may exceed 360 (multi-lap)
  float targetAngleDeg;    // display only; control uses runway counts
  uint32_t decelCapSps2;
  uint8_t quality;         // 0 wedge-uniform, 1 nearest-interior, 2 edge-safe
};

void enterFault(FaultCode code, const char* detail);  // used across sections
float dareDistanceDeg(float angle);                   // used across sections

/* --------------------------- DARE / SAFE --------------------------------- */
uint16_t dare_mask = (1 << 1) | (1 << 5);  // wedges 1 and 5 are never targets
inline bool isDare(int wedge) {
  wedge %= NUM_WEDGES;
  if (wedge < 0) wedge += NUM_WEDGES;
  return (dare_mask >> wedge) & 1;
}

/* --------------------------- TUNING -------------------------------------- */
#define RMS_CURRENT_MA 1450
// FAS polarity is an electrical wiring constant, not a wheel-direction
// calibration.  The attended p probe measures and persists the real sign.
bool INVERT_DIR = true;
static const int ENCODER_DIR_SIGN = -1;  // raw decreases clockwise; fixed

// --- spin / manual classification (kinematic only; no target dependence) ---
const float MOTION_EPS_REV_S          = 0.030f; // wake from IDLE
const float SPIN_DETECT_REV_S         = 0.12f;  // deliberate-spin speed floor
const uint16_t SPIN_CONFIRM_MS        = 60;
const float SPIN_CONFIRM_TRAVEL_DEG   = 6.0f;
const float SPIN_CANCEL_BACKTRACK_DEG = 2.0f;
const uint16_t SPIN_ARM_TIMEOUT_MS    = 1200;
const uint16_t MANUAL_CLASSIFY_MS     = 700;    // slow motion this old = manual
const uint16_t RELEASE_MIN_AGE_MS     = 400;    // hand-release gate
const float RELEASE_PEAK_FRACTION     = 0.92f;
const uint16_t RELEASE_DECAY_MS       = 150;    // no new peak for this long
const float GUEST_OVERRIDE_REV_S      = 0.80f;
const uint16_t GUEST_OVERRIDE_MS      = 60;
const float CONTACT_DECEL_REV_S2      = 1.0f;   // decel beyond any free coast
const uint16_t CONTACT_CONFIRM_MS     = 100;

// --- engagement / reachability ---
// Owner spec: capture right after release, while the wheel is fast - the
// takeover shadows the wheel and the whole slowdown reads as one natural
// coast.  Ceiling set by FastAccelStepper's ESP32 MCPWM pulse limit
// (~5 kHz = 0.78 rev/s at the wheel): 0.95 x 0.72 = 4.4 kHz.
const float ENGAGE_MAX_REV_S          = 0.72f;  // reserve once at/below this
const float ENGAGE_URGENCY_WINDOW_DEG = 60.0f;  // engage before an OPEN window
                                                // narrows past the largest
                                                // safe-interior gap (46 deg)
// Equivalent free-coast distance lost before braking is effective: 80 ms
// precharge (no braking) plus ~250 ms pickup at partial braking authority.
const float ENGAGE_LATENCY_S          = 0.22f;
const float MIN_BRAKE_HEADROOM_DEG    = 15.0f;
// A brake-only capture cannot use the full natural coast: the trailing phase
// bleeds energy through pole-slip drag before coupling.  Targets are capped
// at this fraction of the predicted natural stop distance, minus the margin.
const float NATURAL_REACH_FRACTION    = 0.90f;
const float NATURAL_SHAVE_MARGIN_DEG  = 5.0f;   // brake shaves, never adds
// A SILENT capture must let the wheel couple to the field and stay coupled:
// the planned profile deceleration may exceed the wheel's natural decel at
// capture only by the small load a synchronized rotor absorbs without pole
// hopping.  The slowdown therefore rides essentially the natural coast into
// the target (owner spec).  Applies to the wedge-uniform pass; the weak-spin
// assist passes may still slip briefly by design.
const float COUPLE_MARGIN             = 1.05f;
const float SAFE_WEDGE_EDGE_MARGIN_DEG = 8.0f;  // target interior margin
const float LANDING_INTERIOR_MIN_DEG  = 5.0f;   // verification margin
const float DARE_PROXIMITY_FAULT_DEG  = 2.0f;   // settle this close to a dare
                                                // boundary = unsafe landing
const uint16_t PRECHARGE_MS           = 80;
// The pulse train STARTS at the 100 mA precharge level with the field
// already sweeping at ~95% of wheel speed; full capture torque steps in
// only after this delay, onto an already-synchronized pair.  Stepping
// 600 mA onto a static field just before the pulses was the audible
// capture tick.
const uint16_t CAPTURE_CURRENT_DELAY_MS = 120;
const uint16_t PICKUP_COHERENCE_MS    = 250;
// Friction-model bootstrap: while a direction has fewer than the persist
// threshold of valid fits, defer engagement (bounded by window width and
// time) so the release coast can feed the online fit.
const float CAL_DEFER_MIN_WIDTH_DEG   = 120.0f;
const uint32_t CAL_DEFER_MAX_MS       = 3500;
// Below this runway a capture cannot launch cleanly (precharge advance plus
// the 7-deg launch floor); reserving would only flutter reserve/abandon.
const float MIN_RESERVE_RUNWAY_DEG    = 8.0f;

// --- braking profile ---
const uint16_t CMD_UPDATE_MS          = 25;     // control tick
const uint16_t CMD_RESYNC_MS          = 200;    // field-vs-command resync
const float CMD_RESYNC_TOLERANCE      = 0.08f;  // relative field deviation
// Capture entry fraction (owner spec 0.95): the field starts just under the
// wheel so coupling is near-immediate and gentle.  Applied to the trailing
// MINIMUM over ~200 ms, which already discounts filter lag, so the command
// still starts strictly behind the physical wheel.
const float TRAIL_FRACTION            = 0.95f;
const uint16_t TRAIL_WINDOW_TICKS     = 8;      // ~200 ms trailing window
const float CAPTURE_MAX_CMD_REV_S     = 0.68f;  // 0.95 x engage ceiling
const uint32_t DECEL_CEILING_SPS2     = 650;    // natural-motion decel ceiling
const uint32_t ASSIST_DECEL_MAX_SPS2  = 1100;   // weak-spin nearest-target cap
const float COUPLING_SLACK_REV_S      = 0.020f;
const float FAS_MIN_CMD_REV_S         = 0.00625f; // 40 Hz taper floor
const float STOP_GATE_EXTRA_DEG       = 2.0f;
const float OVERSHOOT_TOL_DEG         = 4.0f;
// Above the phase-capture snap transient (~0.03-0.075 rev/s observed), below
// any deliberate pull; the fault still needs a sustained rise.
const float SPEEDUP_NOISE_REV_S       = 0.050f;
const uint16_t SPEEDUP_TRIM_MS        = 30;     // trim command after this
const uint16_t SPEEDUP_FAULT_MS       = 400;    // latch fault after this
const float FIGHT_SPEED_FRACTION      = 0.45f;
const uint16_t FIGHT_GRACE_MS         = 150;
const uint16_t FIGHT_CONFIRM_MS       = 150;
const float FIGHT_MIN_CMD_REV_S       = 0.060f; // below this, stall != fight
const float OPPOSITE_ABORT_REV_S      = 0.050f;
const uint16_t OPPOSITE_ABORT_MS      = 75;
const uint16_t VELOCITY_LOSS_FAULT_MS = 300;
const uint16_t RESERVED_VEL_TIMEOUT_MS = 500;   // velocity wait cap in precharge
const uint16_t ENCODER_OUTAGE_FAULT_MS = 1000;  // encoder loss in motion states
const uint32_t TAKEOVER_TIMEOUT_MS    = 30000;
// A slow final crawl legitimately restarts the stillness window several
// times; the timeout exists for a genuinely never-still wheel (hardware).
const uint32_t SETTLE_TIMEOUT_MS      = 20000;
// Pre-stillness settle travel beyond this cannot be residual creep under the
// taper detent (friction-only coast from a 0.1 rev/s handoff is ~31 deg
// unheld; the 300 mA detent cuts that well below a wedge): a hand is dragging.
const float LANDING_DRAG_ABORT_DEG    = 45.0f;
// FastAccelStepper's acceleration must out-pace the command profile so the
// pulse generator can follow each 25 ms step-down and stepsToStop() stays
// well below the remaining runway (a 1:1 ratio degenerates into one
// open-loop ramp: stopMove would fire on the first tick).
const uint8_t FAS_TRACK_ACCEL_FACTOR  = 8;   // was 3: field descent lagged natural decel and carried the wheel (diag 2026-08-06)
const uint32_t FAS_TRACK_ACCEL_MAX_SPS2 = 6400; // was 2000 (=0.31 rev/s2 ceiling); wheel decays ~0.5 - field must descend faster than the wheel

// --- landing / hold ---
const float STILL_REV_S               = 0.020f;
const uint16_t SETTLE_MS              = 500;
const uint16_t HOLD1_MS               = 1500;
const uint16_t HOLD2_MS               = 1200;

// --- current ladder (written ONLY on stage transitions) ---
// With the profile-PACED command law the motor brakes through the load angle
// of a synchronized rotor, and holding that synchronization is what needs
// current: at 300 mA the rotor hops poles under the required drag (rattle),
// at 600 mA it stays locked and silent (owner bench ladder; 650 hums, 180
// rattles).  Current sets coupling stiffness; the braking force itself is
// set by the commanded profile.  (600/450 only over-braked under the old
// continuously-trailing law, which forced multi-pole slip at any current.)
const uint16_t CUR_PRECHARGE_MA = 100;  // phase settle, no snap
const uint16_t CUR_CAPTURE_MA   = 600;
const uint16_t CUR_BRAKE_MA     = 450;
const uint16_t CUR_TAPER_MA     = 300;  // final taper / settle watch
const uint16_t CUR_HOLD1_MA     = 150;  // fade...
const uint16_t CUR_HOLD2_MA     = 80;   // ...to freewheel

// --- direction probe ---
const uint16_t DIR_PROBE_CURRENT_MA = 350;
const uint32_t DIR_PROBE_SPEED_HZ   = 100;
const uint32_t DIR_PROBE_ACCEL_SPS2 = 300;
const int32_t  DIR_PROBE_USTEPS     = 160;   // 9 deg at the wheel
const float    DIR_PROBE_MIN_DEG    = 2.0f;
const float    DIR_PROBE_RETURN_TOL_DEG = 3.0f;
const uint32_t DIR_PROBE_TIMEOUT_MS = 7000;

// --- encoder (proven P1 pipeline; unchanged) ---
const uint32_t ENCODER_SAMPLE_PERIOD_US = 1000;
const uint32_t ENCODER_MAX_GOOD_GAP_US  = 20000;
const uint32_t ENCODER_FRESH_US         = 50000;
const float ENCODER_MAX_PLAUSIBLE_REV_S = 8.0f;
const int32_t ENCODER_DELTA_MARGIN_COUNTS = 16;
const uint32_t VELOCITY_WINDOW_US       = 30000;
const uint32_t VELOCITY_MIN_WINDOW_US   = 20000;
const uint32_t VELOCITY_FILTER_TAU_US   = 25000;
const uint8_t VELOCITY_HISTORY_LEN      = 64;

// --- friction model online fit (free-coast, per direction) ---
const uint32_t FIT_SAMPLE_PERIOD_MS = 100;  // 2.2 s of coast reaches min samples
const uint8_t FIT_MAX_SAMPLES = 96;
const uint8_t FIT_PAIR_STRIDE = 4;
const uint8_t FIT_MIN_SAMPLES = 22;
const float FIT_MIN_SPAN_RAD_S = 1.2f;
const float FIT_MIN_SPEED_REV_S = 0.045f;
const float FIT_MAX_SPEED_REV_S = 3.0f;
const float FIT_C_MIN = 0.02f, FIT_C_MAX = 3.0f;
const float FIT_B_MIN = 0.005f, FIT_B_MAX = 1.5f;
// Hand contact produces >5 rad/s2; a fast free coast legitimately reaches
// c+b*w ~ 1.5 rad/s2 at 1.3 rev/s (1.2 here rejected every hard spin's coast
// and starved the fit).
const float FIT_ALPHA_CONTACT_RAD_S2 = 2.5f;
const float FIT_BLEND = 0.35f;
const uint16_t FIT_PERSIST_MIN_FITS = 2;      // persist only once corroborated

/* --------------------------- STATE --------------------------------------- */
TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, TMC_ADDR);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper* stepper = nullptr;
Preferences preferences;

State state = ST_IDLE_STOPPED;
FaultCode faultCode = FC_NONE;
CurrentStage currentStage = CS_FREEWHEEL;
uint16_t g_currentMa = 0;   // actual commanded rms current, for telemetry
bool debugLog = false;
bool takeoverEnabled = true;
bool tmcOk = false;

uint16_t rawZero = 3807;  // label-true anchor (owner-measured 2026-07-28)
float omega = 0.0f;       // signed wheel rev/s; valid only when flagged

// direction calibration (attended p probe; persisted)
bool motorDirectionCalibrated = false;
int motorPositiveEncoderSign = 0;

// friction model, per direction: domega/dt = -(c + b*omega)  [rad/s units]
float cw_c = 0.30f, cw_b = 0.15f;
float ccw_c = 0.30f, ccw_b = 0.15f;
uint16_t cwFitCount = 0, ccwFitCount = 0;

/* ---------------------- ENCODER / VELOCITY CORE -------------------------- */
struct EncoderRead {
  bool ok;
  uint16_t raw;
  uint32_t doneUs;   // immediately after final received byte (or failed op)
  uint16_t i2cUs;
  uint8_t txStatus;
  uint8_t requested;
  uint8_t available;
};
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

/* ---------------------- HIGH-RATE RAM DIAGNOSTICS ------------------------- */
enum EncoderDiagFlag : uint8_t {
  DIAG_VALID      = 1 << 0,
  DIAG_TX_ERROR   = 1 << 1,
  DIAG_SHORT_READ = 1 << 2,
  DIAG_LONG_GAP   = 1 << 3,
  DIAG_ALIAS      = 1 << 4,
  DIAG_RATE       = 1 << 5,
  DIAG_PRIMED     = 1 << 6,
  DIAG_DIR_FLIP   = 1 << 7
};

struct DiagnosticSample {      // 28 bytes; 3072 samples = ~86 KB, ~3 s at 1 kHz
  uint32_t doneUs;
  int32_t counts;
  uint16_t raw;
  uint16_t dtGoodUs;
  int16_t delta;
  int16_t omegaMilliRevS;      // filtered velocity
  int16_t windowMilliRevS;     // unfiltered window slope
  int16_t cmdMilliRevS;        // commanded motor speed (wheel frame)
  int16_t fasMilliRevS;        // FastAccelStepper actual (wheel frame)
  int16_t remainDeciDeg;       // distance to target, 0.1 deg units
  uint16_t i2cUs;
  uint8_t flags;
  uint8_t state;
  uint8_t stage;               // current stage (defect-11 ladder position)
  uint8_t curMa10;             // commanded motor current / 10 mA
};

// 2944 x 28 bytes = ~80 KB static DRAM (~2.9 s at 1 kHz).  3072 overflowed
// dram0_0_seg by ~2 KB on core 3.3.10 with FastAccelStepper's MCPWM machinery.
// Party build: the WiFi/Network stack adds ~31 KB of static DRAM, which no
// longer coexists with the full buffer (measured link overflow: 31192 B).
// The diagnostic buffer is a bench instrument, not control logic; it yields
// the space while WiFi is compiled in and returns to full size with
// PW_WIFI_ENABLE 0.  See DELIVERY.md.
#if PW_WIFI_ENABLE
const uint16_t DIAG_CAPACITY = 1664;   // ~1.66 s at 1 kHz
#else
const uint16_t DIAG_CAPACITY = 2944;   // original bench capacity
#endif
DiagnosticSample diagnosticBuffer[DIAG_CAPACITY];
uint16_t diagnosticHead = 0;
uint16_t diagnosticCount = 0;
bool diagnosticWrapped = false;
bool diagnosticCapture = false;
bool diagnosticSawMotion = false;
uint32_t diagnosticStillSinceUs = 0;
float diagWindowRevS = 0.0f;   // latest unfiltered window slope for logging

/* --------------------------- SPIN RECORD --------------------------------- */
struct SpinRecord {
  uint32_t number;
  int8_t dir;
  float peakRevS;
  uint32_t pushStartMs;
  uint32_t releaseMs;          // 0 until release detected
  float releaseAngleDeg;
  float releaseRevS;
  int targetWedge;             // -1 until reserved
  float targetAngleDeg;
  float runwayDeg;
  float naturalStopDeg;        // estimate at reservation
  float fricC, fricB;
  float takeoverRevS;          // wheel speed at capture start
  float cmdMaxRevS;
  float cmdMinRevS;
  float maxSpeedRiseRevS;      // max post-capture wheel speed increase
  float maxDecelRevS2;         // max measured wheel deceleration
  float finalAngleDeg;
  int finalWedge;
  float targetErrDeg;
  SpinResult result;
  FaultCode fault;
  uint8_t targetQuality;       // 0 wedge-uniform, 1 nearest-int, 2 edge, 3 shadow
  uint8_t hadContact;          // sticky: external contact seen after release
};
SpinRecord spin;               // active spin; summary printed once at close
uint32_t spinCounter = 0;
bool spinOpen = false;
bool spinOpenedDuringFault = false;

/* --------------------------- CONTROL STATE -------------------------------- */
int spinDir = 1;               // latched at confirmation; never changes
uint32_t stateEnteredMs = 0;

// candidate/arm tracking (MOTION_CANDIDATE / MANUAL_ADJUSTMENT / promotion)
uint32_t spinArmMs = 0;
int32_t spinArmStartCounts = 0;
int spinArmDir = 1;
int32_t candidateStartCounts = 0;

// release detection
uint32_t lastPeakMs = 0;

// contact detection (external hand on a released wheel)
uint32_t releasedReverseSinceMs = 0;
uint32_t contactSinceMs = 0;
float prevContactRevS = 0.0f;
uint32_t prevContactMs = 0;

// guest override
uint32_t guestOverrideAboveMs = 0;

// reservation / takeover
int takeoverDir = 1;
int32_t targetCountsMT = 0;    // absolute multiturn encoder target
int32_t reserveCounts = 0;
float planDecelRevS2 = 0.05f;
uint32_t planDecelCapSps2 = DECEL_CEILING_SPS2;
uint32_t reserveMs = 0;
bool reserveRetried = false;
uint32_t captureStartMs = 0;
uint32_t controlStartMs = 0;
float cmdRevS = 0.0f;
uint32_t lastCmdTickMs = 0;
uint32_t lastAppliedHz = 0;
uint32_t lastResyncMs = 0;
float trailRing[TRAIL_WINDOW_TICKS];
uint8_t trailRingCount = 0;
uint8_t trailRingHead = 0;
float lowestForwardRevS = 0.0f;
uint32_t speedupSinceMs = 0;
uint32_t lastSpeedupTrimMs = 0;
uint32_t fightSinceMs = 0;
uint32_t oppositeSinceMs = 0;
uint32_t velocityLossSinceMs = 0;
bool stopRequested = false;
float prevTickWheelRevS = 0.0f;
uint32_t prevTickMs = 0;

// settle
uint32_t settleStillSinceMs = 0;
int32_t settleEntryCounts = 0;
int32_t settleWindowCounts = 0;   // position at start of the stillness window
uint32_t encoderOutageSinceMs = 0;

// direction probe
int32_t directionProbeStartCounts = 0;
int32_t directionProbeLeg1Counts = 0;
uint32_t directionProbeStartedMs = 0;
uint8_t directionProbeLeg = 0;
int directionProbePlusSign = 0;

// friction fit capture
struct FitSample { uint32_t ms; float omegaRadS; };
FitSample fitSamples[FIT_MAX_SAMPLES];
uint8_t fitSampleCount = 0;
int fitDir = 0;
uint32_t fitLastSampleMs = 0;
bool fitFinished = true;

/* ========================================================================== */
/*                         ENCODER IMPLEMENTATION                             */
/* ========================================================================== */
static inline int32_t staticCountsFromRaw(uint16_t raw) {
  int32_t sc = ((int32_t)rawZero - (int32_t)raw) % 4096;
  if (sc < 0) sc += 4096;
  return sc;
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

// Establish a fresh baseline.  A recovery after a long gap snaps to the whole
// turn nearest the running count; the label-true modulo-360 phase is restored
// exactly, so a blind gap can never change which wedge is which.
void primeEncoder(uint16_t raw, uint32_t doneUs, bool preserveNearestTurn) {
  int32_t staticCounts = staticCountsFromRaw(raw);
  if (!encoderPrimed || !preserveNearestTurn) {
    encoderCountsMT = staticCounts;
  } else {
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
      read.doneUs = micros();  // timestamp the acquisition, not its start
      uint32_t elapsedUs = read.doneUs - startUs;
      read.i2cUs = elapsedUs > 65535U ? 65535U : (uint16_t)elapsedUs;
      read.ok = true;
      return true;
    }
    while (Wire.available()) (void)Wire.read();
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
    diagWindowRevS = 0.0f;
    return;
  }
  diagWindowRevS = windowVelocity;
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

int16_t milliRevS(float value) {
  float scaled = value * 1000.0f;
  if (scaled > 32767.0f) return 32767;
  if (scaled < -32768.0f) return -32768;
  return (int16_t)lroundf(scaled);
}

float wheelAngleDeg() {
  double angle = fmod(angleDegMT, 360.0);  // label-true by construction
  if (angle < 0) angle += 360.0;
  return (float)angle;
}

int wedgeAtAngle(float angle) {
  angle = fmodf(angle, 360.0f);
  if (angle < 0.0f) angle += 360.0f;
  return ((int)(angle / WEDGE_DEG)) % NUM_WEDGES;
}

int currentWedge() { return wedgeAtAngle(wheelAngleDeg()); }

int32_t countsForDegrees(float degrees) {
  return (int32_t)lroundf(degrees * 4096.0f / 360.0f);
}

float degreesForCounts(int32_t counts) {
  return (float)counts * 360.0f / 4096.0f;
}

float forwardDistanceDeg(int dir, float fromAngle, float toAngle) {
  float forward = (dir > 0) ? (toAngle - fromAngle) : (fromAngle - toAngle);
  forward = fmodf(forward, 360.0f);
  if (forward < 0.0f) forward += 360.0f;
  return forward;
}

float fasWheelRevS() {
  if (!stepper) return 0.0f;
  return fabsf((float)stepper->getCurrentSpeedInMilliHz(true)) /
         (1000.0f * WHEEL_USTEPS_PER_REV);
}

float remainingTargetDeg() {
  return degreesForCounts(takeoverDir * (targetCountsMT - encoderCountsMT));
}

void recordDiagnostic(uint32_t dtGoodUs, int16_t delta, uint8_t flags) {
  if (!diagnosticCapture) return;
  const EncoderRead& read = encoderRead;
  DiagnosticSample& s = diagnosticBuffer[diagnosticHead];
  s.doneUs = read.doneUs;
  s.counts = encoderCountsMT;
  s.raw = read.ok ? read.raw : 0xFFFF;
  s.dtGoodUs = dtGoodUs > 65535U ? 65535U : (uint16_t)dtGoodUs;
  s.delta = delta;
  s.omegaMilliRevS = milliRevS(omega);
  s.windowMilliRevS = milliRevS(diagWindowRevS);
  s.cmdMilliRevS = milliRevS(cmdRevS);
  s.fasMilliRevS = milliRevS(fasWheelRevS());
  bool controlling = (state == ST_SPEED_MATCH_CAPTURE || state == ST_CONTROLLED_DECEL);
  float remain = controlling ? remainingTargetDeg() : 0.0f;
  if (remain > 3276.0f) remain = 3276.0f;
  if (remain < -3276.0f) remain = -3276.0f;
  s.remainDeciDeg = (int16_t)lroundf(remain * 10.0f);
  s.i2cUs = read.i2cUs;
  s.flags = flags;
  s.state = (uint8_t)state;
  s.stage = (uint8_t)currentStage;
  s.curMa10 = (uint8_t)(g_currentMa / 10 > 255 ? 255 : g_currentMa / 10);
  diagnosticHead = (diagnosticHead + 1) % DIAG_CAPACITY;
  if (diagnosticCount < DIAG_CAPACITY) ++diagnosticCount;
  else diagnosticWrapped = true;
}

void updateEncoder() {
  uint32_t nowUs = micros();
  if (!samplerScheduled) {
    samplerScheduled = true;
    nextSampleDueUs = nowUs;
  }
  if ((int32_t)(nowUs - nextSampleDueUs) < 0) return;

  // Keep a 1 kHz schedule when possible, but never burst-catch-up after a
  // stall; the measured completion-to-completion dt is what matters.
  nextSampleDueUs += ENCODER_SAMPLE_PERIOD_US;
  if ((int32_t)(nowUs - nextSampleDueUs) >= (int32_t)ENCODER_SAMPLE_PERIOD_US) {
    nextSampleDueUs = nowUs + ENCODER_SAMPLE_PERIOD_US;
  }

  readRawSample();
  EncoderRead& read = encoderRead;
  uint32_t dtGoodUs = encoderPrimed ? (read.doneUs - lastGoodUs) : 0;
  int16_t delta = 0;
  uint8_t flags = 0;

  if (!read.ok) {
    flags = (read.txStatus != 0) ? DIAG_TX_ERROR : DIAG_SHORT_READ;
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, 0, flags);
    return;
  }
  if (!encoderPrimed) {
    primeEncoder(read.raw, read.doneUs, false);
    recordDiagnostic(0, 0, DIAG_PRIMED);
    return;
  }
  if (dtGoodUs == 0 || dtGoodUs > ENCODER_MAX_GOOD_GAP_US) {
    // Cannot safely choose a turn count across an extended blind interval.
    primeEncoder(read.raw, read.doneUs, true);
    recordDiagnostic(dtGoodUs, 0, DIAG_LONG_GAP | DIAG_PRIMED);
    return;
  }

  int16_t rawDiff = (int16_t)read.raw - (int16_t)lastGoodRaw;
  delta = rawDiff;
  if (delta > 2048) delta -= 4096;
  if (delta < -2048) delta += 4096;

  int32_t absDelta = delta < 0 ? -(int32_t)delta : (int32_t)delta;
  if (absDelta == 2048) {
    // Exactly half a turn has no unique signed shortest-path interpretation.
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, delta, DIAG_ALIAS);
    return;
  }

  int32_t maxAllowed = ENCODER_DELTA_MARGIN_COUNTS + (int32_t)ceilf(
      ENCODER_MAX_PLAUSIBLE_REV_S * 4096.0f * (float)dtGoodUs / 1000000.0f);
  if (maxAllowed > 2047) maxAllowed = 2047;
  if (absDelta > maxAllowed) {
    // Leave lastGoodRaw/time untouched: the next genuine sample is compared
    // against the last trustworthy sample over its true longer interval.
    invalidateVelocity();
    recordDiagnostic(dtGoodUs, delta, DIAG_RATE);
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
  recordDiagnostic(dtGoodUs, delta, flags);
}

/* ========================================================================== */
/*                    DRIVER / CURRENT-STAGE MACHINE                          */
/* ========================================================================== */
// Bench-proven TMC2209 configuration, kept verbatim.
void driverConfig() {
  driver.begin();
  driver.I_scale_analog(false);   // internal Iref, NOT the Vref pot - critical
  driver.toff(4);
  driver.blank_time(24);
  driver.microsteps(16);
  driver.en_spreadCycle(true);    // SpreadCycle = torque, no RPM cap
  driver.pwm_autoscale(true);
  driver.rms_current(RMS_CURRENT_MA, 1.0);
  driver.TCOOLTHRS(0);
  driver.semin(0);                // CoolStep fully disabled
  driver.semax(0);
  driver.iholddelay(0);
  driver.TPOWERDOWN(255);
}

// The ONLY place TMC current registers are written after boot (the attended
// direction probe overrides the level once, via g_currentMa bookkeeping).
// Register traffic happens exclusively on stage transitions (defect-11), so
// the 1 kHz encoder sampling cadence is never disturbed by UART writes.
void setCurrentStage(CurrentStage next) {
  if (next == currentStage) return;
  bool outputsOk = true;
  switch (next) {
    case CS_FREEWHEEL:
      if (stepper) outputsOk = stepper->disableOutputs();
      driver.freewheel(1);
      driver.ihold(0);
      g_currentMa = 0;
      break;
    case CS_PRECHARGE:
      driver.freewheel(0);
      driver.rms_current(CUR_PRECHARGE_MA, 1.0);
      g_currentMa = CUR_PRECHARGE_MA;
      if (stepper) outputsOk = stepper->enableOutputs();
      break;
    case CS_CAPTURE:   driver.rms_current(CUR_CAPTURE_MA, 1.0); g_currentMa = CUR_CAPTURE_MA; break;
    case CS_BRAKE:     driver.rms_current(CUR_BRAKE_MA, 1.0);   g_currentMa = CUR_BRAKE_MA;   break;
    case CS_TAPER:     driver.rms_current(CUR_TAPER_MA, 1.0);   g_currentMa = CUR_TAPER_MA;   break;
    case CS_HOLD1:     driver.rms_current(CUR_HOLD1_MA, 1.0);   g_currentMa = CUR_HOLD1_MA;   break;
    case CS_HOLD2:     driver.rms_current(CUR_HOLD2_MA, 1.0);   g_currentMa = CUR_HOLD2_MA;   break;
  }
  if (!outputsOk) {
    Serial.printf("# WARN: enable/disable outputs returned false (stage %u)\n",
                  (unsigned)next);
  }
  currentStage = next;
}

// The single release primitive.  Order matters: the coils are floated FIRST
// (EN deasserted, freewheel mode), so clearing a still-draining pulse queue
// afterwards has zero mechanical effect - the forceStop here is a queue
// reset of a dead output stage, not a braking action.  Without it, a stale
// stopMove() ramp can survive for seconds and a later capture would energize
// the motor onto an uncorrelated pulse train / reset position while moving.
void driverFreewheel() {
  setCurrentStage(CS_FREEWHEEL);
  if (stepper) {
    if (stepper->isRunning()) stepper->forceStop();
    stepper->setJumpStart(0);
  }
  cmdRevS = 0.0f;   // dead control episode must not linger in telemetry
}

// Blocking UART read (~ms).  Called only with the wheel at rest.  The raw
// form only updates tmcOk; checkTmcUartOrFault() latches FC_TMC_UART so a
// driver-comm failure is a visible, recoverable fault ('r' re-checks) rather
// than a silent permanent soft-lock.
bool checkTmcUartRaw() {
  uint8_t result = driver.test_connection();
  tmcOk = (result == 0);
  if (!tmcOk) {
    Serial.printf("# TMC UART FAIL code=%u: takeover locked until r re-check passes\n", result);
  }
  return tmcOk;
}

void checkTmcUartOrFault() {
  if (!checkTmcUartRaw()) enterFault(FC_TMC_UART, "test_connection failed");
}

/* ========================================================================== */
/*                    FRICTION MODEL + ONLINE FIT                             */
/* ========================================================================== */
float naturalStopDistanceDeg(float speedRevS, int dir) {
  float c = (dir > 0) ? cw_c : ccw_c;
  float b = (dir > 0) ? cw_b : ccw_b;
  if (speedRevS < 1e-3f) return 0.0f;
  float w = speedRevS * TWO_PI;
  float travelRad = (1.0f / b) * w - (c / (b * b)) * logf(1.0f + b * w / c);
  return travelRad * RAD_TO_DEG;
}

// Minimum stopping distance WITH the motor braking: friction and the motor
// act together, so the model is the same coast integral with the constant
// term raised by the motor's braking authority (extraRadS2, in rad/s^2).
// Computing the brake distance from the motor ceiling ALONE is wrong - at
// speed the natural friction decel exceeds the motor ceiling and that error
// made every window in the previous revision empty.
float brakedStopDistanceDeg(float speedRevS, int dir, float extraRadS2) {
  float c = ((dir > 0) ? cw_c : ccw_c) + extraRadS2;
  float b = (dir > 0) ? cw_b : ccw_b;
  if (speedRevS < 1e-3f) return 0.0f;
  float w = speedRevS * TWO_PI;
  float travelRad = (1.0f / b) * w - (c / (b * b)) * logf(1.0f + b * w / c);
  return travelRad * RAD_TO_DEG;
}

float motorExtraRadS2(uint32_t sps2) {
  return (float)sps2 / WHEEL_USTEPS_PER_REV * TWO_PI;
}

// Instantaneous natural friction deceleration at a given speed, rev/s^2.
float naturalDecelRevS2(float speedRevS, int dir) {
  float c = (dir > 0) ? cw_c : ccw_c;
  float b = (dir > 0) ? cw_b : ccw_b;
  return (c + b * speedRevS * TWO_PI) / TWO_PI;
}

void resetFrictionCapture(int dir) {
  fitSampleCount = 0;
  fitDir = dir;
  fitLastSampleMs = 0;
  fitFinished = false;
}

// Samples are taken ONLY while the wheel free-coasts after hand release
// (motor floating, guest's hand off).  Contact, motor power, invalid encoder
// data, reversal, and speed-ups are all excluded before the fit.
void captureFrictionSample(uint32_t nowMs) {
  if (fitFinished || fitDir == 0 || fitSampleCount >= FIT_MAX_SAMPLES) return;
  if (!encoderVelocityValid) return;
  float forward = omega * (float)fitDir;
  if (forward < FIT_MIN_SPEED_REV_S || forward > FIT_MAX_SPEED_REV_S) return;
  if (fitSampleCount > 0 && nowMs - fitLastSampleMs < FIT_SAMPLE_PERIOD_MS) return;
  fitSamples[fitSampleCount].ms = nowMs;
  fitSamples[fitSampleCount].omegaRadS = forward * TWO_PI;
  ++fitSampleCount;
  fitLastSampleMs = nowMs;
}

void finishFrictionCapture(const char* reason) {
  if (fitFinished) return;
  fitFinished = true;
  if (fitDir == 0 || fitSampleCount < FIT_MIN_SAMPLES) return;
  float span = fitSamples[0].omegaRadS - fitSamples[fitSampleCount - 1].omegaRadS;
  if (span < FIT_MIN_SPAN_RAD_S) return;

  float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f;
  int pairs = 0;
  for (uint8_t i = 0; i + FIT_PAIR_STRIDE < fitSampleCount; ++i) {
    uint8_t j = i + FIT_PAIR_STRIDE;
    float dtS = (float)(fitSamples[j].ms - fitSamples[i].ms) / 1000.0f;
    if (dtS < 0.2f || dtS > 3.0f) continue;
    float dropRadS = fitSamples[i].omegaRadS - fitSamples[j].omegaRadS;
    if (dropRadS <= 0.0f) continue;          // reversal / speed-up: reject
    float alpha = dropRadS / dtS;
    if (alpha > FIT_ALPHA_CONTACT_RAD_S2) {  // hand contact: reject whole coast
      Serial.printf("SPIN#%lu FRICTION_REJECT contact alpha=%.3f reason=%s\n",
                    (unsigned long)spin.number, alpha, reason);
      PW_S3_COUNT_REJECT();
      return;
    }
    float x = 0.5f * (fitSamples[i].omegaRadS + fitSamples[j].omegaRadS);
    sx += x; sy += alpha; sxx += x * x; sxy += x * alpha;
    ++pairs;
  }
  if (pairs < (int)(FIT_MIN_SAMPLES - FIT_PAIR_STRIDE)) return;
  float denom = (float)pairs * sxx - sx * sx;
  if (fabsf(denom) < 1e-3f) return;
  float fitB = ((float)pairs * sxy - sx * sy) / denom;
  float fitC = (sy - fitB * sx) / (float)pairs;
  if (fitC < FIT_C_MIN || fitC > FIT_C_MAX || fitB < FIT_B_MIN || fitB > FIT_B_MAX) {
    Serial.printf("SPIN#%lu FRICTION_REJECT bounds fitC=%.4f fitB=%.4f pairs=%d reason=%s\n",
                  (unsigned long)spin.number, fitC, fitB, pairs, reason);
    PW_S3_COUNT_REJECT();
    return;
  }
  float keep = 1.0f - FIT_BLEND;
  if (fitDir > 0) {
    cw_c = keep * cw_c + FIT_BLEND * fitC;
    cw_b = keep * cw_b + FIT_BLEND * fitB;
    ++cwFitCount;
    if (cwFitCount >= FIT_PERSIST_MIN_FITS) {
      preferences.putFloat("cwC", cw_c);
      preferences.putFloat("cwB", cw_b);
      preferences.putUShort("cwN", cwFitCount);
    }
    Serial.printf("SPIN#%lu FRICTION dir=+1 fitC=%.4f fitB=%.4f pairs=%d -> c=%.4f b=%.4f fits=%u\n",
                  (unsigned long)spin.number, fitC, fitB, pairs, cw_c, cw_b, cwFitCount);
  } else {
    ccw_c = keep * ccw_c + FIT_BLEND * fitC;
    ccw_b = keep * ccw_b + FIT_BLEND * fitB;
    ++ccwFitCount;
    if (ccwFitCount >= FIT_PERSIST_MIN_FITS) {
      preferences.putFloat("ccwC", ccw_c);
      preferences.putFloat("ccwB", ccw_b);
      preferences.putUShort("ccwN", ccwFitCount);
    }
    Serial.printf("SPIN#%lu FRICTION dir=-1 fitC=%.4f fitB=%.4f pairs=%d -> c=%.4f b=%.4f fits=%u\n",
                  (unsigned long)spin.number, fitC, fitB, pairs, ccw_c, ccw_b, ccwFitCount);
  }
}

/* ========================================================================== */
/*                          TARGET SELECTION                                  */
/* ========================================================================== */
float randomUnit() { return (float)random(10000) / 10000.0f; }

// The motor is a brake.  A target is reachable only inside
//   [ latency + minBrake(ceiling) + headroom , naturalStop - shave ].
// Selection is wedge-uniform: each safe wedge is counted once even when its
// interior interval qualifies in two candidate laps, then one random point is
// drawn inside that wedge's qualifying interval.  Fallbacks for weak spins
// use a bounded assist deceleration and, last, the safest non-interior point;
// no fallback ever aims beyond the natural stop (a brake cannot add energy).

// Deficit-weighted selection state: how many times each wedge has been chosen
// by Pass 1 this power cycle.  RAM only - a reboot restarts the balancing.
static uint16_t wedgeChosenCount[NUM_WEDGES] = {0};
TargetChoice chooseSafeTarget(int dir, float curAngle, float speedRevS) {
  TargetChoice out;
  out.found = false;
  out.wedge = -1;
  out.runwayDeg = 0.0f;
  out.targetAngleDeg = 0.0f;
  out.decelCapSps2 = DECEL_CEILING_SPS2;
  out.quality = 0;

  const float interiorSpan = WEDGE_DEG - 2.0f * SAFE_WEDGE_EDGE_MARGIN_DEG;
  float latencyDeg = speedRevS * ENGAGE_LATENCY_S * 360.0f;
  float naturalDeg = naturalStopDistanceDeg(speedRevS, dir);
  float winMax = NATURAL_REACH_FRACTION * naturalDeg - NATURAL_SHAVE_MARGIN_DEG;
  float winMin = latencyDeg
               + brakedStopDistanceDeg(speedRevS, dir, motorExtraRadS2(DECEL_CEILING_SPS2))
               + MIN_BRAKE_HEADROOM_DEG;
  // Coupled-braking floor: runway short enough to need a profile steeper
  // than COUPLE_MARGIN x natural decel can only be reached by slip (rattle).
  float cmd0est = TRAIL_FRACTION * speedRevS;
  float coupledMinDeg = latencyDeg +
      (cmd0est * cmd0est) /
          (2.0f * COUPLE_MARGIN * naturalDecelRevS2(speedRevS, dir)) * 360.0f;
  if (coupledMinDeg > winMin) winMin = coupledMinDeg;
  float assistMin = latencyDeg
                  + brakedStopDistanceDeg(speedRevS, dir, motorExtraRadS2(ASSIST_DECEL_MAX_SPS2))
                  + 2.0f;

  // Pass 1: wedge-uniform among safe wedges reachable at the natural ceiling.
  int candWedge[NUM_WEDGES];
  float candLo[NUM_WEDGES], candHi[NUM_WEDGES];
  uint8_t candCount = 0;
  if (winMax > winMin + 2.0f) {
    for (int w = 0; w < NUM_WEDGES; ++w) {
      if (isDare(w)) continue;
      float interiorEntryAngle = (dir > 0)
          ? w * WEDGE_DEG + SAFE_WEDGE_EDGE_MARGIN_DEG
          : (w + 1) * WEDGE_DEG - SAFE_WEDGE_EDGE_MARGIN_DEG;
      float dNear = forwardDistanceDeg(dir, curAngle, interiorEntryAngle);
      float bestLo = 0.0f, bestHi = -1.0f, bestScore = 1.0e9f;
      for (int lap = 0; lap < 4; ++lap) {
        float a = dNear + 360.0f * lap;
        if (a > winMax) break;
        float b = a + interiorSpan;
        float lo = fmaxf(a, winMin);
        float hi = fminf(b, winMax);
        if (hi - lo < 2.0f) continue;
        // Prefer the lap whose usable interval sits nearest 70% of the
        // natural runway: comfortably reachable, minimally braked.
        float score = fabsf(0.5f * (lo + hi) - 0.70f * winMax);
        if (score < bestScore) { bestScore = score; bestLo = lo; bestHi = hi; }
      }
      if (bestHi > bestLo) {
        candWedge[candCount] = w;
        candLo[candCount] = bestLo;
        candHi[candCount] = bestHi;
        ++candCount;
      }
    }
  }
  if (candCount > 0) {
    // Deficit-weighted draw.  Every candidate in this array has ALREADY passed
    // the full reachability + safety test above, so re-weighting among them
    // changes nothing safety-critical - only which safe wedge wins.  Wheel
    // geometry starves wedges adjacent to the dare wedges (bench 2026-08-04:
    // W0 won 1 of 63 while W4/W9 won 11 each), and a wedge that never comes up
    // is as much a tell as a visible motor grab.  Favour the starved ones on
    // the occasions they are reachable.
    uint16_t maxChosen = 0;
    for (uint8_t i = 0; i < candCount; ++i) {
      uint16_t ct = wedgeChosenCount[candWedge[i]];
      if (ct > maxChosen) maxChosen = ct;
    }
    uint32_t weights[NUM_WEDGES];
    uint32_t totalWeight = 0;
    for (uint8_t i = 0; i < candCount; ++i) {
      weights[i] = 1u + (uint32_t)(maxChosen - wedgeChosenCount[candWedge[i]]);
      totalWeight += weights[i];
    }
    uint32_t r = (uint32_t)random((long)totalWeight);
    uint8_t pick = candCount - 1;
    for (uint8_t i = 0; i < candCount; ++i) {
      if (r < weights[i]) { pick = i; break; }
      r -= weights[i];
    }
    if (wedgeChosenCount[candWedge[pick]] < 60000u) ++wedgeChosenCount[candWedge[pick]];
    out.found = true;
    out.wedge = candWedge[pick];
    out.runwayDeg = candLo[pick] + (candHi[pick] - candLo[pick]) * randomUnit();
    out.decelCapSps2 = DECEL_CEILING_SPS2;
    out.quality = 0;
  }

  // Pass 2 (shadow capture): when the wedge-uniform window is unavailable
  // but the NATURAL stop point already sits safely inside a safe wedge,
  // capture and confirm THAT landing with (near) zero braking.  This beats
  // braking to a nearer interior: it is invisible, and it cannot convert a
  // naturally-safe weak spin into a dare landing through brake scatter
  // (observed on hardware: a spin dying safely was braked short into the
  // dare it was passing through).  Clearance is required over the expected
  // settle segment [natural-4, natural].
  if (!out.found && naturalDeg > 10.0f) {
    float natAng = fmodf(curAngle + (float)dir * naturalDeg, 360.0f);
    if (natAng < 0.0f) natAng += 360.0f;
    float settleAng = fmodf(curAngle + (float)dir * (naturalDeg - 4.0f), 360.0f);
    if (settleAng < 0.0f) settleAng += 360.0f;
    if (!isDare(wedgeAtAngle(natAng)) && !isDare(wedgeAtAngle(settleAng)) &&
        dareDistanceDeg(natAng) >= DARE_PROXIMITY_FAULT_DEG + 3.0f &&
        dareDistanceDeg(settleAng) >= DARE_PROXIMITY_FAULT_DEG + 3.0f) {
      out.found = true;
      out.wedge = wedgeAtAngle(settleAng);
      out.runwayDeg = naturalDeg - 2.0f;
      out.decelCapSps2 = ASSIST_DECEL_MAX_SPS2;
      out.quality = 3;
    }
  }

  // Pass 3: the natural stop is dare territory - brake to the nearest safe
  // interior, cushioned past the entry so stop scatter stays inside.
  if (!out.found && winMax > assistMin) {
    float bestDist = 1.0e9f;
    int bestW = -1;
    for (int w = 0; w < NUM_WEDGES; ++w) {
      if (isDare(w)) continue;
      float interiorEntryAngle = (dir > 0)
          ? w * WEDGE_DEG + SAFE_WEDGE_EDGE_MARGIN_DEG
          : (w + 1) * WEDGE_DEG - SAFE_WEDGE_EDGE_MARGIN_DEG;
      float dNear = forwardDistanceDeg(dir, curAngle, interiorEntryAngle);
      for (int lap = 0; lap < 4; ++lap) {
        float a = dNear + 360.0f * lap;
        float b = a + interiorSpan;
        if (a > winMax) break;
        float lo = fmaxf(a, assistMin);
        float hi = fminf(b, winMax);
        if (hi < lo) continue;
        float d = fminf(lo + 6.0f, hi);   // cushion past the interior entry
        if (d < bestDist) { bestDist = d; bestW = w; }
        break;
      }
    }
    if (bestW >= 0) {
      out.found = true;
      out.wedge = bestW;
      out.runwayDeg = bestDist;
      out.decelCapSps2 = ASSIST_DECEL_MAX_SPS2;
      out.quality = 1;
    }
  }

  // Pass 4: no interior reachable.  Stop at the point of the reachable band
  // deepest inside any safe wedge, requiring dare clearance at BOTH the
  // nominal target and the expected settle point (~2 deg short, stop-gate
  // undershoot) - a target landingVerdict would fault on must never be
  // reserved.
  if (!out.found && winMax > assistMin) {
    float bestScore = -1.0f, bestD = 0.0f;
    int bestEdgeW = -1;
    for (float d = assistMin; d <= winMax; d += 1.0f) {
      float ang = fmodf(curAngle + (float)dir * d, 360.0f);
      if (ang < 0.0f) ang += 360.0f;
      float settleAng = fmodf(curAngle + (float)dir * (d - 2.0f), 360.0f);
      if (settleAng < 0.0f) settleAng += 360.0f;
      int w = wedgeAtAngle(ang);
      if (isDare(w) || isDare(wedgeAtAngle(settleAng))) continue;
      if (dareDistanceDeg(ang) < DARE_PROXIMITY_FAULT_DEG + 1.0f ||
          dareDistanceDeg(settleAng) < DARE_PROXIMITY_FAULT_DEG + 1.0f) continue;
      float within = fmodf(settleAng, WEDGE_DEG);
      float edgeDist = fminf(within, WEDGE_DEG - within);
      if (edgeDist > bestScore) { bestScore = edgeDist; bestD = d; bestEdgeW = w; }
    }
    if (bestEdgeW >= 0) {
      out.found = true;
      out.wedge = bestEdgeW;
      out.runwayDeg = bestD;
      out.decelCapSps2 = ASSIST_DECEL_MAX_SPS2;
      out.quality = 2;
    }
  }

  if (out.found) {
    float ang = fmodf(curAngle + (float)dir * out.runwayDeg, 360.0f);
    if (ang < 0.0f) ang += 360.0f;
    out.targetAngleDeg = ang;
  }
  return out;
}

// Width of the currently reachable braking window; the urgency trigger keeps
// an OPEN window from ever shrinking below the largest safe-interior gap
// (46 deg).  A non-positive width means the window has not opened (or the
// wheel is nearly dead) - that is a "wait" or "fallback" condition, never an
// "engage now" condition.
float reachWindowWidthDeg(float speedRevS, int dir) {
  float latencyDeg = speedRevS * ENGAGE_LATENCY_S * 360.0f;
  float winMax = NATURAL_REACH_FRACTION * naturalStopDistanceDeg(speedRevS, dir)
               - NATURAL_SHAVE_MARGIN_DEG;
  float winMin = latencyDeg
               + brakedStopDistanceDeg(speedRevS, dir, motorExtraRadS2(DECEL_CEILING_SPS2))
               + MIN_BRAKE_HEADROOM_DEG;
  float cmd0est = TRAIL_FRACTION * speedRevS;
  float coupledMinDeg = latencyDeg +
      (cmd0est * cmd0est) /
          (2.0f * COUPLE_MARGIN * naturalDecelRevS2(speedRevS, dir)) * 360.0f;
  if (coupledMinDeg > winMin) winMin = coupledMinDeg;
  return winMax - winMin;
}

/* ========================================================================== */
/*                     FAS WRAPPERS (return-value checked)                    */
/* ========================================================================== */
void enterFault(FaultCode code, const char* detail);  // fwd

bool fasSetSpeedHz(uint32_t hz) {
  if (!stepper || stepper->setSpeedInHz(hz) != 0) {
    enterFault(FC_STEPPER_API, "setSpeedInHz");
    return false;
  }
  return true;
}

bool fasSetAcceleration(uint32_t sps2) {
  if (!stepper || stepper->setAcceleration((int32_t)sps2) != 0) {
    enterFault(FC_STEPPER_API, "setAcceleration");
    return false;
  }
  return true;
}

bool fasRun(int fasSign) {
  if (!stepper) { enterFault(FC_STEPPER_API, "no stepper"); return false; }
  MoveResultCode rc = (fasSign > 0) ? stepper->runForward() : stepper->runBackward();
  if (rc != MoveResultCode::OK) {
    enterFault(FC_STEPPER_API, (fasSign > 0) ? "runForward" : "runBackward");
    return false;
  }
  return true;
}

// Converts an encoder-space direction (+CW / -CCW) to the FAS sign persisted
// by the attended p calibration.  Deliberately independent of INVERT_DIR.
int fasSignForEncoderDirection(int encoderDir) {
  if (!motorDirectionCalibrated || motorPositiveEncoderSign == 0) return 0;
  return encoderDir == motorPositiveEncoderSign ? 1 : -1;
}

/* ========================================================================== */
/*                             TELEMETRY                                      */
/* ========================================================================== */
const char* stateName(State s) {
  switch (s) {
    case ST_IDLE_STOPPED: return "IDLE_STOPPED";
    case ST_MOTION_CANDIDATE: return "MOTION_CANDIDATE";
    case ST_MANUAL_ADJUSTMENT: return "MANUAL_ADJUSTMENT";
    case ST_SPIN_PUSH: return "SPIN_PUSH";
    case ST_SPIN_RELEASED: return "SPIN_RELEASED";
    case ST_TARGET_RESERVED: return "TARGET_RESERVED";
    case ST_SPEED_MATCH_CAPTURE: return "SPEED_MATCH_CAPTURE";
    case ST_CONTROLLED_DECEL: return "CONTROLLED_DECEL";
    case ST_LANDING_SETTLE: return "LANDING_SETTLE";
    case ST_SOFT_HOLD: return "SOFT_HOLD";
    case ST_DIR_PROBE: return "DIR_PROBE";
    case ST_FAULT_LATCHED: return "FAULT_LATCHED";
  }
  return "?";
}

const char* faultName(FaultCode f) {
  switch (f) {
    case FC_NONE: return "NONE";
    case FC_ENCODER_STALE: return "ENCODER_STALE";
    case FC_ENCODER_VELOCITY: return "ENCODER_VELOCITY";
    case FC_DIR_CAL_INVALID: return "DIR_CAL_INVALID";
    case FC_TMC_UART: return "TMC_UART";
    case FC_MOTOR_FIGHT: return "MOTOR_FIGHT";
    case FC_UNEXPECTED_REVERSAL: return "UNEXPECTED_REVERSAL";
    case FC_SUSTAINED_SPEEDUP: return "SUSTAINED_SPEEDUP";
    case FC_POSITION_IMPOSSIBLE: return "POSITION_IMPOSSIBLE";
    case FC_TAKEOVER_TIMEOUT: return "TAKEOVER_TIMEOUT";
    case FC_STEPPER_API: return "STEPPER_API";
    case FC_MONOTONIC_VIOLATION: return "MONOTONIC_VIOLATION";
    case FC_TARGET_INVARIANT: return "TARGET_INVARIANT";
    case FC_LANDING_UNSAFE: return "LANDING_UNSAFE";
  }
  return "?";
}

const char* resultName(SpinResult r) {
  switch (r) {
    case RES_NONE: return "NONE";
    case RES_CONTROLLED_SAFE: return "CONTROLLED_SAFE";
    case RES_EDGE_SAFE: return "EDGE_SAFE";
    case RES_OFF_TARGET_SAFE: return "OFF_TARGET_SAFE";
    case RES_GUEST_STOPPED: return "GUEST_STOPPED";
    case RES_GUEST_RESPUN: return "GUEST_RESPUN";
    case RES_NO_REACHABLE_SAFE: return "NO_REACHABLE_SAFE";
    case RES_CONTROL_LOCKED: return "CONTROL_LOCKED";
    case RES_FAULTED: return "FAULTED";
  }
  return "?";
}

void startSpinEvent(int confirmedDir, uint32_t nowMs) {
  ++spinCounter;
  memset(&spin, 0, sizeof(spin));
  spin.number = spinCounter;
  spin.dir = (int8_t)confirmedDir;
  spin.peakRevS = fabsf(omega);
  spin.pushStartMs = nowMs;
  spin.targetWedge = -1;
  spin.finalWedge = -1;
  spin.cmdMinRevS = 0.0f;
  spin.result = RES_NONE;
  spin.fault = FC_NONE;
  spin.fricC = (confirmedDir > 0) ? cw_c : ccw_c;
  spin.fricB = (confirmedDir > 0) ? cw_b : ccw_b;
  spinOpen = true;
  spinDir = confirmedDir;
  lastPeakMs = nowMs;
  contactSinceMs = 0;
  prevContactMs = 0;
  resetFrictionCapture(confirmedDir);
  fitFinished = true;  // sampling begins at hand release, not during the push
  Serial.printf("SPIN#%lu START dir=%+d omega=%.3f wedge=%d\n",
                (unsigned long)spin.number, spinDir, omega, currentWedge());
}

// One immutable per-spin summary.  Printed exactly once, at spin close.
void closeSpin(SpinResult result) {
  if (!spinOpen) return;
  spinOpen = false;
  spin.result = result;
  spin.fault = faultCode;
  spin.finalAngleDeg = wheelAngleDeg();
  spin.finalWedge = currentWedge();
  if (spin.targetWedge >= 0) {
    float err = spin.finalAngleDeg - spin.targetAngleDeg;
    err = fmodf(err + 540.0f, 360.0f) - 180.0f;
    spin.targetErrDeg = (float)spin.dir * err;
  }
  char fitSuffix[64] = "";   // S3: fit-rejection + fit-count telemetry
#if PW_S3_ENABLE
  snprintf(fitSuffix, sizeof(fitSuffix), " fitRej=%lu fitsCW=%u fitsCCW=%u",
           (unsigned long)fitRejectCount, cwFitCount, ccwFitCount);
#endif
  Serial.printf(
      "SPIN#%lu SUMMARY dir=%+d peak=%.3f releaseMs=%lu relAngle=%.1f relSpeed=%.3f "
      "targetW=%d targetAngle=%.1f runway=%.1f natStop=%.1f fricC=%.4f fricB=%.4f "
      "tkSpeed=%.3f cmdMax=%.3f cmdMin=%.3f rise=%.3f maxDecel=%.3f "
      "final=%.1f finalW=%d err=%.1f quality=%u result=%s fault=%s%s\n",
      (unsigned long)spin.number, spin.dir, spin.peakRevS,
      (unsigned long)(spin.releaseMs ? spin.releaseMs - spin.pushStartMs : 0),
      spin.releaseAngleDeg, spin.releaseRevS,
      spin.targetWedge, spin.targetAngleDeg, spin.runwayDeg,
      spin.naturalStopDeg, spin.fricC, spin.fricB,
      spin.takeoverRevS, spin.cmdMaxRevS, spin.cmdMinRevS,
      spin.maxSpeedRiseRevS, spin.maxDecelRevS2,
      spin.finalAngleDeg, spin.finalWedge, spin.targetErrDeg,
      spin.targetQuality, resultName(result), faultName(spin.fault), fitSuffix);
  if (isDare(spin.finalWedge)) {
    Serial.printf("SPIN#%lu LANDED-DARE wedge=%d THIS IS A FAILURE result=%s fault=%s\n",
                  (unsigned long)spin.number, spin.finalWedge,
                  resultName(result), faultName(spin.fault));
  }
}

/* ========================================================================== */
/*                              FAULT PATH                                    */
/* ========================================================================== */
void enterFault(FaultCode code, const char* detail) {
  if (state == ST_FAULT_LATCHED) return;
  faultCode = code;
  PW_S1_PERSIST(code);  // S1: latch survives a power cycle; only r clears
  Serial.printf("FAULT code=%s detail=%s state=%s angle=%.1f wedge=%d omega=%.3f cmd=%.3f\n",
                faultName(code), detail, stateName(state), wheelAngleDeg(),
                currentWedge(), omega, cmdRevS);
  // Safest physically reasonable shutdown.  A fighting or reversing motor is
  // actively harming the mechanism: abrupt stop is justified there and ONLY
  // there.  Everything else ramps down at the configured deceleration.
  if (stepper && stepper->isRunning()) {
    if (code == FC_MOTOR_FIGHT || code == FC_UNEXPECTED_REVERSAL) {
      stepper->forceStop();                 // emergency use only
    } else if (!stepper->isStopping()) {
      stepper->stopMove();
    }
  }
  // An open spin is NOT closed here: its summary must record the real resting
  // wedge, so ST_FAULT_LATCHED closes it once the wheel is actually still
  // (also prevents the residual coast from being re-counted as a new spin).
  spinOpenedDuringFault = false;
  state = ST_FAULT_LATCHED;
  stateEnteredMs = millis();
  settleStillSinceMs = 0;
}

void serviceFault() {
  // Finish any ramp-down, then float the coils and stay latched until 'r'.
  if (stepper && stepper->isRunning()) return;
  if (currentStage != CS_FREEWHEEL) driverFreewheel();
}

/* ========================================================================== */
/*                        DIRECTION PROBE (two-leg)                           */
/* ========================================================================== */
bool probeStartIsSafe() {
  int wedge = currentWedge();
  if (isDare(wedge) || isDare(wedge - 1) || isDare(wedge + 1)) return false;
  float within = fmodf(wheelAngleDeg(), WEDGE_DEG);
  float edgeMargin = fminf(within, WEDGE_DEG - within);
  return edgeMargin >= 11.0f;
}

void startDirectionProbe() {
  if (!stepper || !encoderPositionFresh()) {
    Serial.println(F("# DIR PROBE refused: encoder/stepper unavailable"));
    return;
  }
  if (state != ST_IDLE_STOPPED &&
      !(state == ST_FAULT_LATCHED && faultCode == FC_DIR_CAL_INVALID)) {
    Serial.println(F("# DIR PROBE refused: wait for a fully stopped wheel (or clear the fault with r)"));
    return;
  }
  if (stepper->isRunning()) {
    Serial.println(F("# DIR PROBE refused: pulse generator still draining"));
    return;
  }
  if (encoderVelocityValid && fabsf(omega) > STILL_REV_S) {
    Serial.println(F("# DIR PROBE refused: wheel must be at rest"));
    return;
  }
  if (!probeStartIsSafe()) {
    Serial.println(F("# DIR PROBE refused: center the pointer in safe wedge 3 or 7-11 first"));
    return;
  }
  // Attended, at-rest calibration.  The stepper is idle, so the position can
  // be re-zeroed without an abrupt-stop call.  Restore the base DIR polarity
  // first: captures repolarize the pin per direction (always-runForward).
  stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
  stepper->setCurrentPosition(0);
  stepper->setJumpStart(0);
  setCurrentStage(CS_PRECHARGE);
  driver.rms_current(DIR_PROBE_CURRENT_MA, 1.0);  // probe-specific level
  g_currentMa = DIR_PROBE_CURRENT_MA;
  if (!fasSetSpeedHz(DIR_PROBE_SPEED_HZ)) return;
  if (!fasSetAcceleration(DIR_PROBE_ACCEL_SPS2)) return;
  directionProbeStartCounts = encoderCountsMT;
  directionProbeStartedMs = millis();
  directionProbeLeg = 1;
  directionProbePlusSign = 0;
  if (stepper->move(DIR_PROBE_USTEPS) != MoveResultCode::OK) {
    probeFail("leg 1 move() rejected");
    return;
  }
  if (spinOpen) closeSpin(RES_CONTROL_LOCKED);  // never orphan an open record
  faultCode = FC_NONE;   // probe may be used to recover from DIR_CAL fault
  PW_S1_CLEAR();         // S1: mirror the RAM latch lifecycle exactly
  state = ST_DIR_PROBE;
  stateEnteredMs = millis();
  Serial.printf("# DIR PROBE leg 1/2: moving FAS+ %ld usteps; keep hands clear\n",
                (long)DIR_PROBE_USTEPS);
}

void probeFail(const char* why) {
  motorDirectionCalibrated = false;
  motorPositiveEncoderSign = 0;
  preferences.putBool("dir_ok", false);
  preferences.putInt("pos_sign", 0);
  driverFreewheel();
  Serial.printf("# DIR PROBE FAILED: %s. Takeover LOCKED until p passes.\n", why);
  // A failed probe is a hardware-facing calibration fault: latch it so the
  // failure is visible in status, not just one scrolled-away serial line.
  enterFault(FC_DIR_CAL_INVALID, why);
}

void serviceDirectionProbe() {
  if (stepper && stepper->isRunning()) {
    if (millis() - directionProbeStartedMs <= DIR_PROBE_TIMEOUT_MS) return;
    stepper->stopMove();
    probeFail("timeout");
    return;
  }

  int32_t delta = encoderCountsMT - directionProbeStartCounts;
  float degrees = degreesForCounts(delta);
  if (fabsf(degrees) < DIR_PROBE_MIN_DEG) {
    probeFail(directionProbeLeg == 2
              ? "leg 2 barely moved; suspect stuck DIR line"
              : "encoder barely moved; check belt/driver");
    return;
  }

  int legSign = degrees > 0.0f ? 1 : -1;
  if (directionProbeLeg == 1) {
    directionProbePlusSign = legSign;
    directionProbeLeg1Counts = delta;
    directionProbeStartCounts = encoderCountsMT;
    directionProbeStartedMs = millis();
    directionProbeLeg = 2;
    if (stepper->move(-DIR_PROBE_USTEPS) != MoveResultCode::OK) {
      probeFail("leg 2 move() rejected");
      return;
    }
    Serial.printf("# DIR PROBE leg 2/2: FAS+ moved encoder %+.2f deg; now FAS- back\n", degrees);
    return;
  }

  if (legSign == directionProbePlusSign) {
    // Both electrical directions moved the wheel the SAME way: the DIR pin
    // is not switching.  Clear calibration so nothing automatic can run.
    probeFail("FAS+ and FAS- moved the SAME direction - DIR line fault (GPIO27)");
    return;
  }
  float returnErrDeg = degreesForCounts(directionProbeLeg1Counts + delta);
  if (fabsf(returnErrDeg) > DIR_PROBE_RETURN_TOL_DEG) {
    probeFail("legs did not retrace (return error too large); check belt slip");
    return;
  }

  motorPositiveEncoderSign = directionProbePlusSign;
  motorDirectionCalibrated = true;
  preferences.putBool("dir_ok", true);
  preferences.putInt("pos_sign", motorPositiveEncoderSign);
  driverFreewheel();
  state = ST_IDLE_STOPPED;
  stateEnteredMs = millis();
  Serial.printf("# DIR PROBE PASS: FAS+ is encoder dir=%+d, retrace err=%.2f deg; takeover ENABLED\n",
                motorPositiveEncoderSign, returnErrDeg);
}

/* ========================================================================== */
/*                       CONTROL: RESERVE / CAPTURE / BRAKE                   */
/* ========================================================================== */
bool controlAvailable() {
  return takeoverEnabled && motorDirectionCalibrated && tmcOk &&
         fasSignForEncoderDirection(spinDir) != 0;
}

void trailRingReset(float seed) {
  for (uint8_t i = 0; i < TRAIL_WINDOW_TICKS; ++i) trailRing[i] = seed;
  trailRingCount = 1;
  trailRingHead = 0;
}

void trailRingPush(float value) {
  trailRing[trailRingHead] = value;
  trailRingHead = (trailRingHead + 1) % TRAIL_WINDOW_TICKS;
  if (trailRingCount < TRAIL_WINDOW_TICKS) ++trailRingCount;
}

// Conservative trailing wheel speed: the minimum over the recent window,
// discounting filter delay, sample latency, slip and quantization.  This is
// what the command may never exceed while the wheel is faster than the field.
float trailingMinForwardRevS() {
  float minV = 1.0e9f;
  for (uint8_t i = 0; i < trailRingCount; ++i) {
    if (trailRing[i] < minV) minV = trailRing[i];
  }
  return (minV == 1.0e9f) ? 0.0f : minV;
}

bool tryReserveTarget(uint32_t nowMs) {
  if (!encoderMotionReady()) return false;
  if (stepper && stepper->isRunning()) return false;  // stale queue must die first
  // FORWARD speed in the latched spin direction: a wheel moving the other way
  // must never reserve for the stale direction (the caller handles reversal).
  float speed = omega * (float)spinDir;
  if (speed < 0.02f) return false;

  float width = reachWindowWidthDeg(speed, spinDir);
  bool inWindow = speed <= ENGAGE_MAX_REV_S;
  // Urgency means an OPEN window is closing; a window that never opened
  // (width <= 0, weak spin) is handled by the fallback passes instead.
  bool urgent = width > 0.0f && width <= ENGAGE_URGENCY_WINDOW_DEG;
  if (!inWindow && !urgent) return false;

  // Friction-model bootstrap: with too few fits in this direction, let the
  // free coast feed the online fit before engaging - bounded by both window
  // width (safety) and time, and never past the urgency trigger.
  uint16_t fits = (spinDir > 0) ? cwFitCount : ccwFitCount;
  if (fits < FIT_PERSIST_MIN_FITS && !urgent &&
      width > CAL_DEFER_MIN_WIDTH_DEG &&
      spin.releaseMs != 0 && nowMs - spin.releaseMs < CAL_DEFER_MAX_MS) {
    return false;
  }

  // Fold this coast's samples into the model BEFORE targeting, so the
  // reachability decision uses the freshest friction estimate.
  finishFrictionCapture("reserve");

  TargetChoice choice = chooseSafeTarget(spinDir, wheelAngleDeg(), speed);
  if (!choice.found) return false;
  // Too short to launch cleanly: reserving would only flutter through
  // precharge/abandon cycles; the honest close path owns this spin.
  if (choice.runwayDeg < MIN_RESERVE_RUNWAY_DEG) return false;

  // Invariant: a dare can never be reserved.
  if (isDare(choice.wedge)) {
    enterFault(FC_TARGET_INVARIANT, "chose dare wedge");
    return false;
  }

  takeoverDir = spinDir;
  reserveCounts = encoderCountsMT;
  targetCountsMT = encoderCountsMT + takeoverDir * countsForDegrees(choice.runwayDeg);
  planDecelCapSps2 = choice.decelCapSps2;
  reserveMs = nowMs;
  reserveRetried = false;

  spin.targetWedge = choice.wedge;
  spin.targetAngleDeg = choice.targetAngleDeg;
  spin.runwayDeg = choice.runwayDeg;
  spin.naturalStopDeg = naturalStopDistanceDeg(speed, spinDir);
  spin.fricC = (spinDir > 0) ? cw_c : ccw_c;
  spin.fricB = (spinDir > 0) ? cw_b : ccw_b;
  spin.targetQuality = choice.quality;

  guestOverrideAboveMs = 0;   // never inherit a stale debounce timestamp
  setCurrentStage(CS_PRECHARGE);
  state = ST_TARGET_RESERVED;
  stateEnteredMs = nowMs;
  Serial.printf("SPIN#%lu RESERVE targetW=%d targetAngle=%.1f runway=%.1f natStop=%.1f "
                "speed=%.3f q=%u urgent=%d\n",
                (unsigned long)spin.number, choice.wedge, choice.targetAngleDeg,
                choice.runwayDeg, spin.naturalStopDeg, speed, choice.quality,
                urgent && !inWindow);
  return true;
}

bool launchCapture(uint32_t nowMs) {
  int fasSign = fasSignForEncoderDirection(takeoverDir);
  if (fasSign == 0) {
    enterFault(FC_DIR_CAL_INVALID, "fasSign=0 at capture");
    return false;
  }
  if (stepper && stepper->isRunning()) {
    // Guarded upstream; reaching here means a stale pulse train survived.
    enterFault(FC_STEPPER_API, "queue active at capture");
    return false;
  }
  float forward = omega * (float)takeoverDir;
  if (forward < 0.02f) return false;  // caller decides what to do

  // Entry command: below a trailing-minimum estimate so the motor picks the
  // wheel up strictly from behind.  Never above the capture cap.
  float trailSeed = fminf(forward, fabsf(omega));
  float cmd0 = fminf(TRAIL_FRACTION * trailSeed, CAPTURE_MAX_CMD_REV_S);
  uint32_t hz = (uint32_t)floorf(cmd0 * WHEEL_USTEPS_PER_REV);
  if (hz < 40) return false;

  float remainingDeg = remainingTargetDeg();
  if (remainingDeg < 7.0f) return false;

  // Plan the command-profile deceleration to consume exactly the runway.
  float remRev = remainingDeg / 360.0f;
  float aPlan = (cmd0 * cmd0) / (2.0f * remRev);
  float capRevS2 = (float)planDecelCapSps2 / WHEEL_USTEPS_PER_REV;
  if (aPlan > capRevS2) aPlan = capRevS2;
  if (aPlan < 0.008f) aPlan = 0.008f;
  // Wedge-uniform targets must remain coupled-reachable after the precharge
  // advance; a plan now steeper than natural decel would slip.  Abandon so
  // the caller re-reserves from the fresh state.
  if (spin.targetQuality == 0 &&
      aPlan > naturalDecelRevS2(forward, takeoverDir)) {
    return false;
  }
  planDecelRevS2 = aPlan;
  // The FAS acceleration is the pulse generator's TRACKING rate, not the
  // profile: it must exceed the profile decel so the field can follow each
  // 25 ms command step and stepsToStop() stays a small fraction of the
  // remaining runway (equal rates would trip the stop gate immediately and
  // collapse the closed loop into one open-loop ramp).
  uint32_t aPlanSps2 = (uint32_t)lroundf(aPlan * WHEEL_USTEPS_PER_REV);
  if (aPlanSps2 < 50) aPlanSps2 = 50;
  // aPlanSps2 <= ASSIST cap (1100) by construction, so 3x always exceeds the
  // 2000 cap only from ~667 up; the cap can never fall below 1.8x the plan.
  uint32_t aFasSps2 = aPlanSps2 * FAS_TRACK_ACCEL_FACTOR;
  if (aFasSps2 > FAS_TRACK_ACCEL_MAX_SPS2) aFasSps2 = FAS_TRACK_ACCEL_MAX_SPS2;

  // Jump start: begin the pulse train at cmd0 instead of ramping from zero.
  // The seeded ramp-down (stepsToStop == jumpStep) must fit comfortably
  // inside the runway even with a legitimately high fitted friction model,
  // or the stop gate degenerates to a first-tick open-loop ramp that can
  // drag the wheel past its natural stop.  If it does not fit, lower the
  // entry speed until it does.
  uint32_t jumpStep = (uint32_t)lroundf(((float)hz * (float)hz) / (2.0f * (float)aFasSps2));
  float remainingSteps = remainingDeg / 360.0f * WHEEL_USTEPS_PER_REV;
  uint32_t maxJump = (uint32_t)(0.6f * remainingSteps);
  if (jumpStep > maxJump) {
    hz = (uint32_t)floorf(sqrtf(2.0f * (float)aFasSps2 * (float)maxJump));
    if (hz < 40) return false;
    cmd0 = (float)hz / WHEEL_USTEPS_PER_REV;
    jumpStep = maxJump;
  }

  if (!fasSetSpeedHz(hz)) return false;
  if (!fasSetAcceleration(aFasSps2)) return false;
  stepper->setJumpStart(jumpStep);
  stepper->setCurrentPosition(0);   // motor is at standstill (asserted above)
  // FastAccelStepper 1.2.7's backward (count-down) continuous path executes
  // the live speed-change stream badly on this platform: captured traces
  // show the pulse rate wandering at 50-60% of command with slow
  // oscillations under runBackward, while runForward tracks within
  // 1-3 milli-rev/s.  Both physical directions therefore run FORWARD, with
  // rotation selected by DIR-pin polarity (safe: motor is at standstill).
  stepper->setDirectionPin(PIN_DIR, fasSign > 0 ? INVERT_DIR : !INVERT_DIR);
  // Current stays at the precharge level for the first pulses (soft start);
  // ST_SPEED_MATCH_CAPTURE raises it to capture torque after
  // CAPTURE_CURRENT_DELAY_MS, once the field and rotor are synchronized.
  if (!fasRun(1)) return false;

  cmdRevS = cmd0;
  lastAppliedHz = hz;
  lastResyncMs = nowMs;
  trailRingReset(forward);
  lowestForwardRevS = forward;
  speedupSinceMs = 0;
  lastSpeedupTrimMs = 0;
  fightSinceMs = 0;
  oppositeSinceMs = 0;
  velocityLossSinceMs = 0;
  guestOverrideAboveMs = 0;
  stopRequested = false;
  captureStartMs = nowMs;
  controlStartMs = nowMs;
  lastCmdTickMs = nowMs;
  prevTickWheelRevS = forward;
  prevTickMs = nowMs;

  spin.takeoverRevS = forward;
  spin.cmdMaxRevS = cmd0;
  spin.cmdMinRevS = cmd0;

  state = ST_SPEED_MATCH_CAPTURE;
  stateEnteredMs = nowMs;
  Serial.printf("SPIN#%lu CAPTURE fasDir=%+d wheel=%.3f cmd0=%.3f hz=%lu aPlan=%.4f(%lu sps2) aFas=%lu remain=%.1f\n",
                (unsigned long)spin.number, fasSign, forward, cmd0,
                (unsigned long)hz, aPlan, (unsigned long)aPlanSps2,
                (unsigned long)aFasSps2, remainingDeg);
  return true;
}

// Shared safety monitors for the powered states.  Returns false if the state
// was changed (fault or release) and the caller must stop processing.
bool controlSafetyChecks(uint32_t nowMs) {
  // 1. Encoder position freshness is non-negotiable while powered.
  if (!encoderPositionFresh()) {
    enterFault(FC_ENCODER_STALE, "position stale in control");
    return false;
  }
  // 2. Velocity may re-prime for a few tens of ms (normal); longer is a loss.
  if (!encoderVelocityValid) {
    if (velocityLossSinceMs == 0) velocityLossSinceMs = nowMs;
    else if (nowMs - velocityLossSinceMs >= VELOCITY_LOSS_FAULT_MS) {
      enterFault(FC_ENCODER_VELOCITY, "velocity unusable in control");
      return false;
    }
    return true;  // keep last command; monitors resume on next valid sample
  }
  velocityLossSinceMs = 0;

  float forward = omega * (float)takeoverDir;

  // 3. Guest override: the wheel was grabbed and re-spun mid-control.
  bool overrideMoving = fabsf(omega) > fmaxf(GUEST_OVERRIDE_REV_S, cmdRevS + 0.25f);
  if (overrideMoving) {
    if (guestOverrideAboveMs == 0) guestOverrideAboveMs = nowMs;
    if (nowMs - guestOverrideAboveMs >= GUEST_OVERRIDE_MS) {
      Serial.printf("SPIN#%lu GUEST-RESPIN detected mid-control\n",
                    (unsigned long)spin.number);
      driverFreewheel();   // floats coils, then clears the live pulse queue
      closeSpin(RES_GUEST_RESPUN);
      startSpinEvent((omega >= 0.0f) ? 1 : -1, nowMs);
      state = ST_SPIN_PUSH;
      stateEnteredMs = nowMs;
      return false;
    }
  } else {
    guestOverrideAboveMs = 0;
  }

  // 4. Sustained reversal: never fight it, never correct it.
  if (forward < -OPPOSITE_ABORT_REV_S) {
    if (oppositeSinceMs == 0) oppositeSinceMs = nowMs;
    else if (nowMs - oppositeSinceMs >= OPPOSITE_ABORT_MS) {
      enterFault(FC_UNEXPECTED_REVERSAL, "sustained opposite motion");
      return false;
    }
  } else {
    oppositeSinceMs = 0;
  }

  // 5. Wheel speed-up detector: under power the wheel must never accelerate.
  if (forward > 0.0f) {
    if (forward < lowestForwardRevS) {
      lowestForwardRevS = forward;
      speedupSinceMs = 0;
    } else if (forward > lowestForwardRevS + SPEEDUP_NOISE_REV_S) {
      if (speedupSinceMs == 0) speedupSinceMs = nowMs;
      float rise = forward - lowestForwardRevS;
      if (rise > spin.maxSpeedRiseRevS) spin.maxSpeedRiseRevS = rise;
      if (nowMs - speedupSinceMs >= SPEEDUP_TRIM_MS &&
          nowMs - lastSpeedupTrimMs >= SPEEDUP_TRIM_MS) {
        // Immediate torque/speed reduction; monotonic by construction.
        lastSpeedupTrimMs = nowMs;
        float trimmed = cmdRevS * 0.95f;
        if (trimmed < cmdRevS) {
          cmdRevS = trimmed;
          uint32_t hz = (uint32_t)floorf(cmdRevS * WHEEL_USTEPS_PER_REV);
          if (hz >= 40 && stepper && !stepper->isStopping()) {
            if (!fasSetSpeedHz(hz)) return false;
            stepper->applySpeedAcceleration();
            lastAppliedHz = hz;
          }
          if (cmdRevS < spin.cmdMinRevS) spin.cmdMinRevS = cmdRevS;
        }
      }
      if (nowMs - speedupSinceMs >= SPEEDUP_FAULT_MS) {
        enterFault(FC_SUSTAINED_SPEEDUP, "wheel accelerating under power");
        return false;
      }
    } else {
      speedupSinceMs = 0;
    }
  }

  // 6. Fight watchdog: the wheel far below the *actual* field speed means
  // step loss, belt jump, or a DIR fault.  (At very low command speeds a
  // natural early stall is handled by the profile, not treated as a fight.)
  float fasActual = fasWheelRevS();
  if (nowMs - captureStartMs >= FIGHT_GRACE_MS &&
      fasActual > FIGHT_MIN_CMD_REV_S &&
      forward < fasActual * FIGHT_SPEED_FRACTION) {
    if (fightSinceMs == 0) fightSinceMs = nowMs;
    else if (nowMs - fightSinceMs >= FIGHT_CONFIRM_MS) {
      enterFault(FC_MOTOR_FIGHT, "wheel far below field speed");
      return false;
    }
  } else {
    fightSinceMs = 0;
  }

  // 7. Physical plausibility of the remaining distance.
  float remaining = remainingTargetDeg();
  if (remaining > spin.runwayDeg + 30.0f || remaining < -180.0f) {
    enterFault(FC_POSITION_IMPOSSIBLE, "remaining distance impossible");
    return false;
  }

  // 8. Hard time budget for the whole controlled phase.
  if (nowMs - controlStartMs >= TAKEOVER_TIMEOUT_MS) {
    enterFault(FC_TAKEOVER_TIMEOUT, "controlled phase too long");
    return false;
  }
  return true;
}

// The braking command tick: at most once per CMD_UPDATE_MS, and the ONLY
// place a new speed is applied to the running continuous move.
void serviceDecelTick(uint32_t nowMs) {
  if (nowMs - lastCmdTickMs < CMD_UPDATE_MS) return;
  uint32_t dtMs = nowMs - lastCmdTickMs;
  lastCmdTickMs = nowMs;

  float forward = encoderVelocityValid ? omega * (float)takeoverDir : cmdRevS;
  if (encoderVelocityValid) {
    trailRingPush(forward);
    // measured deceleration telemetry
    if (prevTickMs != 0 && nowMs > prevTickMs) {
      float d = (prevTickWheelRevS - forward) * 1000.0f / (float)(nowMs - prevTickMs);
      if (d > spin.maxDecelRevS2) spin.maxDecelRevS2 = d;
    }
    prevTickWheelRevS = forward;
    prevTickMs = nowMs;
  }

  if (stopRequested) return;  // FAS ignores speed updates while stopping

  float remaining = remainingTargetDeg();
  int32_t toleranceCounts = countsForDegrees(OVERSHOOT_TOL_DEG);

  // Overshoot: begin the taper at once.  (stopMove still queues its own
  // ramp-down distance - small at these speeds with the raised FAS tracking
  // accel - so this bounds the excursion, it cannot cancel it.)
  if (takeoverDir * (encoderCountsMT - targetCountsMT) > toleranceCounts) {
    stopRequested = true;
    setCurrentStage(CS_TAPER);
    stepper->stopMove();
    return;
  }

  // Encoder-gated stop: begin the natural taper when the hardware ramp-down
  // distance meets the remaining runway.
  float stopLeadDeg = (float)stepper->stepsToStop() * 360.0f / WHEEL_USTEPS_PER_REV
                    + STOP_GATE_EXTRA_DEG;
  if (remaining <= stopLeadDeg) {
    stopRequested = true;
    setCurrentStage(CS_TAPER);
    stepper->stopMove();
    return;
  }

  // Monotonic command computation.  The command follows the sqrt PROFILE that
  // consumes the remaining runway at planDecel; the wheel decays naturally
  // onto the field once (the entry sits at 0.88x the trailing wheel speed),
  // couples, and is then paced down in synchronization - silent load-angle
  // braking.  The command must NOT continuously track a fraction of the
  // wheel speed: doing so re-opens the slip gap every tick and turns the
  // whole takeover into an audible pole-slip ratchet with ~5x the planned
  // braking force (observed on hardware at both 450 and 300 mA).  The wheel
  // can never be pulled: targets are capped below the natural stop, so a
  // coupled wheel always pushes INTO the field, and the chase-down plus the
  // speed-up detector guard the remaining pull paths.
  float prevCmd = cmdRevS;
  float remRev = fmaxf(remaining, 0.0f) / 360.0f;
  float profile = sqrtf(2.0f * planDecelRevS2 * remRev);
  float newCmd = fminf(prevCmd, profile);

  float trailMin = trailingMinForwardRevS();  // telemetry / debug reference
  // Capture-ramp surge fix (bench 2026-08-06, spins #16/#19/#27): cmd0 is
  // computed at reservation and goes stale by naturalDecel x rampTime on
  // urgent high-speed takeovers; the ramp tail then shoves the decayed wheel
  // back up (rise 0.07-0.11 rev/s, user-visible). Zero coupling slack while
  // in SPEED_MATCH_CAPTURE so the field can never lead the wheel; braking
  // keeps the original 0.020 slack. Lowering-only - cannot ratchet.
  float couplingSlack = (state == ST_SPEED_MATCH_CAPTURE) ? 0.0f : COUPLING_SLACK_REV_S;
  if (encoderVelocityValid && forward < newCmd - couplingSlack) {
    // Wheel slower than the field: reduce toward the wheel so the motor can
    // never lead it.  (Also naturally sheds braking authority when the wheel
    // is dying early: the field settles to the wheel's own speed.)
    newCmd = fminf(newCmd, fmaxf(forward, 0.0f));
  }

  // Invariant: commanded motor speed never increases after capture.
  if (newCmd > prevCmd + 1.0e-4f) {
    enterFault(FC_MONOTONIC_VIOLATION, "command tried to increase");
    return;
  }
  if (newCmd > prevCmd) newCmd = prevCmd;

  uint32_t hz = (uint32_t)floorf(newCmd * WHEEL_USTEPS_PER_REV);
  if (hz < 40) {
    // Below FastAccelStepper's practical floor.  Taper out here; landing
    // verification still requires safe-interior position AND stillness.
    stopRequested = true;
    setCurrentStage(CS_TAPER);
    stepper->stopMove();
    cmdRevS = newCmd;
    if (cmdRevS < spin.cmdMinRevS) spin.cmdMinRevS = cmdRevS;
    return;
  }

  cmdRevS = newCmd;
  if (cmdRevS < spin.cmdMinRevS) spin.cmdMinRevS = cmdRevS;

  // Apply to the pulse generator only on meaningful change, and RESYNC the
  // field to the command periodically.  FastAccelStepper re-derives its ramp
  // position with log2 fixed-point rounding on every applySpeedAcceleration;
  // a dense stream of applies compounds that rounding and the actual field
  // speed can sag far below the command (observed ~45% low on hardware,
  // over-braking the wheel into a long crawl).  Re-asserting the target
  // recovers the sag; the field approaching the command from BELOW remains
  // under 0.88x the trailing wheel speed, so it can never lead the wheel.
  uint32_t deltaHz = (hz > lastAppliedHz) ? hz - lastAppliedHz : lastAppliedHz - hz;
  bool applyNow = (float)deltaHz >= fmaxf(4.0f, 0.01f * (float)lastAppliedHz);
  if (state == ST_SPEED_MATCH_CAPTURE && hz < lastAppliedHz) applyNow = true;  // capture: track the wheel down immediately
  if (nowMs - lastResyncMs >= CMD_RESYNC_MS) {
    lastResyncMs = nowMs;
    float fasNow = fasWheelRevS();
    if (fabsf(fasNow - cmdRevS) > CMD_RESYNC_TOLERANCE * fmaxf(cmdRevS, 0.02f)) {
      applyNow = true;
    }
  }
  if (applyNow && stepper && !stepper->isStopping()) {
    if (!fasSetSpeedHz(hz)) return;
    stepper->applySpeedAcceleration();   // required for a live speed change
    lastAppliedHz = hz;
  }
  (void)dtMs;

  static uint32_t lastLogMs = 0;
  if (debugLog && !diagnosticCapture && nowMs - lastLogMs > 200) {
    lastLogMs = nowMs;
    Serial.printf("# TK remain=%.1f cmd=%.3f wheel=%.3f fas=%.3f trail=%.3f profile=%.3f stage=%u\n",
                  remaining, cmdRevS, forward, fasWheelRevS(),
                  trailMin, profile, (unsigned)currentStage);
  }
}

/* ========================================================================== */
/*                        LANDING VERDICT                                     */
/* ========================================================================== */
// Angular distance from an angle to the nearest dare-wedge span.
float dareDistanceDeg(float angle) {
  float best = 360.0f;
  for (int w = 0; w < NUM_WEDGES; ++w) {
    if (!isDare(w)) continue;
    float lo = w * WEDGE_DEG, hi = (w + 1) * WEDGE_DEG;
    float d;
    if (angle >= lo && angle <= hi) d = 0.0f;
    else {
      float dLo = fabsf(angle - lo), dHi = fabsf(angle - hi);
      dLo = fminf(dLo, 360.0f - dLo);
      dHi = fminf(dHi, 360.0f - dHi);
      d = fminf(dLo, dHi);
    }
    if (d < best) best = d;
  }
  return best;
}

void landingVerdict() {
  float angle = wheelAngleDeg();
  int wedge = currentWedge();
  float within = fmodf(angle, WEDGE_DEG);
  float edgeDist = fminf(within, WEDGE_DEG - within);

  if (isDare(wedge)) {
    // A controlled attempt settled on a dare: hardware/model failure.  Be
    // honest, latch, and lock automatic control until inspected.
    enterFault(FC_LANDING_UNSAFE, "settled on dare after control");
    return;
  }
  if (dareDistanceDeg(angle) < DARE_PROXIMITY_FAULT_DEG) {
    // Within measurement error of a dare boundary: the interior-margin
    // invariant is violated where it matters.  Honest latched failure.
    enterFault(FC_LANDING_UNSAFE, "settled at a dare boundary");
    return;
  }
  SpinResult result;
  if (edgeDist < LANDING_INTERIOR_MIN_DEG) {
    result = RES_EDGE_SAFE;               // safe, but honestly sub-interior
  } else if (wedge == spin.targetWedge) {
    result = RES_CONTROLLED_SAFE;
  } else {
    result = RES_OFF_TARGET_SAFE;
  }
  closeSpin(result);
  setCurrentStage(CS_HOLD1);
  state = ST_SOFT_HOLD;
  stateEnteredMs = millis();
}

/* ========================================================================== */
/*                          DIAGNOSTIC DUMP                                   */
/* ========================================================================== */
void dumpDiagnostics() {
  if (diagnosticCount == 0) {
    Serial.println(F("# DIAG: no samples captured."));
    return;
  }
  Serial.println(F("# DIAG columns: done_us,dt_good_us,raw,delta,counts,i2c_us,"
                   "omega_mrev,window_mrev,cmd_mrev,fas_mrev,remain_ddeg,"
                   "flags_hex,state,stage,current_ma"));
  uint16_t first = (diagnosticHead + DIAG_CAPACITY - diagnosticCount) % DIAG_CAPACITY;
  for (uint16_t i = 0; i < diagnosticCount; ++i) {
    const DiagnosticSample& s = diagnosticBuffer[(first + i) % DIAG_CAPACITY];
    Serial.printf("D,%lu,%u,%u,%d,%ld,%u,%d,%d,%d,%d,%d,%02X,%u,%u,%u\n",
                  (unsigned long)s.doneUs, s.dtGoodUs, s.raw, s.delta,
                  (long)s.counts, s.i2cUs, s.omegaMilliRevS, s.windowMilliRevS,
                  s.cmdMilliRevS, s.fasMilliRevS, s.remainDeciDeg,
                  s.flags, s.state, s.stage, (unsigned)s.curMa10 * 10);
  }
  Serial.printf("# DIAG n=%u wrapped=%d\n", diagnosticCount, diagnosticWrapped);
}

void startDiagnosticCapture() {
  diagnosticHead = 0;
  diagnosticCount = 0;
  diagnosticWrapped = false;
  diagnosticCapture = true;
  diagnosticSawMotion = false;
  diagnosticStillSinceUs = 0;
  Serial.println(F("# DIAG armed: RAM-only 1 kHz capture; dumps after true stop or fault."));
}

void serviceDiagnosticCapture() {
  if (!diagnosticCapture || !diagnosticSawMotion) return;
  bool stoppedState = (state == ST_IDLE_STOPPED || state == ST_SOFT_HOLD ||
                       state == ST_FAULT_LATCHED);
  bool safelyStopped = stoppedState &&
                       (!stepper || !stepper->isRunning()) &&
                       encoderPositionFresh() && fabsf(omega) <= STILL_REV_S;
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

/* ========================================================================== */
/*                             SERIAL UI                                      */
/* ========================================================================== */
void help() {
  Serial.println(F(
    "\n=== PRIZE WHEEL (correctness redesign) ===\n"
    " z  set current raw as wedge-0 anchor (wheel at rest, pointer on 11|0 line)\n"
    " p  attended two-leg direction probe (safe wedge center only)\n"
    " s  status\n"
    " d  arm high-rate RAM capture (dumps after true stop/fault)\n"
    " v  toggle live control logs\n"
    " e  toggle automatic takeover\n"
    " f  print friction model\n"
    " F  reset friction model to seeds\n"
    " x  clear stored direction calibration\n"
    " r  fault reset (re-checks TMC UART)\n"
    " m  print dare mask\n"
    " ?  help"));
  pwPartyHelpLines();
}

void printStatus() {
  uint32_t ageUs = encoderPrimed ? (uint32_t)(micros() - lastGoodUs) : 0;
  Serial.printf("# state=%s fault=%s angle=%.2f wedge=%d omega=%.4f natStop=%.1f "
                "pos=%s vel=%s age_us=%lu dirCal=%d(sign %+d) tmc=%d takeover=%d "
                "rawZero=%u stage=%u cmd=%.3f\n",
                stateName(state), faultName(faultCode), wheelAngleDeg(),
                currentWedge(), omega,
                encoderVelocityValid
                    ? naturalStopDistanceDeg(fabsf(omega), omega >= 0 ? 1 : -1)
                    : 0.0f,
                encoderPositionFresh() ? "FRESH" : "STALE",
                encoderVelocityValid ? "VALID" : "REPRIME",
                (unsigned long)ageUs, motorDirectionCalibrated ? 1 : 0,
                motorPositiveEncoderSign, tmcOk ? 1 : 0, takeoverEnabled ? 1 : 0,
                rawZero, (unsigned)currentStage, cmdRevS);
}

// WIFI_TASK: one shared single-char parser for the serial console AND the
// telnet clients.  Existing guards (z only while IDLE, etc.) apply to both.
void handleSerial() {
  if (!Serial.available()) return;
  char command = (char)Serial.read();
  if (pwPartyCommandChar(command)) return;   // party commands: t/a/l/w/V<n>
  handleCommandChar(command);
}

void handleCommandChar(char command) {
  switch (command) {
    case 'z':
      if (!encoderPrimed) Serial.println(F("# encoder not primed; z ignored"));
      else if (state != ST_IDLE_STOPPED && state != ST_FAULT_LATCHED)
        Serial.println(F("# z ignored: wheel controller busy"));
      else if (encoderVelocityValid && fabsf(omega) > STILL_REV_S)
        Serial.println(F("# z ignored: wheel must be at rest"));
      else {
        rawZero = lastGoodRaw;
        preferences.putUShort("rawZero", rawZero);
        primeEncoder(lastGoodRaw, micros(), false);
        Serial.printf("# raw-zero anchor set: raw=%u persisted (angle %.2f wedge %d)\n",
                      rawZero, wheelAngleDeg(), currentWedge());
      }
      break;
    case 'p': startDirectionProbe(); break;
    case 's':
      if (diagnosticCapture) Serial.println(F("# DIAG active: status suppressed"));
      else printStatus();
      break;
    case 'd':
      if (!diagnosticCapture) startDiagnosticCapture();
      else Serial.println(F("# DIAG already armed"));
      break;
    case 'v':
      if (diagnosticCapture) Serial.println(F("# live logging suppressed while DIAG armed"));
      else {
        debugLog = !debugLog;
        Serial.printf("# live control logging=%d\n", debugLog ? 1 : 0);
      }
      break;
    case 'e':
      takeoverEnabled = !takeoverEnabled;
      Serial.printf("# takeoverEnabled=%d\n", takeoverEnabled ? 1 : 0);
      break;
    case 'f':
      Serial.printf("# friction cw: c=%.4f b=%.4f fits=%u | ccw: c=%.4f b=%.4f fits=%u\n",
                    cw_c, cw_b, cwFitCount, ccw_c, ccw_b, ccwFitCount);
      break;
    case 'F':
      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {
        cw_c = 0.30f; cw_b = 0.15f; ccw_c = 0.30f; ccw_b = 0.15f;
        cwFitCount = 0; ccwFitCount = 0;
        preferences.remove("cwC"); preferences.remove("cwB"); preferences.remove("cwN");
        preferences.remove("ccwC"); preferences.remove("ccwB"); preferences.remove("ccwN");
        Serial.println(F("# friction model reset to seeds"));
      } else Serial.println(F("# F ignored: controller busy"));
      break;
    case 'x':
      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {
        motorDirectionCalibrated = false;
        motorPositiveEncoderSign = 0;
        preferences.putBool("dir_ok", false);
        preferences.putInt("pos_sign", 0);
        Serial.println(F("# direction calibration cleared; takeover locked"));
      } else Serial.println(F("# x ignored: controller busy"));
      break;
    case 'r':
      if (state == ST_FAULT_LATCHED) {
        if (stepper && stepper->isRunning()) {
          Serial.println(F("# r ignored: motor still ramping down"));
          break;
        }
        if (spinOpen) {
          if (encoderMotionReady() && fabsf(omega) > STILL_REV_S) {
            Serial.println(F("# r ignored: wait for the wheel to stop (open spin record)"));
            break;
          }
          closeSpin(spinOpenedDuringFault ? RES_CONTROL_LOCKED : RES_FAULTED);
        }
        driverFreewheel();
        if (!checkTmcUartRaw()) {
          // Keep the ORIGINAL fault code in the latch; the UART result is
          // already printed by the check itself.
          Serial.println(F("# r: TMC UART still failing; fault remains latched"));
          break;
        }
        // S2: a driver that power-cycled answers the UART but runs default
        // registers; re-apply the boot config and prove it stuck by readback.
        if (!pwS2ReconfigVerify()) {
          Serial.println(F("# r: TMC config re-apply failed; fault remains latched"));
          break;
        }
        faultCode = FC_NONE;
        PW_S1_CLEAR();  // S1: the r command is the only routine latch clear
        state = ST_IDLE_STOPPED;
        stateEnteredMs = millis();
        Serial.printf("# fault cleared; tmc=%d dirCal=%d%s\n",
                      tmcOk ? 1 : 0, motorDirectionCalibrated ? 1 : 0,
                      motorDirectionCalibrated ? "" : " (run p before guest use)");
      } else Serial.println(F("# r: no latched fault"));
      break;
    case 'm':
      Serial.printf("# dare_mask=0x%03X; dare wedges: 1 5\n", dare_mask);
      break;
    case '?': help(); break;
    default: break;
  }
}

/* ========================================================================== */
/*                          SETUP / MAIN LOOP                                 */
/* ========================================================================== */
void setup() {
  Serial.begin(115200);
  delay(300);
  randomSeed(esp_random());

  preferences.begin("prizewheel", false);
  rawZero = preferences.getUShort("rawZero", rawZero);  // label-true anchor
  motorPositiveEncoderSign = preferences.getInt("pos_sign", 0);
  motorDirectionCalibrated = preferences.getBool("dir_ok", false) &&
      (motorPositiveEncoderSign == 1 || motorPositiveEncoderSign == -1);
  cw_c = preferences.getFloat("cwC", 0.30f);
  cw_b = preferences.getFloat("cwB", 0.15f);
  ccw_c = preferences.getFloat("ccwC", 0.30f);
  ccw_b = preferences.getFloat("ccwB", 0.15f);
  cwFitCount = preferences.getUShort("cwN", 0);
  ccwFitCount = preferences.getUShort("ccwN", 0);
  // Persisted values pass the same bounds as fresh fits: one corrupted NVS
  // float (or b<=0 -> NaN travel) must never poison target selection.
  if (!(cw_c >= FIT_C_MIN && cw_c <= FIT_C_MAX) ||
      !(cw_b >= FIT_B_MIN && cw_b <= FIT_B_MAX)) {
    cw_c = 0.30f; cw_b = 0.15f; cwFitCount = 0;
    Serial.println(F("# NVS cw friction out of bounds; reset to seeds"));
  }
  if (!(ccw_c >= FIT_C_MIN && ccw_c <= FIT_C_MAX) ||
      !(ccw_b >= FIT_B_MIN && ccw_b <= FIT_B_MAX)) {
    ccw_c = 0.30f; ccw_b = 0.15f; ccwFitCount = 0;
    Serial.println(F("# NVS ccw friction out of bounds; reset to seeds"));
  }

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(3);  // bounded transaction; all failures are recorded

  if (readRawSample()) {
    primeEncoder(encoderRead.raw, encoderRead.doneUs, false);
    samplerScheduled = true;
    nextSampleDueUs = encoderRead.doneUs + ENCODER_SAMPLE_PERIOD_US;
    Serial.printf("# AS5600 primed: raw=%u i2c=%uus angle=%.2f wedge=%d\n",
                  encoderRead.raw, encoderRead.i2cUs, wheelAngleDeg(), currentWedge());
  } else {
    Serial.printf("# AS5600 initial read failed tx=%u req=%u avail=%u; retrying in loop\n",
                  encoderRead.txStatus, encoderRead.requested, encoderRead.available);
  }

  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, LOW);  // hold at boot; freewheel selected below

  TMC_SERIAL.begin(115200, SERIAL_8N1, TMC_RX_PIN, TMC_TX_PIN);
  driverConfig();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  if (stepper) {
    stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
    stepper->setEnablePin(PIN_EN, true);
    stepper->setAutoEnable(false);
  } else {
    Serial.println(F("# FATAL: stepperConnectToPin failed; control locked"));
  }

  checkTmcUartOrFault();
  Serial.printf("# frame: label-true static rawZero=%u | dirCal=%s sign=%+d | tmc=%d\n",
                rawZero, motorDirectionCalibrated ? "VALID" : "REQUIRED (p)",
                motorPositiveEncoderSign, tmcOk ? 1 : 0);
  Serial.printf("# friction cw c=%.4f b=%.4f (%u fits) ccw c=%.4f b=%.4f (%u fits)\n",
                cw_c, cw_b, cwFitCount, ccw_c, ccw_b, ccwFitCount);
  help();

  driverFreewheel();
  // A boot-time TMC fault latched above must survive setup's tail.
  if (state != ST_FAULT_LATCHED) {
    state = ST_IDLE_STOPPED;
    stateEnteredMs = millis();
  }

  // Party additions: S1 fault-latch restore, DFPlayer, LED task, SoftAP.
  pwPartyBegin();
}

// Deliberate-spin arming shared by MOTION_CANDIDATE and MANUAL_ADJUSTMENT
// (promotion).  Purely kinematic: speed, travel, duration, direction.
bool spinConfirmLogic(uint32_t nowMs) {
  if (!encoderMotionReady()) {
    spinArmMs = 0;
    return false;
  }
  if (spinArmMs == 0) {
    if (fabsf(omega) >= SPIN_DETECT_REV_S) {
      spinArmMs = nowMs;
      spinArmStartCounts = encoderCountsMT;
      spinArmDir = (omega >= 0.0f) ? 1 : -1;
    }
    return false;
  }
  int32_t signedTravel = spinArmDir * (encoderCountsMT - spinArmStartCounts);
  if (signedTravel < -countsForDegrees(SPIN_CANCEL_BACKTRACK_DEG) ||
      nowMs - spinArmMs > SPIN_ARM_TIMEOUT_MS) {
    spinArmMs = 0;
    return false;
  }
  if (nowMs - spinArmMs >= SPIN_CONFIRM_MS &&
      signedTravel >= countsForDegrees(SPIN_CONFIRM_TRAVEL_DEG)) {
    spinArmMs = 0;
    return true;
  }
  return false;
}

// External-contact detector for the released coast: deceleration harder than
// any free coast means a hand is on the wheel.
bool contactDetected(uint32_t nowMs) {
  if (!encoderVelocityValid) return false;
  float speed = fabsf(omega);
  if (prevContactMs != 0 && nowMs > prevContactMs) {
    float decel = (prevContactRevS - speed) * 1000.0f / (float)(nowMs - prevContactMs);
    if (decel > CONTACT_DECEL_REV_S2) {
      if (contactSinceMs == 0) contactSinceMs = nowMs;
    } else {
      contactSinceMs = 0;
    }
  }
  prevContactRevS = speed;
  prevContactMs = nowMs;
  return contactSinceMs != 0 && nowMs - contactSinceMs >= CONTACT_CONFIRM_MS;
}

void loop() {
  uint32_t pwLoopStartUs = micros();   // loop-budget tracker (PARTY_TASK rule 4)
  updateEncoder();
  handleSerial();
  uint32_t nowMs = millis();

  // Encoder-outage watchdog for the unpowered motion states: every exit from
  // MOTION_CANDIDATE / MANUAL_ADJUSTMENT / SPIN_PUSH / SPIN_RELEASED needs a
  // working encoder, so a sustained I2C loss there would otherwise wedge the
  // machine forever with an open spin record.  (Powered states and the
  // settle latch their own faults; IDLE, DIR_PROBE and FAULT need no watch.)
  bool watchEncoder = (state == ST_MOTION_CANDIDATE ||
                       state == ST_MANUAL_ADJUSTMENT ||
                       state == ST_SPIN_PUSH || state == ST_SPIN_RELEASED);
  if (watchEncoder && !encoderPositionFresh()) {
    if (encoderOutageSinceMs == 0) encoderOutageSinceMs = nowMs;
    else if (nowMs - encoderOutageSinceMs >= ENCODER_OUTAGE_FAULT_MS) {
      encoderOutageSinceMs = 0;
      enterFault(FC_ENCODER_STALE, "encoder outage in motion state");
    }
  } else {
    encoderOutageSinceMs = 0;
  }

  switch (state) {
    case ST_IDLE_STOPPED: {
      if (encoderMotionReady() && fabsf(omega) >= MOTION_EPS_REV_S) {
        candidateStartCounts = encoderCountsMT;
        spinArmMs = 0;
        settleStillSinceMs = 0;
        state = ST_MOTION_CANDIDATE;
        stateEnteredMs = nowMs;
      }
      break;
    }

    case ST_MOTION_CANDIDATE: {
      // Deliberate spin?  Purely kinematic; a weak push is still a push.
      if (spinConfirmLogic(nowMs)) {
        startSpinEvent(spinArmDir, nowMs);
        state = ST_SPIN_PUSH;
        stateEnteredMs = nowMs;
        break;
      }
      // Slow, limited movement that has lasted a while: a hand repositioning
      // the stopped wheel.  Classification never consults target availability.
      if (nowMs - stateEnteredMs >= MANUAL_CLASSIFY_MS &&
          (!encoderVelocityValid || fabsf(omega) < SPIN_DETECT_REV_S)) {
        state = ST_MANUAL_ADJUSTMENT;
        stateEnteredMs = nowMs;
        break;
      }
      // Nudge that already ended.
      if (encoderMotionReady() && fabsf(omega) <= STILL_REV_S) {
        if (settleStillSinceMs == 0) settleStillSinceMs = nowMs;
        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          state = ST_IDLE_STOPPED;
          stateEnteredMs = nowMs;
        }
      } else {
        settleStillSinceMs = 0;
      }
      break;
    }

    case ST_MANUAL_ADJUSTMENT: {
      // Motor floating; no spin count, no target, no takeover.  A movement
      // that later becomes a genuine spin is promoted.
      if (spinConfirmLogic(nowMs)) {
        Serial.println(F("# MANUAL movement promoted to spin"));
        startSpinEvent(spinArmDir, nowMs);
        state = ST_SPIN_PUSH;
        stateEnteredMs = nowMs;
        break;
      }
      if (encoderMotionReady() && fabsf(omega) <= STILL_REV_S) {
        if (settleStillSinceMs == 0) settleStillSinceMs = nowMs;
        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          float travel = degreesForCounts(encoderCountsMT - candidateStartCounts);
          Serial.printf("MANUAL-ADJUST travel=%+.1fdeg durMs=%lu wedge=%d (no spin, no motor)\n",
                        travel, (unsigned long)(nowMs - stateEnteredMs), currentWedge());
          state = ST_IDLE_STOPPED;
          stateEnteredMs = nowMs;
        }
      } else {
        settleStillSinceMs = 0;
      }
      break;
    }

    case ST_SPIN_PUSH: {
      if (!encoderMotionReady()) break;
      float speed = fabsf(omega);
      if (speed > spin.peakRevS) {
        spin.peakRevS = speed;
        lastPeakMs = nowMs;
      }
      // Sustained opposite travel: the guest changed their mind mid-push.
      if (omega * (float)spinDir < -SPIN_DETECT_REV_S &&
          nowMs - lastPeakMs > 200) {
        Serial.printf("SPIN#%lu redirected; restarting detection\n",
                      (unsigned long)spin.number);
        closeSpin(RES_GUEST_STOPPED);
        candidateStartCounts = encoderCountsMT;
        spinArmMs = 0;
        state = ST_MOTION_CANDIDATE;
        stateEnteredMs = nowMs;
        break;
      }
      // Hand-release: age, decay below peak, and no new peak for a while.
      bool released = (nowMs - spin.pushStartMs >= RELEASE_MIN_AGE_MS) &&
                      (speed < RELEASE_PEAK_FRACTION * spin.peakRevS) &&
                      (nowMs - lastPeakMs >= RELEASE_DECAY_MS);
      if (released) {
        spin.releaseMs = nowMs;
        spin.releaseAngleDeg = wheelAngleDeg();
        spin.releaseRevS = speed;
        resetFrictionCapture(spinDir);
        state = ST_SPIN_RELEASED;
        stateEnteredMs = nowMs;
        Serial.printf("SPIN#%lu RELEASE speed=%.3f peak=%.3f angle=%.1f\n",
                      (unsigned long)spin.number, speed, spin.peakRevS,
                      spin.releaseAngleDeg);
        break;
      }
      // The wheel died under the guest's hand before any release.
      if (speed <= STILL_REV_S) {
        if (settleStillSinceMs == 0) settleStillSinceMs = nowMs;
        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          closeSpin(RES_GUEST_STOPPED);
          state = ST_IDLE_STOPPED;
          stateEnteredMs = nowMs;
        }
      } else {
        settleStillSinceMs = 0;
      }
      break;
    }

    case ST_SPIN_RELEASED: {
      if (!encoderMotionReady()) break;
      float speed = fabsf(omega);
      // The guest caught the wheel and spun it the OTHER way: that is a new
      // spin, not this one - close honestly and re-classify.  Without this,
      // reservation would keep evaluating the stale direction.
      if (omega * (float)spinDir < -SPIN_DETECT_REV_S) {
        if (releasedReverseSinceMs == 0) releasedReverseSinceMs = nowMs;
        else if (nowMs - releasedReverseSinceMs >= 150) {
          releasedReverseSinceMs = 0;
          finishFrictionCapture("reversed");
          closeSpin(RES_GUEST_STOPPED);
          candidateStartCounts = encoderCountsMT;
          spinArmMs = 0;
          settleStillSinceMs = 0;
          state = ST_MOTION_CANDIDATE;
          stateEnteredMs = nowMs;
          break;
        }
      } else {
        releasedReverseSinceMs = 0;
      }
      // Guest pushed again: back to the push phase (still the same spin).
      if (speed > spin.peakRevS * 1.02f && speed > SPIN_DETECT_REV_S) {
        spin.peakRevS = speed;
        lastPeakMs = nowMs;
        finishFrictionCapture("re-push");
        state = ST_SPIN_PUSH;
        stateEnteredMs = nowMs;
        break;
      }
      captureFrictionSample(nowMs);
      if (contactDetected(nowMs)) spin.hadContact = 1;  // sticky for the close

      if (controlAvailable()) {
        if (tryReserveTarget(nowMs)) break;
        if (state != ST_SPIN_RELEASED) break;  // reservation may have faulted
      }

      // Wheel reaching stillness without engagement: honest accounting only.
      if (speed <= STILL_REV_S) {
        if (settleStillSinceMs == 0) settleStillSinceMs = nowMs;
        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          finishFrictionCapture("coast-end");
          SpinResult res = !controlAvailable() ? RES_CONTROL_LOCKED
                          : (spin.hadContact ? RES_GUEST_STOPPED
                                             : RES_NO_REACHABLE_SAFE);
          closeSpin(res);
          state = ST_IDLE_STOPPED;
          stateEnteredMs = nowMs;
        }
      } else {
        settleStillSinceMs = 0;
      }
      break;
    }

    case ST_TARGET_RESERVED: {
      // 80 ms low-current phase settle.  No pulses yet.
      if (!encoderPositionFresh()) {
        enterFault(FC_ENCODER_STALE, "stale during precharge");
        break;
      }
      // A guest grabbing the wheel during the precharge supersedes the spin.
      if (encoderMotionReady() && fabsf(omega) > GUEST_OVERRIDE_REV_S) {
        if (guestOverrideAboveMs == 0) guestOverrideAboveMs = nowMs;
        if (nowMs - guestOverrideAboveMs >= GUEST_OVERRIDE_MS) {
          guestOverrideAboveMs = 0;
          driverFreewheel();
          closeSpin(RES_GUEST_RESPUN);
          startSpinEvent((omega >= 0.0f) ? 1 : -1, nowMs);
          state = ST_SPIN_PUSH;
          stateEnteredMs = nowMs;
          break;
        }
      } else {
        guestOverrideAboveMs = 0;
      }
      if (nowMs - stateEnteredMs < PRECHARGE_MS) break;
      if (!encoderMotionReady()) {
        // Velocity re-primes within tens of ms; a powered wait must not be
        // unbounded (this state has no other time budget yet).
        if (nowMs - stateEnteredMs >= PRECHARGE_MS + RESERVED_VEL_TIMEOUT_MS) {
          enterFault(FC_ENCODER_VELOCITY, "velocity lost during precharge");
        }
        break;
      }

      // The wheel advanced during precharge; re-validate the runway.
      float remaining = remainingTargetDeg();
      float speed = fabsf(omega);
      float minNow = speed * 0.05f * 360.0f +
          brakedStopDistanceDeg(speed, spinDir, motorExtraRadS2(ASSIST_DECEL_MAX_SPS2));
      if (remaining < minNow || speed < 0.02f) {
        bool replaced = false;
        if (!reserveRetried) {
          reserveRetried = true;
          TargetChoice choice = chooseSafeTarget(spinDir, wheelAngleDeg(), speed);
          if (choice.found && !isDare(choice.wedge)) {
            targetCountsMT = encoderCountsMT +
                             takeoverDir * countsForDegrees(choice.runwayDeg);
            planDecelCapSps2 = choice.decelCapSps2;
            spin.targetWedge = choice.wedge;
            spin.targetAngleDeg = choice.targetAngleDeg;
            spin.runwayDeg = choice.runwayDeg;
            spin.targetQuality = choice.quality;
            spin.naturalStopDeg = naturalStopDistanceDeg(speed, spinDir);
            spin.fricC = (spinDir > 0) ? cw_c : ccw_c;
            spin.fricB = (spinDir > 0) ? cw_b : ccw_b;
            replaced = true;
            Serial.printf("SPIN#%lu RESERVE-ADJUST targetW=%d runway=%.1f\n",
                          (unsigned long)spin.number, choice.wedge, choice.runwayDeg);
          }
        }
        if (!replaced) {
          // Never launch toward a target already inside the minimum braking
          // distance: release and let SPIN_RELEASED re-evaluate honestly.
          driverFreewheel();
          state = ST_SPIN_RELEASED;
          stateEnteredMs = nowMs;
          break;
        }
      }
      if (!launchCapture(nowMs)) {
        if (state == ST_TARGET_RESERVED) {
          // Nothing catastrophic - the wheel is simply too slow to capture.
          driverFreewheel();
          state = ST_SPIN_RELEASED;
          stateEnteredMs = nowMs;
        }
      }
      break;
    }

    case ST_SPEED_MATCH_CAPTURE: {
      if (!controlSafetyChecks(nowMs)) break;
      serviceDecelTick(nowMs);   // trailing window keeps filling; cmd is const
      if (state != ST_SPEED_MATCH_CAPTURE) break;  // tick may have faulted
      // Torque steps in only after the field has swept in sync for a while.
      if (currentStage == CS_PRECHARGE &&
          nowMs - captureStartMs >= CAPTURE_CURRENT_DELAY_MS) {
        setCurrentStage(CS_CAPTURE);
      }
      if (nowMs - captureStartMs >= PICKUP_COHERENCE_MS) {
        // Never step the ladder back up if the stop taper already began.
        if (!stopRequested) setCurrentStage(CS_BRAKE);
        state = ST_CONTROLLED_DECEL;
        stateEnteredMs = nowMs;
      }
      break;
    }

    case ST_CONTROLLED_DECEL: {
      if (!controlSafetyChecks(nowMs)) break;
      serviceDecelTick(nowMs);
      if (state != ST_CONTROLLED_DECEL) break;  // tick may have faulted
      if (stopRequested && stepper && !stepper->isRunning()) {
        settleStillSinceMs = 0;
        settleEntryCounts = encoderCountsMT;
        state = ST_LANDING_SETTLE;
        stateEnteredMs = nowMs;
      }
      break;
    }

    case ST_LANDING_SETTLE: {
      // Pulse train has tapered to zero; the wheel settles under taper
      // current.  Landing requires BOTH interior position AND stillness.
      if (!encoderPositionFresh()) {
        enterFault(FC_ENCODER_STALE, "stale during settle");
        break;
      }
      setCurrentStage(CS_TAPER);  // idempotent; undoes any late CS_BRAKE write
      if (encoderMotionReady() &&
          fabsf(omega) > fmaxf(GUEST_OVERRIDE_REV_S, 0.3f)) {
        // Grabbed and re-spun before the summary: hand it back to detection.
        driverFreewheel();
        closeSpin(RES_GUEST_RESPUN);
        startSpinEvent((omega >= 0.0f) ? 1 : -1, nowMs);
        state = ST_SPIN_PUSH;
        stateEnteredMs = nowMs;
        break;
      }
      // Residual momentum may legitimately creep the wheel several degrees
      // under the taper detent before it rests, so small pre-stillness travel
      // belongs to the landing verdict, not to guest blame.  Travel no
      // residual creep can plausibly produce (well over a wedge) means a hand
      // is dragging the wheel: release and close honestly.
      if (fabsf(degreesForCounts(encoderCountsMT - settleEntryCounts)) >
              LANDING_DRAG_ABORT_DEG &&
          encoderMotionReady() && fabsf(omega) > STILL_REV_S) {
        Serial.printf("SPIN#%lu SETTLE-DRAG: guest moved the wheel during settle\n",
                      (unsigned long)spin.number);
        driverFreewheel();
        closeSpin(RES_GUEST_STOPPED);
        candidateStartCounts = encoderCountsMT;
        spinArmMs = 0;
        settleStillSinceMs = 0;
        state = ST_MOTION_CANDIDATE;
        stateEnteredMs = nowMs;
        break;
      }
      if (encoderMotionReady() && fabsf(omega) <= STILL_REV_S) {
        if (settleStillSinceMs == 0) {
          settleStillSinceMs = nowMs;
          settleWindowCounts = encoderCountsMT;
        } else if (fabsf(degreesForCounts(encoderCountsMT - settleWindowCounts)) > 1.5f) {
          // Sub-threshold drift through the window (slow drag): restart it so
          // the verdict samples a genuinely settled position.
          settleStillSinceMs = nowMs;
          settleWindowCounts = encoderCountsMT;
        } else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          landingVerdict();
          // The verdict moved us to SOFT_HOLD (or FAULT).  Leave immediately:
          // the timeout below must never clobber a completed landing (this
          // exact clobber latched a spurious TAKEOVER_TIMEOUT on hardware).
          break;
        }
      } else {
        settleStillSinceMs = 0;
      }
      if (state == ST_LANDING_SETTLE &&
          nowMs - stateEnteredMs >= SETTLE_TIMEOUT_MS) {
        enterFault(FC_TAKEOVER_TIMEOUT, "settle never became still");
      }
      break;
    }

    case ST_SOFT_HOLD: {
      // Fade the hold torque so release is imperceptible, then float.
      uint32_t age = nowMs - stateEnteredMs;
      if (currentStage == CS_HOLD1 && age >= HOLD1_MS) setCurrentStage(CS_HOLD2);
      else if (currentStage == CS_HOLD2 && age >= HOLD1_MS + HOLD2_MS) {
        driverFreewheel();
        // Between-spins driver health check (wheel at rest, timing harmless).
        checkTmcUartOrFault();
        if (state == ST_FAULT_LATCHED) break;
        state = ST_IDLE_STOPPED;
        stateEnteredMs = nowMs;
        break;
      }
      // A new hand motion during the fade releases the wheel immediately.
      if (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S) {
        driverFreewheel();
        candidateStartCounts = encoderCountsMT;
        spinArmMs = 0;
        settleStillSinceMs = 0;   // never inherit the landing's stillness age
        state = ST_MOTION_CANDIDATE;
        stateEnteredMs = nowMs;
      }
      break;
    }

    case ST_DIR_PROBE:
      serviceDirectionProbe();
      break;

    case ST_FAULT_LATCHED:
      serviceFault();
      // Spins during a latched fault are observed honestly, never controlled.
      // One record per motion episode: a spin left open by the fault itself
      // (or newly detected here) closes only when the wheel actually rests,
      // so the recorded final wedge is real and the residual coast is never
      // re-counted as a second spin.
      if (!spinOpen) {
        if (spinConfirmLogic(nowMs)) {
          startSpinEvent(spinArmDir, nowMs);
          spinOpenedDuringFault = true;
          Serial.printf("SPIN#%lu FAULT ACTIVE (%s): spin will NOT be controlled\n",
                        (unsigned long)spin.number, faultName(faultCode));
          settleStillSinceMs = 0;
        }
      } else if (encoderMotionReady() && fabsf(omega) <= STILL_REV_S) {
        if (settleStillSinceMs == 0) settleStillSinceMs = nowMs;
        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          closeSpin(spinOpenedDuringFault ? RES_CONTROL_LOCKED : RES_FAULTED);
        }
      } else {
        settleStillSinceMs = 0;
        // A dead encoder cannot prove stillness; close the record after a
        // bounded wait rather than leaving it open forever.
        if (spinOpen && !encoderPositionFresh() &&
            nowMs - stateEnteredMs > 5000) {
          closeSpin(spinOpenedDuringFault ? RES_CONTROL_LOCKED : RES_FAULTED);
        }
      }
      break;
  }

  serviceDiagnosticCapture();

  // Party additions run LAST, after all control work, and measure themselves
  // against the <=2 ms FX+WiFi budget ('t' prints the max-tracker).
  pwPartyService(pwLoopStartUs);
}

// Implementations of the party additions; included last so they can observe
// every control global without any forward-declaration surgery above.
#include "pw_party_impl.h"
