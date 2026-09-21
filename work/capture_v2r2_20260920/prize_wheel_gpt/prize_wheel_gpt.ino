/* ============================================================================
 * prize_wheel_gpt.ino - Prize wheel firmware, correctness redesign
 *
 * Isolated pulse-first diagnostic, motion disabled by default. An explicitly
 * triggered finite motor spin releases to real freewheel, then the ordinary
 * release detector may attempt one capture. Full capture current is enabled
 * only after STEP activity is verified with EN high. Rotor phase is unmeasured.
 * No automatic boot movement, takeover, fault clear, or repeat.
 *
 * State machine:
 *   IDLE_STOPPED -> MOTION_CANDIDATE -> (MANUAL_ADJUSTMENT | SPIN_PUSH)
 *   SPIN_PUSH -> SPIN_RELEASED -> TARGET_RESERVED -> CAPTURE_ARMING
 *   -> SPEED_MATCH_CAPTURE
 *   -> CONTROLLED_DECEL -> LANDING_SETTLE -> SOFT_HOLD -> IDLE_STOPPED
 *   Any powered state -> FAULT_LATCHED on hardware/invariant failure.
 *
 * Hard rules enforced here:
 *   - Spin detection is purely kinematic; target availability never delays or
 *     reclassifies a spin.
 *   - After the explicit spin-up, command speed never increases after capture,
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
 * Hardware: ESP32-S3, BTT TMC5160T Pro (SPI), NEMA23 76mm dual-shaft, 1:1 direct drive (v2),
 *           AS5600 on the motor rear shaft (wheel inferred through 1:1 coupling).
 * Build:    ESP32 Arduino core 3.3.10, FastAccelStepper 1.2.7, TMCStepper.
 * ========================================================================== */

#include <Wire.h>
#include <Preferences.h>
#include <TMCStepper.h>
#include "pw_tmc5160_safe.h"
#include <FastAccelStepper.h>
#include "pw_step_clock.h"
#include "pw_brake_profile.h"
#include "pw_gentle_test.h"
#include "pw_speedup_watch.h"
#include "pw_capture_arm.h"
#include <esp_heap_caps.h>
// Party additions (WiFi + FX + sanctioned fixes S1/S2/S3): declarations,
// config switches and the serial mirror.  Implementations are included at the
// very bottom of this file.  See PARTY_TASK.md / DELIVERY.md.
#include "pw_party.h"

// Local, disabled-by-default self-spin diagnostic; implementation is last.
bool pwSelfspinEnableOutputs();
bool pwSelfspinBeforePositionReset();
bool pwSelfspinAllowControl();
bool pwSelfspinCommand(char command);
void pwSelfspinFaultDisable();
void pwSelfspinService();
void pwSelfspinBegin();
void pwSelfspinPrintStatus();
bool pwSelfspinBeginCaptureAttempt();
bool pwSelfspinCaptureDriverHealthy();
void pwSelfspinCaptureAbort(const char* reason);

/* ----------------------------- PINS -------------------------------------- */
#define TMC_CS_PIN   10
#define TMC_MOSI_PIN 11
#define TMC_SCK_PIN  12
#define TMC_MISO_PIN 13
#define R_SENSE      0.075f

#define PIN_EN   7
#define PIN_STEP 5
#define PIN_DIR  6

#define PIN_SDA  38
#define PIN_SCL  39
#define AS5600_ADDR 0x36
#define AS5600_RAW  0x0C

/* --------------------------- MECHANICAL ---------------------------------- */
#define MOTOR_FULLSTEPS 200
#define MICROSTEPS      16
#define GEAR_RATIO      1.0f
const float WHEEL_USTEPS_PER_REV = MOTOR_FULLSTEPS * MICROSTEPS * GEAR_RATIO;
#define NUM_WEDGES 12
const float WEDGE_DEG = 360.0f / NUM_WEDGES;

/* ------------------------- ENUMS (all hoisted) ---------------------------- */
enum State : uint8_t {
  ST_IDLE_STOPPED, ST_MOTION_CANDIDATE, ST_MANUAL_ADJUSTMENT,
  ST_SPIN_PUSH, ST_SPIN_RELEASED, ST_TARGET_RESERVED,
  ST_SPEED_MATCH_CAPTURE, ST_CONTROLLED_DECEL, ST_LANDING_SETTLE,
  ST_SOFT_HOLD, ST_DIR_PROBE, ST_FAULT_LATCHED, ST_SELFSPIN,
  ST_CAPTURE_ARMING // appended: STEP verified with EN high before capture
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
  FC_LANDING_UNSAFE,       // settled on a dare after a controlled attempt
  FC_TRACKING_LOST,        // continued travel after the commanded stop
  FC_SELFSPIN_ABORT,       // preserve existing persisted fault ID 15
  FC_UNKNOWN_PERSISTED    // unknown saved byte stays intact; never auto-clear
};

enum CurrentStage : uint8_t {
  CS_FREEWHEEL, CS_PRECHARGE, CS_CAPTURE, CS_BRAKE, CS_TAPER, CS_HOLD1, CS_HOLD2,
  CS_CAPTURE_PREPARED // 2200 mA registers prepared, but EN must remain HIGH
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
const float SAFE_EDGE_MARGIN_DEG = 4.5f;  // margin at safe|safe boundaries (scatter widened, owner req 2026-08-06; was 8.0 uniform)

const float DARE_EDGE_MARGIN_DEG = 8.0f;  // margin at dare-facing boundaries - the certified value, unchanged
const float LANDING_INTERIOR_MIN_DEG  = 5.0f;   // verification margin
const float DARE_PROXIMITY_FAULT_DEG  = 2.0f;   // settle this close to a dare
                                                // boundary = unsafe landing
// Pulse-first experiment: no static precharge or weak-current pulse dwell.
// STEP rate is checked while EN is high; rotor electrical phase is unknown.
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
const bool GENTLE_BRAKE_TEST = true; // attended evaluation; bypass guessed friction targeting
const float GENTLE_TEST_MIN_SECONDS = 6.0f;
const uint32_t DECEL_CEILING_SPS2     = 320;    // 0.10 rev/s^2 maximum
const uint32_t ASSIST_DECEL_MAX_SPS2  = 320;    // no aggressive fallback
const float COUPLING_SLACK_REV_S      = 0.020f;
const float FAS_MIN_CMD_REV_S         = 0.00625f; // 40 Hz taper floor
const float STOP_GATE_EXTRA_DEG       = 2.0f;
const float OVERSHOOT_TOL_DEG         = 4.0f;
// Above the phase-capture snap transient (~0.03-0.075 rev/s observed), below
// any deliberate pull; the fault still needs a sustained rise.
const float SPEEDUP_NOISE_REV_S       = 0.050f;
const uint16_t SPEEDUP_FAULT_MS       = 400;    // latch fault after this
const float FIGHT_SPEED_FRACTION      = 0.45f;
const uint16_t FIGHT_GRACE_MS         = 150;
const uint16_t FIGHT_CONFIRM_MS       = 150;
const float FIGHT_MIN_CMD_REV_S       = 0.060f; // below this, stall != fight
const float OPPOSITE_ABORT_REV_S      = 0.050f;
const uint16_t OPPOSITE_ABORT_MS      = 75;
const uint16_t VELOCITY_LOSS_FAULT_MS = 300;
const uint16_t ENCODER_OUTAGE_FAULT_MS = 1000;  // encoder loss in motion states
const uint32_t TAKEOVER_TIMEOUT_MS    = 30000;
// A slow final crawl legitimately restarts the stillness window several
// times; the timeout exists for a genuinely never-still wheel (hardware).
const uint32_t SETTLE_TIMEOUT_MS      = 20000;
// Pre-stillness settle travel beyond this cannot be residual creep under the
// taper detent (friction-only coast from a 0.1 rev/s handoff is ~31 deg
// unheld; the 300 mA detent cuts that well below a wedge): a hand is dragging.
const float LANDING_DRAG_ABORT_DEG    = 45.0f;
// The pulse generator uses the planned deceleration, including stopMove().
// No faster tracking acceleration is used during normal braking.

// --- landing / hold ---
const float STILL_REV_S               = 0.020f;
const uint16_t SETTLE_MS              = 500;
const uint16_t HOLD_HEALTH_MS         = 1000;
const uint16_t HOLD_RELEASE_CONFIRM_MS = 40;
const uint16_t DIAG_HOLD_RECORD_MS    = 5000;
uint32_t holdReleaseSinceMs = 0;
uint32_t holdLastHealthMs = 0;
int8_t holdReleaseDir = 0;

// --- current ladder (written ONLY on stage transitions) ---
// With the profile-PACED command law the motor brakes through the load angle
// of a synchronized rotor, and holding that synchronization is what needs
// current: at 300 mA the rotor hops poles under the required drag (rattle),
// at 600 mA it stays locked and silent (owner bench ladder; 650 hums, 180
// rattles).  Current sets coupling stiffness; the braking force itself is
// set by the commanded profile.  (600/450 only over-braked under the old
// continuously-trailing law, which forced multi-pole slip at any current.)
const uint16_t CUR_PRECHARGE_MA = 350;  // phase settle, no snap (v2: NEMA23 scale)
const uint16_t CUR_CAPTURE_MA   = 2200;
const uint16_t CUR_BRAKE_MA     = 2200; // retain capture torque through deceleration
const uint16_t CUR_TAPER_MA     = 1100;  // final taper / settle watch
const uint16_t CUR_HOLD1_MA     = 1650; // continuous hold after confirmed stillness
const uint16_t CUR_HOLD2_MA     = 1650; // legacy stage ID retained; no timed fade

// --- direction probe ---
const uint16_t DIR_PROBE_CURRENT_MA = 1600;  // v2: NEMA23 direct-drive needs far more than the old NEMA17 belt figure
const uint32_t DIR_PROBE_SPEED_HZ   = 100;
const uint32_t DIR_PROBE_ACCEL_SPS2 = 300;
const int32_t  DIR_PROBE_USTEPS     = 160;   // 9 deg at the wheel
const float    DIR_PROBE_MIN_DEG    = 2.0f;
const float    DIR_PROBE_RETURN_TOL_DEG = 3.0f;
const uint32_t DIR_PROBE_TIMEOUT_MS = 15000;  // temp: diagnosing slow settle vs stuck isRunning()

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
PwTmc5160 driver(TMC_CS_PIN, R_SENSE, TMC_MOSI_PIN, TMC_MISO_PIN, TMC_SCK_PIN);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper* stepper = nullptr;
Preferences preferences;

State state = ST_IDLE_STOPPED;
FaultCode faultCode = FC_NONE;
uint8_t persistedFaultRaw = 0; // preserve even an unknown nonzero NVS value
const char* PW_RECOVERY_GUARD_KEY = "recovGuard";
uint8_t recoveryGuardRaw = 0; // incomplete recovery must also lock the next boot
CurrentStage currentStage = CS_FREEWHEEL;
uint16_t g_currentMa = 0;   // actual commanded rms current, for telemetry
bool debugLog = false;
bool takeoverEnabled = false;
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

struct DiagnosticSample {      // PSRAM trace; controller timing is unchanged
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
  int32_t stepCount;          // FAS hardware PCNT position, not speed estimate
  uint32_t stepUs;
  uint32_t drvStatus;
  uint32_t tstep;
  uint16_t driverAgeUs;
  uint8_t gstat;
  uint8_t pins;               // bit 0 EN, bit 1 DIR
};

// Eight seconds at 1 kHz, allocated only in external RAM. No control data
// uses this buffer; allocation failure disables diagnostics, not the wheel.
const uint16_t DIAG_CAPACITY = 16384;
DiagnosticSample* diagnosticBuffer = nullptr;
bool diagnosticDumpActive = false;
uint16_t diagnosticDumpFirst = 0, diagnosticDumpIndex = 0;
uint8_t diagnosticDumpPhase = 0;
char diagnosticDumpLine[320];
size_t diagnosticDumpLength = 0, diagnosticDumpOffset = 0;
uint32_t diagnosticDriverUs = 0, diagnosticDrvStatus = 0, diagnosticTstep = 0;
uint8_t diagnosticGstat = 0;
uint8_t diagnosticDriverStage = 255;
uint16_t diagnosticHead = 0;
uint16_t diagnosticCount = 0;
bool diagnosticWrapped = false;
bool diagnosticCapture = false;
// Retain the lead-up to a fault instead of overwriting it during free coast.
bool diagnosticFrozen = false;
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
  uint8_t targetQuality;       // 0 uniform, 1 nearest, 2 edge, 3 shadow, 4 gentle test
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
PwSpeedupWatch speedupWatch;
uint32_t fightSinceMs = 0;
uint32_t oppositeSinceMs = 0;
uint32_t velocityLossSinceMs = 0;
bool stopRequested = false;
float prevTickWheelRevS = 0.0f;
uint32_t prevTickMs = 0;
PwCaptureArm captureArm;
uint32_t captureEntryHz = 0;
bool captureDirHigh = false;

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
  if (!diagnosticCapture || diagnosticFrozen || !diagnosticBuffer) return;
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
  s.stepUs = micros();
  s.stepCount = stepper ? stepper->getCurrentPosition() : 0;
  s.pins = (digitalRead(PIN_EN) ? 1 : 0) | (digitalRead(PIN_DIR) ? 2 : 0);
  if (diagnosticDriverStage != (uint8_t)currentStage ||
      (uint32_t)(s.stepUs - diagnosticDriverUs) >= 50000UL) {
    // Read-only SPI snapshots, at stage changes or 20 Hz. dt_good_us records
    // any timing cost on the next sample; no driver registers are written.
    diagnosticDrvStatus = driver.DRV_STATUS();
    diagnosticTstep = driver.TSTEP();
    diagnosticGstat = (uint8_t)driver.GSTAT();
    diagnosticDriverUs = micros();
    diagnosticDriverStage = (uint8_t)currentStage;
  }
  s.drvStatus = diagnosticDrvStatus;
  s.tstep = diagnosticTstep;
  s.gstat = diagnosticGstat;
  uint32_t driverAge = (uint32_t)(micros() - diagnosticDriverUs);
  s.driverAgeUs = driverAge > 65535U ? 65535U : (uint16_t)driverAge;
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
  // I_scale_analog: N/A on TMC5160 (TMC2209-only GCONF bit, inaccessible here)
  driver.toff(4);
  driver.blank_time(24);
  driver.microsteps(16);
  driver.en_pwm_mode(false);      // SpreadCycle = torque, no RPM cap (TMC5160: false=SpreadCycle)
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
      if (stepper) outputsOk = pwSelfspinEnableOutputs();
      break;
    case CS_CAPTURE:   driver.rms_current(CUR_CAPTURE_MA, 1.0); g_currentMa = CUR_CAPTURE_MA; break;
    case CS_CAPTURE_PREPARED:
      // Never briefly energize a stationary field while preparing full torque.
      if (stepper) outputsOk = stepper->disableOutputs();
      driver.freewheel(0);
      driver.rms_current(CUR_CAPTURE_MA, 1.0);
      g_currentMa = CUR_CAPTURE_MA; // requested current; pins separately prove EN-off
      break;
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
static inline void wedgeEdgeMargins(int w, float* loM, float* hiM) {
  int prev = (w + NUM_WEDGES - 1) % NUM_WEDGES;
  int next = (w + 1) % NUM_WEDGES;
  *loM = isDare(prev) ? DARE_EDGE_MARGIN_DEG : SAFE_EDGE_MARGIN_DEG;
  *hiM = isDare(next) ? DARE_EDGE_MARGIN_DEG : SAFE_EDGE_MARGIN_DEG;
}

TargetChoice chooseGentleTestTarget(int dir, float curAngle, uint32_t entryHz,
                                   float advanceDeg) {
  uint32_t forbidden = 0;
  for (int w = 0; w < NUM_WEDGES; ++w) if (isDare(w)) forbidden |= 1UL << w;
  PwGentleTarget t = pwGentleTarget(entryHz, curAngle, dir, forbidden,
      NUM_WEDGES, WHEEL_USTEPS_PER_REV, DECEL_CEILING_SPS2,
      GENTLE_TEST_MIN_SECONDS, advanceDeg);
  TargetChoice out;
  out.found = t.found;
  out.wedge = t.wedge;
  out.targetAngleDeg = t.angleDeg;
  out.runwayDeg = t.runwayDeg;
  out.decelCapSps2 = t.accelerationLimit;
  out.quality = 4; // attended six-second-minimum braking test
  return out;
}

TargetChoice chooseSafeTarget(int dir, float curAngle, float speedRevS) {
  if (GENTLE_BRAKE_TEST) {
    uint32_t entryHz = (uint32_t)floorf(fminf(TRAIL_FRACTION * speedRevS,
        CAPTURE_MAX_CMD_REV_S) * WHEEL_USTEPS_PER_REV);
    return chooseGentleTestTarget(dir, curAngle, entryHz,
        speedRevS * ENGAGE_LATENCY_S * 360.0f);
  }

  TargetChoice out;
  out.found = false;
  out.wedge = -1;
  out.runwayDeg = 0.0f;
  out.targetAngleDeg = 0.0f;
  out.decelCapSps2 = DECEL_CEILING_SPS2;
  out.quality = 0;

  // Per-edge margins: dare-facing edges keep the certified 8.0; safe|safe edges

  // relax to 4.5 so landings scatter visibly instead of clustering mid-wedge.
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
  float profileMinDeg = latencyDeg + pwBrakeDistanceDeg(
      fminf(cmd0est, CAPTURE_MAX_CMD_REV_S),
      (float)DECEL_CEILING_SPS2 / WHEEL_USTEPS_PER_REV) + STOP_GATE_EXTRA_DEG;
  if (profileMinDeg > winMin) winMin = profileMinDeg;
  float assistMin = latencyDeg
                  + brakedStopDistanceDeg(speedRevS, dir, motorExtraRadS2(ASSIST_DECEL_MAX_SPS2))
                  + 2.0f;
  assistMin = fmaxf(assistMin, latencyDeg + pwBrakeDistanceDeg(
      fminf(cmd0est, CAPTURE_MAX_CMD_REV_S),
      (float)ASSIST_DECEL_MAX_SPS2 / WHEEL_USTEPS_PER_REV) + STOP_GATE_EXTRA_DEG);

  // Pass 1: wedge-uniform among safe wedges reachable at the natural ceiling.
  int candWedge[NUM_WEDGES];
  float candLo[NUM_WEDGES], candHi[NUM_WEDGES];
  uint8_t candCount = 0;
  if (winMax > winMin + 2.0f) {
    for (int w = 0; w < NUM_WEDGES; ++w) {
      if (isDare(w)) continue;
      float loM, hiM; wedgeEdgeMargins(w, &loM, &hiM);
      float wSpan = WEDGE_DEG - loM - hiM;
      float interiorEntryAngle = (dir > 0)
          ? w * WEDGE_DEG + loM
          : (w + 1) * WEDGE_DEG - hiM;
      float dNear = forwardDistanceDeg(dir, curAngle, interiorEntryAngle);
      float bestLo = 0.0f, bestHi = -1.0f, bestScore = 1.0e9f;
      for (int lap = 0; lap < 4; ++lap) {
        float a = dNear + 360.0f * lap;
        if (a > winMax) break;
        float b = a + wSpan;
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
  if (!out.found && naturalDeg > 10.0f && naturalDeg - 2.0f >= assistMin) {
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
      float loM, hiM; wedgeEdgeMargins(w, &loM, &hiM);
      float wSpan = WEDGE_DEG - loM - hiM;
      float interiorEntryAngle = (dir > 0)
          ? w * WEDGE_DEG + loM
          : (w + 1) * WEDGE_DEG - hiM;
      float dNear = forwardDistanceDeg(dir, curAngle, interiorEntryAngle);
      for (int lap = 0; lap < 4; ++lap) {
        float a = dNear + 360.0f * lap;
        float b = a + wSpan;
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
  float profileMinDeg = latencyDeg + pwBrakeDistanceDeg(
      fminf(cmd0est, CAPTURE_MAX_CMD_REV_S),
      (float)DECEL_CEILING_SPS2 / WHEEL_USTEPS_PER_REV) + STOP_GATE_EXTRA_DEG;
  if (profileMinDeg > winMin) winMin = profileMinDeg;
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
    case ST_SELFSPIN: return "SELFSPIN";
    case ST_CAPTURE_ARMING: return "CAPTURE_ARMING";
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
    case FC_TRACKING_LOST: return "TRACKING_LOST";
    case FC_SELFSPIN_ABORT: return "SELFSPIN_ABORT";
    case FC_UNKNOWN_PERSISTED: return "UNKNOWN_PERSISTED";
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
  if (diagnosticCapture && diagnosticSawMotion &&
      result != RES_CONTROLLED_SAFE && result != RES_EDGE_SAFE &&
      result != RES_OFF_TARGET_SAFE) diagnosticFrozen = true;
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
void pwPersistFaultLatch(uint8_t code) {
  // A future firmware's nonzero latch must not be erased by a boot-time fault.
  if (pwUnknownPersistedFault(persistedFaultRaw, (uint8_t)FC_SELFSPIN_ABORT)) return;
  if (preferences.putUChar(PW_S1_NVS_KEY, code) == 1) persistedFaultRaw = code;
}

void enterFault(FaultCode code, const char* detail) {
  pwSelfspinFaultDisable(); // EN high before serial/NVS on diagnostic failures
  if (state == ST_FAULT_LATCHED) return;
  faultCode = code;
  if (diagnosticCapture) diagnosticFrozen = true;  // before NVS/serial latency
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
  return pwSelfspinAllowControl() && takeoverEnabled && motorDirectionCalibrated && tmcOk &&
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
  if (!GENTLE_BRAKE_TEST && fits < FIT_PERSIST_MIN_FITS && !urgent &&
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
  state = ST_TARGET_RESERVED;
  stateEnteredMs = nowMs;
  Serial.printf("SPIN#%lu RESERVE targetW=%d targetAngle=%.1f runway=%.1f natStop=%.1f "
                "speed=%.3f q=%u urgent=%d\n",
                (unsigned long)spin.number, choice.wedge, choice.targetAngleDeg,
                choice.runwayDeg, spin.naturalStopDeg, speed, choice.quality,
                urgent && !inWindow);
  return true;
}

// Recompute from the current encoder angle, including travel during EN-high
// pulse verification. Does not energize, reset position, or print/block serial.
bool prepareCapturePlan(uint32_t hz, float forward) {
  if (GENTLE_BRAKE_TEST) {
    TargetChoice test = chooseGentleTestTarget(takeoverDir, wheelAngleDeg(), hz, 0);
    if (!test.found || isDare(test.wedge)) return false;
    targetCountsMT = encoderCountsMT + takeoverDir * countsForDegrees(test.runwayDeg);
    planDecelCapSps2 = test.decelCapSps2;
    spin.targetWedge = test.wedge;
    spin.targetAngleDeg = test.targetAngleDeg;
    spin.runwayDeg = degreesForCounts(takeoverDir * (targetCountsMT - reserveCounts));
    spin.targetQuality = test.quality;
  }
  float remaining = remainingTargetDeg();
  if (!isfinite(remaining) || remaining < 7.0f || isDare(spin.targetWedge)) return false;
  PwBrakePlan plan = pwPlanBrake(hz, remaining, WHEEL_USTEPS_PER_REV, planDecelCapSps2);
  if (!plan.feasible || (spin.targetQuality == 0 &&
      plan.decelRevS2 > naturalDecelRevS2(forward, takeoverDir))) return false;
  planDecelRevS2 = plan.decelRevS2;
  return fasSetAcceleration(plan.accelerationSps2);
}

bool launchCapture(uint32_t nowMs) {
  // One accepted capture attempt for the entire self-spin trial, even if a
  // guest/reversal branch would otherwise open a new ordinary spin record.
  if (!pwSelfspinBeginCaptureAttempt()) {
    pwSelfspinCaptureAbort("capture attempt interlock");
    return false;
  }
  int fasSign = fasSignForEncoderDirection(takeoverDir);
  float forward = omega * (float)takeoverDir;
  if (!fasSign || !stepper || stepper->isRunning() ||
      digitalRead(PIN_EN) != HIGH || !encoderMotionReady() ||
      !isfinite(forward) || forward < 0.02f || !pwSelfspinCaptureDriverHealthy()) {
    pwSelfspinCaptureAbort("capture arming prerequisites");
    return false;
  }
  // Retain 0.95 for this isolated current/timing experiment. This matches
  // pulse progression approximately; it does NOT measure rotor phase.
  captureEntryHz = (uint32_t)floorf(fminf(TRAIL_FRACTION * forward,
                                       CAPTURE_MAX_CMD_REV_S) * WHEEL_USTEPS_PER_REV);
  if (!prepareCapturePlan(captureEntryHz, forward) || !fasSetSpeedHz(captureEntryHz)) {
    pwSelfspinCaptureAbort("capture initial runway/plan");
    return false;
  }
  uint32_t acceleration = stepper->getAcceleration();
  if (!acceleration) { pwSelfspinCaptureAbort("capture acceleration zero"); return false; }
  uint32_t jumpStep = (uint32_t)lroundf((float)captureEntryHz * captureEntryHz /
                                     (2.0f * acceleration));
  stepper->setJumpStart(jumpStep);
  if (!pwSelfspinBeforePositionReset()) return false;
  stepper->setCurrentPosition(0); // old queue is empty; no powered position reset
  captureDirHigh = fasSign > 0 ? INVERT_DIR : !INVERT_DIR;
  stepper->setDirectionPin(PIN_DIR, captureDirHigh);
  state = ST_CAPTURE_ARMING;
  stateEnteredMs = nowMs;
  setCurrentStage(CS_CAPTURE_PREPARED); // full-current registers; EN remains high
  if (!captureArm.begin(micros(), stepper->getCurrentPosition(),
                        captureEntryHz, captureDirHigh) ||
      digitalRead(PIN_EN) != HIGH || !fasRun(1)) {
    pwSelfspinCaptureAbort("pulse-first launch rejected");
    return false;
  }
  cmdRevS = (float)captureEntryHz / WHEEL_USTEPS_PER_REV;
  lastAppliedHz = captureEntryHz;
  // No powered-state timers or serviceDecelTick until actual torque-on.
  return true;
}

void serviceCaptureArming(uint32_t nowMs) {
  if (!stepper || faultCode != FC_NONE || currentStage != CS_CAPTURE_PREPARED) {
    pwSelfspinCaptureAbort("capture arming state");
    return;
  }
  const float forward = omega * (float)takeoverDir;
  PwCaptureArm::Verdict decision = captureArm.update(micros(),
      stepper->getCurrentPosition(), digitalRead(PIN_EN) == HIGH,
      digitalRead(PIN_DIR) == HIGH, encoderMotionReady(), forward,
      WHEEL_USTEPS_PER_REV, TRAIL_FRACTION);
  if (decision == PwCaptureArm::WAIT) return;
  if (decision == PwCaptureArm::REJECT) {
    // Cutoff precedes all serial/NVS output. The numeric reason is read-only.
    pwSelfspinCaptureAbort("pulse-first observation rejected");
    Serial.printf("# PULSEFIRST reject=%u hz=%.1f requested=%lu\n",
        (unsigned)captureArm.reason, captureArm.pulseHz, (unsigned long)captureEntryHz);
    return;
  }
  // Re-plan exactly once using the NOW-current angle. Applying acceleration
  // changes the forthcoming brake ramp; queued constant-speed pulses remain.
  if (!stepper->isRunning() || !pwSelfspinCaptureDriverHealthy() ||
      !prepareCapturePlan(captureEntryHz, forward)) {
    pwSelfspinCaptureAbort("capture final health/runway/plan");
    return;
  }
  stepper->applySpeedAcceleration();
  // Recheck after SPI/config traffic; never enable on stale observations.
  decision = captureArm.update(micros(), stepper->getCurrentPosition(),
      digitalRead(PIN_EN) == HIGH, digitalRead(PIN_DIR) == HIGH,
      encoderMotionReady(), omega * (float)takeoverDir,
      WHEEL_USTEPS_PER_REV, TRAIL_FRACTION);
  if (decision != PwCaptureArm::READY || !pwSelfspinEnableOutputs() ||
      digitalRead(PIN_EN) != LOW) {
    pwSelfspinCaptureAbort("capture torque-on interlock");
    return;
  }
  // Registers already contain the capture current: bookkeeping only. Do not
  // issue another current write or STEP/position reset across the EN edge.
  currentStage = CS_CAPTURE;
  const uint32_t torqueOnMs = millis();
  lastResyncMs = torqueOnMs;
  trailRingReset(forward);
  speedupWatch.reset();
  fightSinceMs = oppositeSinceMs = velocityLossSinceMs = guestOverrideAboveMs = 0;
  stopRequested = false;
  captureStartMs = controlStartMs = lastCmdTickMs = prevTickMs = torqueOnMs;
  prevTickWheelRevS = forward;
  spin.takeoverRevS = forward;
  spin.cmdMaxRevS = spin.cmdMinRevS = cmdRevS;
  state = ST_SPEED_MATCH_CAPTURE;
  stateEnteredMs = torqueOnMs;
  Serial.printf("# PULSEFIRST TORQUE_ON arm_us=%lu pulses=%ld hz=%.1f requested=%lu "
                "fraction=%.2f current=%u EN=%d DIR=%d phase_unverified=1\n",
      (unsigned long)(micros() - captureArm.startUs),
      (long)(stepper->getCurrentPosition() - captureArm.startPosition),
      captureArm.pulseHz, (unsigned long)captureEntryHz, TRAIL_FRACTION,
      g_currentMa, digitalRead(PIN_EN), digitalRead(PIN_DIR));
  Serial.printf("SPIN#%lu CAPTURE fasDir=%+d wheel=%.3f cmd0=%.3f hz=%lu "
                "aPlan=%.4f(%lu sps2) aFas=%lu remain=%.1f\n",
      (unsigned long)spin.number, fasSignForEncoderDirection(takeoverDir),
      forward, cmdRevS, (unsigned long)captureEntryHz, planDecelRevS2,
      (unsigned long)stepper->getAcceleration(), (unsigned long)stepper->getAcceleration(),
      remainingTargetDeg());
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
    speedupWatch.reset();  // incomplete velocity evidence cannot age the debounce
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

  // 5. A short low outlier must not poison the reference for the rest of
  // the spin. Preserve the speed-rise threshold and confirmation time,
  // but establish the baseline and rising condition from robust samples.
  bool sustainedSpeedup = speedupWatch.update(nowMs, forward,
      SPEEDUP_NOISE_REV_S, SPEEDUP_FAULT_MS);
  if (speedupWatch.rise() > spin.maxSpeedRiseRevS)
    spin.maxSpeedRiseRevS = speedupWatch.rise();
  if (sustainedSpeedup) {
    Serial.printf("# SPEEDUP median=%.3f baseline=%.3f rise=%.3f sustained_ms=%lu\n",
                  speedupWatch.median(), speedupWatch.baseline(), speedupWatch.rise(),
                  (unsigned long)speedupWatch.sustainedMs(nowMs));
    enterFault(FC_SUSTAINED_SPEEDUP, "sustained robust speed rise under power");
    return false;
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

  if (stopRequested) {
    // The library now owns the smooth final ramp at planDecelRevS2.
    cmdRevS = fminf(cmdRevS, fasWheelRevS());
    if (cmdRevS < spin.cmdMinRevS) spin.cmdMinRevS = cmdRevS;
    return;
  }

  float remaining = remainingTargetDeg();
  int32_t toleranceCounts = countsForDegrees(OVERSHOOT_TOL_DEG);

  // Both overshoot and normal stop use the SAME planned acceleration.
  // Keep braking current while pulses ramp down; do not weaken coupling
  // at the instant the motor is asked to finish the stop.
  float stopLeadDeg = (float)stepper->stepsToStop() * 360.0f / WHEEL_USTEPS_PER_REV
                    + STOP_GATE_EXTRA_DEG;
  if (takeoverDir * (encoderCountsMT - targetCountsMT) > toleranceCounts ||
      remaining <= stopLeadDeg) {
    stopRequested = true;
    stepper->stopMove();
    Serial.printf("# RAMP_STOP a=%lu remain=%.1f lead=%.1f fas=%.3f\n",
                  (unsigned long)stepper->getAcceleration(), remaining,
                  stopLeadDeg, fasWheelRevS());
    return;
  }

  // Position supplies a desired speed, while elapsed time bounds changes.
  // The hardware uses the same acceleration limit, rather than an 8x ramp.
  float prevCmd = cmdRevS;
  float remRev = fmaxf(remaining, 0.0f) / 360.0f;
  float profile = sqrtf(2.0f * planDecelRevS2 * remRev);
  float newCmd = fminf(prevCmd, profile);

  float trailMin = trailingMinForwardRevS();  // telemetry / debug reference
  // A slower encoder can lower the desired command, but cannot introduce
  // an unbounded step. Existing reversal, speed-up and fight faults remain.
  float couplingSlack = (state == ST_SPEED_MATCH_CAPTURE) ? 0.0f : COUPLING_SLACK_REV_S;
  if (encoderVelocityValid && forward < newCmd - couplingSlack) {
    // Request a lower field speed when the encoder is slower. The ramp
    // limiter below bounds the response; the fault monitors remain active.
    newCmd = fminf(newCmd, fmaxf(forward, 0.0f));
  }

  newCmd = pwLimitBrakeCommand(prevCmd, newCmd, planDecelRevS2,
                               (float)dtMs * 0.001f);

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
    stepper->stopMove();
    cmdRevS = newCmd;
    if (cmdRevS < spin.cmdMinRevS) spin.cmdMinRevS = cmdRevS;
    return;
  }

  cmdRevS = newCmd;
  if (cmdRevS < spin.cmdMinRevS) spin.cmdMinRevS = cmdRevS;

  // Avoid unnecessary library ramp re-quantization. Actual acceleration
  // stays bounded by aPlanSps2 even when a lower target is requested.
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
  holdReleaseSinceMs = 0;
  holdReleaseDir = 0;
  holdLastHealthMs = stateEnteredMs;
  Serial.printf("# HOLD current=%u mA; retained until sustained spin motion\n", g_currentMa);
}

/* ========================================================================== */
/*                          DIAGNOSTIC DUMP                                   */
/* ========================================================================== */
void dumpDiagnostics() {
  if (diagnosticCount == 0) {
    Serial.println(F("# DIAG: no samples captured."));
    return;
  }
  diagnosticDumpFirst = (diagnosticHead + DIAG_CAPACITY - diagnosticCount) % DIAG_CAPACITY;
  diagnosticDumpIndex = 0;
  diagnosticDumpPhase = 0;
  diagnosticDumpLength = diagnosticDumpOffset = 0;
  diagnosticDumpActive = true;
}

// Drain the trace without blocking the 1 kHz controller or extending hold time.
// Start only after safe settling; enqueue one complete line when UART has room.
void serviceDiagnosticDump() {
  if (!diagnosticDumpActive) return;
  if (diagnosticDumpOffset == diagnosticDumpLength) {
    if (diagnosticDumpPhase == 0) {
      snprintf(diagnosticDumpLine, sizeof(diagnosticDumpLine),
        "# DIAG columns: done_us,dt_good_us,raw,delta,counts,i2c_us,"
        "omega_mrev,window_mrev,cmd_mrev,fas_mrev,remain_ddeg,"
        "flags_hex,state,stage,current_ma,step_count,step_us,drv_status_hex,"
        "tstep,driver_age_us,gstat_hex,pins_hex\n");
      diagnosticDumpPhase = 1;
    } else if (diagnosticDumpIndex < diagnosticCount) {
      const DiagnosticSample& s = diagnosticBuffer[(diagnosticDumpFirst + diagnosticDumpIndex) % DIAG_CAPACITY];
      snprintf(diagnosticDumpLine, sizeof(diagnosticDumpLine),
        "D,%lu,%u,%u,%d,%ld,%u,%d,%d,%d,%d,%d,%02X,%u,%u,%u,%ld,%lu,%08lX,%lu,%u,%02X,%02X\n",
        (unsigned long)s.doneUs, s.dtGoodUs, s.raw, s.delta,
        (long)s.counts, s.i2cUs, s.omegaMilliRevS, s.windowMilliRevS,
        s.cmdMilliRevS, s.fasMilliRevS, s.remainDeciDeg,
        s.flags, s.state, s.stage, (unsigned)s.curMa10 * 10,
        (long)s.stepCount, (unsigned long)s.stepUs, (unsigned long)s.drvStatus,
        (unsigned long)s.tstep, s.driverAgeUs, s.gstat, s.pins);
      ++diagnosticDumpIndex;
    } else if (diagnosticDumpPhase == 1) {
      snprintf(diagnosticDumpLine, sizeof(diagnosticDumpLine),
        "# DIAG n=%u wrapped=%d frozen=%d\n", diagnosticCount, diagnosticWrapped, diagnosticFrozen);
      diagnosticDumpPhase = 2;
    } else {
      diagnosticDumpActive = false;
      return;
    }
    diagnosticDumpLength = strlen(diagnosticDumpLine);
    diagnosticDumpOffset = 0;
  }
  int available = Serial.availableForWrite();
  if (available <= 0) return;
  size_t count = diagnosticDumpLength - diagnosticDumpOffset;
  // Enqueue complete rows so event messages cannot split a CSV record.
  if (count > (size_t)available) return;
  diagnosticDumpOffset += Serial.write((const uint8_t*)diagnosticDumpLine + diagnosticDumpOffset, count);
}

void startDiagnosticCapture() {
  if (!diagnosticBuffer) { Serial.println(F("# DIAG unavailable: PSRAM allocation failed")); return; }
  if (diagnosticDumpActive) { Serial.println(F("# DIAG dump still in progress")); return; }
  diagnosticDriverStage = 255;
  diagnosticHead = 0;
  diagnosticCount = 0;
  diagnosticWrapped = false;
  diagnosticFrozen = false;
  diagnosticCapture = true;
  diagnosticSawMotion = false;
  diagnosticStillSinceUs = 0;
  Serial.println(F("# DIAG armed: 16384 PSRAM samples, PCNT + driver status; includes 5 seconds of hold."));
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
    // Include five seconds of persistent hold before freezing this trace.
    bool held = state == ST_SOFT_HOLD && currentStage == CS_HOLD1 &&
                digitalRead(PIN_EN) == LOW;
    if (held && millis() - stateEnteredMs < DIAG_HOLD_RECORD_MS) return;
    diagnosticFrozen = true;
    if (held || (currentStage == CS_FREEWHEEL && digitalRead(PIN_EN) == HIGH)) {
      diagnosticCapture = false;
      dumpDiagnostics();
    }
  }
}

/* ========================================================================== */
/*                             SERIAL UI                                      */
/* ========================================================================== */
void help() {
  Serial.println(F(
    "\n=== PRIZE WHEEL (correctness redesign) ===\n"
    "# build: v2-capture-pulsefirst-20260920; based on gentle6\n"
    " [ / ] one-shot encoder-dir -1 / +1; ! sticky abort; no automatic rearm\n"
    " z  set current raw as wedge-0 anchor (wheel at rest, pointer on 11|0 line)\n"
    " p  attended two-leg direction probe (safe wedge center only)\n"
    " s  status\n"
    " d  arm high-rate RAM capture (dumps after true stop/fault)\n"
    " D  dump current trace at rest with outputs disabled\n"
    " v  toggle live control logs\n"
    " e  toggle automatic takeover\n"
    " f  print friction model\n"
    " F  reset friction model to seeds\n"
    " x  clear stored direction calibration\n"
    " r  disabled build only: acknowledge preserved trace, recover known SELFSPIN_ABORT\n"
    " m  print dare mask\n"
    " ?  help"));
  pwPartyHelpLines();
  pwSelfspinPrintStatus(); // reports actual compile-time motion setting
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
  Serial.printf("# motor fas=%.4f current=%u EN=%d hold_ms=%lu\n", fasWheelRevS(),
                g_currentMa, digitalRead(PIN_EN),
                (unsigned long)(state == ST_SOFT_HOLD ? millis() - stateEnteredMs : 0));
  pwSelfspinPrintStatus();
  Serial.printf("# saved_fault_raw=%u; unknown nonzero values stay locked\n", persistedFaultRaw);
}

// WIFI_TASK: one shared single-char parser for the serial console AND the
// telnet clients.  Existing guards (z only while IDLE, etc.) apply to both.
void handleSerial() {
  if (!Serial.available()) return;
  char command = (char)Serial.read();
  if (pwSelfspinCommand(command)) return;
  if (pwPartyCommandChar(command)) return;   // party commands: t/a/l/w/V<n>
  handleCommandChar(command);
}

void handleCommandChar(char command) {
  if (pwSelfspinCommand(command)) return; // covers any non-serial command source
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
      if (diagnosticCapture && state != ST_IDLE_STOPPED &&
          state != ST_SOFT_HOLD && state != ST_FAULT_LATCHED)
        Serial.println(F("# DIAG active: moving status suppressed"));
      else printStatus();
      break;
    case 'D':
      if (diagnosticDumpActive) break;
      if ((state != ST_IDLE_STOPPED && state != ST_FAULT_LATCHED) ||
          currentStage != CS_FREEWHEEL || digitalRead(PIN_EN) != HIGH ||
          (stepper && stepper->isRunning()) || !encoderMotionReady() ||
          fabsf(omega) > STILL_REV_S) {
        Serial.println(F("# DIAG manual dump refused: wheel must be stopped with outputs disabled"));
        break;
      }
      diagnosticFrozen = true;
      diagnosticCapture = false;
      dumpDiagnostics();
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
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH); // establish no-torque state before initialization
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);
  delay(300);
  randomSeed(esp_random());
  diagnosticBuffer = (DiagnosticSample*)heap_caps_malloc(
      sizeof(DiagnosticSample) * DIAG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  Serial.printf("# DIAG PSRAM samples=%u bytes=%lu allocated=%d\n", DIAG_CAPACITY,
                (unsigned long)(sizeof(DiagnosticSample) * DIAG_CAPACITY), diagnosticBuffer != nullptr);

  bool nvsReady = preferences.begin("prizewheel", false);
  persistedFaultRaw = nvsReady ? preferences.getUChar(PW_S1_NVS_KEY, 0) : 255;
  recoveryGuardRaw = nvsReady ? preferences.getUChar(PW_RECOVERY_GUARD_KEY, 0) : 255;
  if (recoveryGuardRaw != 0) {
    // Preserve unknown primary values. A known interrupted recovery with an
    // already-cleared primary latch is still locked, and cannot pass r's exact
    // saved-primary-15 prerequisite without a separately reviewed repair.
    persistedFaultRaw = pwRestoreFaultWithRecoveryGuard(persistedFaultRaw,
        recoveryGuardRaw, (uint8_t)FC_SELFSPIN_ABORT);
    Serial.printf("# RECOVERY_GUARD raw=%u; interrupted recovery, outputs locked\n", recoveryGuardRaw);
  }
  if (persistedFaultRaw != 0) {
    faultCode = pwUnknownPersistedFault(persistedFaultRaw, (uint8_t)FC_SELFSPIN_ABORT)
        ? FC_UNKNOWN_PERSISTED : (FaultCode)persistedFaultRaw;
    state = ST_FAULT_LATCHED;
    stateEnteredMs = millis();
    Serial.printf("# S1 early restore raw=%u fault=%s; outputs locked\n",
                  persistedFaultRaw, faultName(faultCode));
  }
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
  // v2 reseed: old fit (0.30/0.15) was tuned for the NEMA17/belt wheel and
  // understates this 36" wheel's real drag, so the brake plan is consistently
  // overtaken by faster-than-planned natural deceleration (FC_MOTOR_FIGHT).
  // Forcibly override whatever is persisted; online fit refines from here.
  cw_c = 0.55f; cw_b = 0.28f;
  ccw_c = 0.55f; ccw_b = 0.28f;
  cwFitCount = 0; ccwFitCount = 0;
  preferences.putFloat("cwC", cw_c);   preferences.putFloat("cwB", cw_b);
  preferences.putFloat("ccwC", ccw_c); preferences.putFloat("ccwB", ccw_b);
  preferences.putUShort("cwN", 0);     preferences.putUShort("ccwN", 0);
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
  digitalWrite(PIN_EN, HIGH);  // keep outputs disabled during initialization

  driverConfig();   // TMC5160T Pro over SPI - no serial begin needed

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP, DRIVER_MCPWM_PCNT);  /* S3: keep FAS off RMT so FastLED owns it (see S3_PORT.md) */
  if (stepper) {
    stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
    stepper->setEnablePin(PIN_EN, true);
    stepper->setAutoEnable(false);
    if (!pwFixStepperClock()) {
      stepper->disableOutputs();
      stepper = nullptr;  // reset cannot bypass an unsupported timer layout
      enterFault(FC_STEPPER_API, "unsupported STEP clock layout");
    }
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
  pwSelfspinBegin();
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
  pwSelfspinService();
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
    case ST_SELFSPIN: break; // serviced above; no ordinary spin classification
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
      // EN stayed high at reservation. Failure consumes this whole trial.
      launchCapture(nowMs);
      break;
    }

    case ST_CAPTURE_ARMING: {
      serviceCaptureArming(nowMs);
      break;
    }

    case ST_SPEED_MATCH_CAPTURE: {
      if (!controlSafetyChecks(nowMs)) break;
      serviceDecelTick(nowMs);   // trailing window keeps filling; cmd is const
      if (state != ST_SPEED_MATCH_CAPTURE) break;  // tick may have faulted
      if (nowMs - captureStartMs >= PICKUP_COHERENCE_MS) {
        // A planned stop may already be running; keep full braking torque.
        setCurrentStage(CS_BRAKE);
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
      // Pulse train has tapered to zero; retain braking torque during settle.
      // Landing requires BOTH interior position AND stillness.
      if (!encoderPositionFresh()) {
        enterFault(FC_ENCODER_STALE, "stale during settle");
        break;
      }
      setCurrentStage(CS_BRAKE);  // retain torque through settling and hold
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
      // Excess travel after pulses stop is a failed controlled landing.
      // Motion alone does not prove guest contact; latch and preserve the
      // trace instead of automatically capturing the same coast again.
      if (fabsf(degreesForCounts(encoderCountsMT - settleEntryCounts)) >
              LANDING_DRAG_ABORT_DEG &&
          encoderMotionReady() && fabsf(omega) > STILL_REV_S) {
        enterFault(FC_TRACKING_LOST, "excess travel after pulse stop");
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
      // Zero STEP pulses with continuous holding torque: no timed freewheel.
      if (!encoderPositionFresh()) {
        enterFault(FC_ENCODER_STALE, "encoder stale during persistent hold");
        break;
      }
      if (nowMs - holdLastHealthMs >= HOLD_HEALTH_MS) {
        holdLastHealthMs = nowMs;
        uint32_t drv = driver.DRV_STATUS();
        uint8_t gst = (uint8_t)driver.GSTAT();
        // Temperature warning/shutdown, shorts, driver/charge-pump error.
        // Open-load flags are unreliable at rest and are not used here.
        if (drv == 0 || drv == 0xFFFFFFFFUL || (drv & 0x1E000000UL) || (gst & 0x07)) {
          enterFault(FC_TMC_UART, "driver health fault during persistent hold");
          break;
        }
      }
      // A single noisy sample must not unlock the wheel. This detects motion,
      // not human force: holding torque must first prevent imbalance drift.
      if (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S) {
        int8_t direction = omega >= 0.0f ? 1 : -1;
        if (holdReleaseSinceMs == 0 || direction != holdReleaseDir) {
          holdReleaseSinceMs = nowMs;
          holdReleaseDir = direction;
        } else if (nowMs - holdReleaseSinceMs >= HOLD_RELEASE_CONFIRM_MS) {
          driverFreewheel();
          Serial.printf("# HOLD released: sustained motion dir=%+d omega=%.3f\n", direction, omega);
          candidateStartCounts = encoderCountsMT;
          spinArmMs = 0;
          settleStillSinceMs = 0;
          holdReleaseSinceMs = 0;
          state = ST_MOTION_CANDIDATE;
          stateEnteredMs = nowMs;
        }
      } else {
        holdReleaseSinceMs = 0;
        holdReleaseDir = 0;
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
  serviceDiagnosticDump();

  // Party additions run LAST, after all control work, and measure themselves
  // against the <=2 ms FX+WiFi budget ('t' prints the max-tracker).
  pwPartyService(pwLoopStartUs);
}

// Implementations of the party additions; included last so they can observe
// every control global without any forward-declaration surgery above.
#include "pw_party_impl.h"
#include "pw_selfspin.h"
