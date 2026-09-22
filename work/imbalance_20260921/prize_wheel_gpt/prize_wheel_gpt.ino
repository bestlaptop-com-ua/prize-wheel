/* ============================================================================
 * prize_wheel_gpt.ino - Prize wheel firmware, correctness redesign
 *
 * Unvalidated production integration: takeover defaults OFF. Eligible spins
 * may be captured within the bounded test envelope and guided to a selected
 * safe wedge. An infeasible plan remains a free coast. Wedges 1 and 5 are never
 * selected targets; landing feedback still determines the actual result.
 *
 * State machine:
 *   IDLE_STOPPED -> MOTION_CANDIDATE -> (MANUAL_ADJUSTMENT | SPIN_PUSH)
 *   SPIN_PUSH -> SPIN_RELEASED -> TARGET_RESERVED -> CAPTURE_ARMING -> SPEED_MATCH_CAPTURE
 *   -> CONTROLLED_DECEL -> LANDING_SETTLE -> SOFT_HOLD -> IDLE_STOPPED
 *   Any powered state -> FAULT_LATCHED on hardware/invariant failure.
 *
 * Hard rules enforced here:
 *   - Spin detection is purely kinematic; target availability never delays or
 *     reclassifies a spin.
 *   - Command speed never increases after capture. Target selection retains
 *     the brake-reachable model; pulse-speed agreement does not establish
 *     rotor electrical phase or prove exclusively braking mechanical torque.
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
 *           AS5600 on the motor rear shaft (not independent disc feedback).
 * Build:    ESP32 Arduino core 3.3.10, FastAccelStepper 1.2.7, TMCStepper.
 * ========================================================================== */

#include <Wire.h>
#include <Preferences.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include "pw_step_clock.h"
#include "pw_fault_policy.h"
#include "pw_brake_profile.h"
#include "pw_speedup_watch.h"
#include "pw_capture_arm.h"
#include "pw_capture_cycle.h"
#include "pw_capture_lease.h"
#include "pw_fit_lifecycle.h"
#include "pw_friction_store.h"
#include "pw_imbalance.h"
// Party additions (WiFi + FX + sanctioned fixes S1/S2/S3): declarations,
// config switches and the serial mirror.  Implementations are included at the
// very bottom of this file.  See PARTY_TASK.md / DELIVERY.md.
#include "pw_party.h"

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
#define NUM_WEDGES 18   // 2026-09-21 wheel: 20 deg wedges, labels 0-17 clockwise
const float WEDGE_DEG = 360.0f / NUM_WEDGES;

/* ------------------------- ENUMS (all hoisted) ---------------------------- */
enum State : uint8_t {
  ST_IDLE_STOPPED, ST_MOTION_CANDIDATE, ST_MANUAL_ADJUSTMENT,
  ST_SPIN_PUSH, ST_SPIN_RELEASED, ST_TARGET_RESERVED,
  ST_SPEED_MATCH_CAPTURE, ST_CONTROLLED_DECEL, ST_LANDING_SETTLE,
  ST_SOFT_HOLD, ST_DIR_PROBE, ST_FAULT_LATCHED,
  ST_CAPTURE_ARMING, // append: observe STEP with EN high before torque
  ST_RECOVERY_NUDGE  // 2026-09-21: powered step off a dare into the adjacent safe wedge
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
  FC_TRACKING_LOST = 14,   // persisted compatibility with the v2 bench build
  FC_SELFSPIN_ABORT = 15,  // persisted compatibility; no selfspin commands here
  FC_UNKNOWN_PERSISTED = 16, // raw stored ID is retained separately, never rewritten
  FC_CONTROL_OVERSPEED = 17 // instantaneous powered-control speed ceiling
};
static_assert(FC_LANDING_UNSAFE == 13 && FC_TRACKING_LOST == 14 &&
              FC_SELFSPIN_ABORT == 15 && FC_UNKNOWN_PERSISTED == 16 &&
              FC_CONTROL_OVERSPEED == 17,
              "Persisted fault IDs must remain compatible with deployed builds");

enum CurrentStage : uint8_t {
  CS_FREEWHEEL, CS_PRECHARGE, CS_CAPTURE, CS_BRAKE, CS_TAPER, CS_HOLD1, CS_HOLD2,
  CS_CAPTURE_PREPARED // full current registers, outputs remain disabled
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
// Wheel labels are 1-18; firmware indices are label-1 (index 0 = label 1, at the 18|1 line).
// Owner dares by LABEL: 3, 8, 13, 16 (8 is the hard one) -> indices 2, 7, 12, 15.
uint32_t dare_mask = (1UL << 2) | (1UL << 7) | (1UL << 12) | (1UL << 15);
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
// Retained conservative model allowance for arming plus pickup. The actual
// EN-high pulse-arming lease is independently limited to 150 ms.
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
// Full-current registers are prepared with EN high. Pulse proof precedes the
// first enable edge; no static field dwell or low-current capture stage.
const uint16_t PICKUP_COHERENCE_MS    = 250;
// Friction-model bootstrap: while a direction has fewer than the persist
// threshold of valid fits, defer engagement (bounded by window width and
// time) so the release coast can feed the online fit.
const float CAL_DEFER_MIN_WIDTH_DEG   = 120.0f;
const uint32_t CAL_DEFER_MAX_MS       = 3500;
// Additional floor before the pulse-arming travel and 7-degree final plan gate.
const float MIN_RESERVE_RUNWAY_DEG    = 8.0f;

// --- braking profile ---
const uint16_t CMD_UPDATE_MS          = 25;     // control tick
const uint16_t CMD_RESYNC_MS          = 200;    // field-vs-command resync
const float CMD_RESYNC_TOLERANCE      = 0.08f;  // relative field deviation
// Entry is 0.95 of the current filtered forward speed. EN-high pulse evidence
// checks that requested progression; it does not establish electrical phase
// or guarantee gentle coupling. The trailing window is retained for telemetry.
const float TRAIL_FRACTION            = 0.98f;  // 2026-09-21: 5% field lag snapped the wheel at torque-on
const uint16_t TRAIL_WINDOW_TICKS     = 8;      // ~200 ms trailing window
// 2026-09-21 party policy: engage in the 0.72 rev/s window like the certified
// 24in build, not a last-second grab at 0.20 (coupling slip is fixed).
const float CAPTURE_MAX_CMD_REV_S     = 0.38f;  // 0.95 x engage ceiling
const float CAPTURE_MAX_WHEEL_REV_S   = 0.40f;  // brake arcs must fit the 160 deg uphill half
const uint32_t DECEL_CEILING_SPS2     = 600;    // uphill arcs only: gravity supplies most of this
const uint32_t ASSIST_DECEL_MAX_SPS2  = 600;    // pass-3 braking to nearest safe interior
const uint32_t SHADOW_DECEL_MAX_SPS2  = 1000;   // pass-2 shadow capture: plan ~= natural decel, motor only corrects
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
// The pulse generator, command limiter and final stop use the same planned
// deceleration. No faster tracking ramp or clipped infeasible plan is used.

// --- landing / hold ---
const float STILL_REV_S               = 0.020f;
const uint16_t SETTLE_MS              = 500;
const uint16_t HOLD1_MS               = 1500;   // unused: hold is persistent
const float HOLD_RELEASE_DEFLECT_DEG  = 1.2f;   // below the 1.8 deg pole-slip load angle
const uint16_t HOLD_RELEASE_CONFIRM_MS = 30;
float holdAnchorDeg = 0.0f;
bool holdAnchorValid = false;
uint32_t holdDeflectSinceMs = 0;
const uint16_t HOLD2_MS               = 1200;

// --- current ladder (written ONLY on stage transitions) ---
// With the profile-PACED command law the motor brakes through the load angle
// of a synchronized rotor, and holding that synchronization is what needs
// current: at 300 mA the rotor hops poles under the required drag (rattle),
// at 600 mA it stays locked and silent (owner bench ladder; 650 hums, 180
// rattles).  Current sets coupling stiffness; the braking force itself is
// set by the commanded profile.  (600/450 only over-braked under the old
// continuously-trailing law, which forced multi-pole slip at any current.)
const uint16_t CUR_PRECHARGE_MA = 350;  // legacy stage retained for the explicit direction probe
const uint16_t CUR_CAPTURE_MA   = 2800; // 2026-09-22: 2240 proved 20% headroom; party runs at 2800
const uint16_t CUR_BRAKE_MA     = 2800; // retain capture torque through braking/settling
const uint16_t CUR_TAPER_MA     = 1100;  // final taper / settle watch
const uint16_t CUR_HOLD1_MA     = 1650; // continuous hold: gravity (0.57 rad/s2) beats friction 3:1
const uint16_t CUR_HOLD2_MA     = 300;   // ...to freewheel

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
const PwFrictionModel FRICTION_SEED = {0.17f, 0.05f, 0}; // 2026-09-21 hand-spin fits (<0.7 rev/s), gravity separated
const PwFrictionBounds FRICTION_BOUNDS = {FIT_C_MIN, FIT_C_MAX, FIT_B_MIN, FIT_B_MAX};

/* --------------------------- STATE --------------------------------------- */
TMC5160Stepper driver(TMC_CS_PIN, R_SENSE, TMC_MOSI_PIN, TMC_MISO_PIN, TMC_SCK_PIN);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper* stepper = nullptr;
Preferences preferences;
bool preferencesAvailable = false;
uint8_t persistedFaultRaw = 0;
uint8_t recoveryGuardRaw = 0;
// Dare recovery (2026-09-21). On this wheel a dying coast balances on the
// crest (wedge 8, the hard dare) or settles in the rest cone that overlaps
// wedge 16. A resting wheel on a dare is stepped, energized, into the centre
// of the adjacent safe wedge in the downhill direction: it reads as the
// wheel teetering and settling, and it ends in a held safe wedge.
const uint32_t RECOVERY_SPEED_HZ  = 220;   // ~0.07 rev/s
const uint32_t RECOVERY_ACCEL_SPS2 = 400;
const uint16_t RECOVERY_CURRENT_MA = 2200;
const uint32_t RECOVERY_TIMEOUT_MS = 8000;
uint8_t dareRecoveryAttempts = 0;
uint32_t recoveryStillSinceMs = 0;
bool startDareRecovery(const char* why);

State state = ST_IDLE_STOPPED;
FaultCode faultCode = FC_NONE;
CurrentStage currentStage = CS_FREEWHEEL;
uint16_t g_currentMa = 0;   // actual commanded rms current, for telemetry
bool debugLog = false;
bool takeoverEnabled = false; // integration unvalidated; explicit enable required
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
// encoderCountsMT = uncorrected (rawZero - raw) accumulator + AS5600 INL offset.
// With no stored model the offset is 0 and both are identical (legacy behaviour).
int32_t encoderRawCountsMT = 0;
static_assert(ENCODER_DIR_SIGN == -1, "INL offset sign assumes counts = rawZero - raw");
PwEncoderInl encoderInl;
PwGravity wheelGravity;
PwImbalanceLine imbalanceLine;
bool imbalanceLoaded = false;
// Reach is judged at 96% speed: with gravity > friction the last crest is a
// cliff (a 1% speed error can move the natural stop by most of a revolution).
const float IMB_NATURAL_SPEED_MARGIN = 0.96f;
// Mechanical-energy-equivalent speed at hand release; 0 = unknown (legacy re-push test).
float releaseCompRevS = 0.0f;
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
const uint16_t DIAG_CAPACITY = 2304;   // original bench capacity
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
uint32_t captureStartMs = 0;
uint32_t lastPoweredHealthMs = 0;
uint32_t controlStartMs = 0;
float cmdRevS = 0.0f;
uint32_t lastCmdTickMs = 0;
uint32_t lastAppliedHz = 0;
uint32_t lastResyncMs = 0;
float trailRing[TRAIL_WINDOW_TICKS];
uint8_t trailRingCount = 0;
uint8_t trailRingHead = 0;
PwSpeedupWatch speedupWatch;
PwCaptureArm captureArm;
PwCaptureCycle captureCycle;
PwCaptureLease captureLease;
uint32_t captureEntryHz = 0;
bool captureDirHigh = false;
bool captureTimerReady = false;
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
struct FitSample { uint32_t ms; float omegaRadS; float angleRad; };
FitSample fitSamples[FIT_MAX_SAMPLES];
uint8_t fitSampleCount = 0;
int fitDir = 0;
uint32_t fitLastSampleMs = 0;
PwFitLifecycle fitLifecycle;

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
    encoderRawCountsMT = staticCounts;
  } else {
    int32_t diff = encoderRawCountsMT - staticCounts;
    int32_t turns = (int32_t)lroundf((float)diff / 4096.0f);
    encoderRawCountsMT = staticCounts + turns * 4096;
  }
  encoderCountsMT = encoderRawCountsMT + encoderInl.labelOffsetCounts(raw, rawZero);
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

  encoderRawCountsMT += ENCODER_DIR_SIGN * delta;   // invert to count clockwise
  encoderCountsMT = encoderRawCountsMT + encoderInl.labelOffsetCounts(read.raw, rawZero);
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
  // TMC5160 GSTAT is write-to-clear: the power-up reset flag (and any uv_cp)
  // stays latched forever otherwise, and captureDriverHealthy() requires 0.
  driver.GSTAT(0x07);
}

// The ONLY place TMC current registers are written after boot (the attended
// direction probe overrides the level once, via g_currentMa bookkeeping).
// Register traffic happens exclusively on stage transitions (defect-11), so
// the 1 kHz encoder sampling cadence is never disturbed by UART writes.
void setCurrentStage(CurrentStage next) {
  if (next != CS_FREEWHEEL && faultCode != FC_NONE) {
    digitalWrite(PIN_EN, HIGH);
    return;  // no stage transition may re-enable a latched controller
  }
  if (next == currentStage) return;
  if (next != CS_FREEWHEEL) fitLifecycle.discard(); // powered samples never enter a coast fit
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
    case CS_CAPTURE_PREPARED:
      digitalWrite(PIN_EN, HIGH);
      if (stepper) outputsOk = stepper->disableOutputs();
      driver.freewheel(0);
      driver.rms_current(CUR_CAPTURE_MA, 1.0);
      g_currentMa = CUR_CAPTURE_MA; // requested setting; EN still disabled
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
  captureLease.cancel(); // retire any pending EN permission before queue cleanup
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
  if (wheelGravity.active())  // from the CURRENT angle, conservative speed
    return wheelGravity.stopTravelRad(speedRevS * IMB_NATURAL_SPEED_MARGIN, dir,
        wheelAngleDeg() * DEG_TO_RAD, c, b, millis()) * RAD_TO_DEG;
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
  if (wheelGravity.active())
    return wheelGravity.stopTravelRad(speedRevS, dir, wheelAngleDeg() * DEG_TO_RAD,
        c, b, millis()) * RAD_TO_DEG;
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
  if (wheelGravity.active() && speedRevS >= 1e-3f) {
    // The instantaneous value swings through zero once per revolution, so use
    // the energy-average over the natural runway, floored at 25% of friction.
    float w = speedRevS * IMB_NATURAL_SPEED_MARGIN * TWO_PI;
    float travel = wheelGravity.stopTravelRad(speedRevS * IMB_NATURAL_SPEED_MARGIN, dir,
        wheelAngleDeg() * DEG_TO_RAD, c, b, millis());
    float friction = c + b * speedRevS * TWO_PI;
    float average = travel > 1e-3f ? (w * w) / (2.0f * travel) : friction;
    return fmaxf(average, 0.25f * friction) / TWO_PI;
  }
  return (c + b * speedRevS * TWO_PI) / TWO_PI;
}

void resetFrictionCapture(int dir) {
  fitSampleCount = 0;
  fitDir = dir;
  fitLastSampleMs = 0;
  fitLifecycle.begin();
}

// Samples are taken ONLY while the wheel free-coasts after hand release
// (motor floating, guest's hand off).  Contact, motor power, invalid encoder
// data, reversal, and speed-ups are all excluded before the fit.
void captureFrictionSample(uint32_t nowMs) {
  if (!fitLifecycle.canSample(currentStage == CS_FREEWHEEL && digitalRead(PIN_EN) == HIGH) ||
      fitDir == 0 || fitSampleCount >= FIT_MAX_SAMPLES) return;
  if (!encoderVelocityValid) return;
  float forward = omega * (float)fitDir;
  if (forward < FIT_MIN_SPEED_REV_S || forward > FIT_MAX_SPEED_REV_S) return;
  if (fitSampleCount > 0 && nowMs - fitLastSampleMs < FIT_SAMPLE_PERIOD_MS) return;
  fitSamples[fitSampleCount].ms = nowMs;
  fitSamples[fitSampleCount].omegaRadS = forward * TWO_PI;
  fitSamples[fitSampleCount].angleRad = wheelAngleDeg() * DEG_TO_RAD;
  ++fitSampleCount;
  fitLastSampleMs = nowMs;
}

void finishFrictionCapture(const char* reason, PwFitEnd end) {
  float span = fitSampleCount > 0
      ? fitSamples[0].omegaRadS - fitSamples[fitSampleCount - 1].omegaRadS : 0.0f;
  bool enoughData = fitDir != 0 && fitSampleCount >= FIT_MIN_SAMPLES &&
      span >= FIT_MIN_SPAN_RAD_S;
  if (!fitLifecycle.beginAttempt(end, enoughData, fitSampleCount)) return;

  float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f;
  int pairs = 0;
  for (uint8_t i = 0; i + FIT_PAIR_STRIDE < fitSampleCount; ++i) {
    uint8_t j = i + FIT_PAIR_STRIDE;
    float dtS = (float)(fitSamples[j].ms - fitSamples[i].ms) / 1000.0f;
    if (dtS < 0.2f || dtS > 3.0f) continue;
    float dropRadS = fitSamples[i].omegaRadS - fitSamples[j].omegaRadS;
    if (wheelGravity.active()) {
      // Give back what gravity did to the speed over this pair, so c/b stay
      // friction-only.  Segments spanning > 1 rad are left alone: the trapezoid
      // is meaningless there and whole revolutions cancel anyway.
      for (uint8_t k = i; k < j; ++k) {
        float segS = (float)(fitSamples[k + 1].ms - fitSamples[k].ms) / 1000.0f;
        float segRad = 0.5f * (fitSamples[k].omegaRadS + fitSamples[k + 1].omegaRadS) * segS;
        if (segRad > 1.0f) continue;
        dropRadS += 0.5f * segS *
            (wheelGravity.forwardAccel(fitSamples[k].angleRad, fitDir) +
             wheelGravity.forwardAccel(fitSamples[k + 1].angleRad, fitDir));
      }
    }
    if (dropRadS <= 0.0f) continue;          // reversal / speed-up: reject
    float alpha = dropRadS / dtS;
    if (alpha > FIT_ALPHA_CONTACT_RAD_S2) {  // hand contact: reject whole coast
      fitLifecycle.completeAttempt(false, true);
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
  if (!pwValidFriction({fitC, fitB, 0}, FRICTION_BOUNDS)) {
    Serial.printf("SPIN#%lu FRICTION_REJECT bounds fitC=%.4f fitB=%.4f pairs=%d reason=%s\n",
                  (unsigned long)spin.number, fitC, fitB, pairs, reason);
    PW_S3_COUNT_REJECT();
    return;
  }
  fitLifecycle.completeAttempt(true); // each clean coast can update the model at most once
  float keep = 1.0f - FIT_BLEND;
  if (fitDir > 0) {
    cw_c = keep * cw_c + FIT_BLEND * fitC;
    cw_b = keep * cw_b + FIT_BLEND * fitB;
    ++cwFitCount;
    if (cwFitCount >= FIT_PERSIST_MIN_FITS) {
      if (!pwSaveFrictionDirection(preferences, true, {cw_c, cw_b, cwFitCount}))
        Serial.println(F("# WARN: cw friction persistence failed"));
    }
    Serial.printf("SPIN#%lu FRICTION dir=+1 fitC=%.4f fitB=%.4f pairs=%d -> c=%.4f b=%.4f fits=%u\n",
                  (unsigned long)spin.number, fitC, fitB, pairs, cw_c, cw_b, cwFitCount);
  } else {
    ccw_c = keep * ccw_c + FIT_BLEND * fitC;
    ccw_b = keep * ccw_b + FIT_BLEND * fitB;
    ++ccwFitCount;
    if (ccwFitCount >= FIT_PERSIST_MIN_FITS) {
      if (!pwSaveFrictionDirection(preferences, false, {ccw_c, ccw_b, ccwFitCount}))
        Serial.println(F("# WARN: ccw friction persistence failed"));
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

TargetChoice chooseSafeTarget(int dir, float curAngle, float speedRevS) {
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
  const float profileMin = latencyDeg + pwBrakeDistanceDeg(
      fminf(TRAIL_FRACTION * speedRevS, CAPTURE_MAX_CMD_REV_S),
      (float)DECEL_CEILING_SPS2 / WHEEL_USTEPS_PER_REV);
  winMin = fmaxf(winMin, profileMin);
  float assistMin = latencyDeg
                  + brakedStopDistanceDeg(speedRevS, dir, motorExtraRadS2(ASSIST_DECEL_MAX_SPS2))
                  + 2.0f;
  assistMin = fmaxf(assistMin, latencyDeg + pwBrakeDistanceDeg(
      fminf(TRAIL_FRACTION * speedRevS, CAPTURE_MAX_CMD_REV_S),
      (float)ASSIST_DECEL_MAX_SPS2 / WHEEL_USTEPS_PER_REV));
  float shadowMin = latencyDeg + pwBrakeDistanceDeg(
      fminf(TRAIL_FRACTION * speedRevS, CAPTURE_MAX_CMD_REV_S),
      (float)SHADOW_DECEL_MAX_SPS2 / WHEEL_USTEPS_PER_REV);

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
  if (!out.found && naturalDeg > 10.0f && naturalDeg - 2.0f >= shadowMin) {
    float natAng = fmodf(curAngle + (float)dir * naturalDeg, 360.0f);
    if (natAng < 0.0f) natAng += 360.0f;
    float settleAng = fmodf(curAngle + (float)dir * (naturalDeg - 4.0f), 360.0f);
    if (settleAng < 0.0f) settleAng += 360.0f;
    if (!isDare(wedgeAtAngle(natAng)) && !isDare(wedgeAtAngle(settleAng)) &&
        dareDistanceDeg(natAng) >= DARE_PROXIMITY_FAULT_DEG + 3.0f &&
        dareDistanceDeg(settleAng) >= DARE_PROXIMITY_FAULT_DEG + 3.0f) {
      out.found = true;
      out.wedge = wedgeAtAngle(settleAng);
      out.runwayDeg = naturalDeg - 6.0f;   // 2026-09-22: 2 deg was inside prediction noise (plan refused)
      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;
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
  const float profileMin = latencyDeg + pwBrakeDistanceDeg(
      fminf(TRAIL_FRACTION * speedRevS, CAPTURE_MAX_CMD_REV_S),
      (float)DECEL_CEILING_SPS2 / WHEEL_USTEPS_PER_REV);
  winMin = fmaxf(winMin, profileMin);
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
    case ST_CAPTURE_ARMING: return "CAPTURE_ARMING";
    case ST_RECOVERY_NUDGE: return "RECOVERY_NUDGE";
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
    case FC_TRACKING_LOST: return "TRACKING_LOST";
    case FC_SELFSPIN_ABORT: return "SELFSPIN_ABORT";
    case FC_UNKNOWN_PERSISTED: return "UNKNOWN_PERSISTED";
    case FC_CONTROL_OVERSPEED: return "CONTROL_OVERSPEED";
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
  // Only the normal kinematic confirmation after a completed HOLD can use
  // its one-use new-spin permission. Guest re-push/reversal/fault records
  // cannot reset the physical attempt budget.
  const bool confirmedHandSpin = (state == ST_MOTION_CANDIDATE ||
      state == ST_MANUAL_ADJUSTMENT) && encoderMotionReady();
  captureCycle.confirmHandSpinAfterHold(confirmedHandSpin,
      faultCode == FC_NONE && currentStage == CS_FREEWHEEL,
      digitalRead(PIN_EN) == HIGH, stepper && !stepper->isRunning());
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
  fitLifecycle.discard();  // sampling begins at hand release, not during the push
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
      "final=%.1f finalW=%d err=%.1f quality=%u result=%s fault=%s%s "
      "captureAttempted=%d captureEnergized=%d captureAbandoned=%d\n",
      (unsigned long)spin.number, spin.dir, spin.peakRevS,
      (unsigned long)(spin.releaseMs ? spin.releaseMs - spin.pushStartMs : 0),
      spin.releaseAngleDeg, spin.releaseRevS,
      spin.targetWedge, spin.targetAngleDeg, spin.runwayDeg,
      spin.naturalStopDeg, spin.fricC, spin.fricB,
      spin.takeoverRevS, spin.cmdMaxRevS, spin.cmdMinRevS,
      spin.maxSpeedRiseRevS, spin.maxDecelRevS2,
      spin.finalAngleDeg, spin.finalWedge, spin.targetErrDeg,
      spin.targetQuality, resultName(result), faultName(spin.fault), fitSuffix,
      captureCycle.attempted(), captureCycle.energized(), captureCycle.abandoned());
  if (isDare(spin.finalWedge)) {
    Serial.printf("SPIN#%lu LANDED-DARE wedge=%d THIS IS A FAILURE result=%s fault=%s\n",
                  (unsigned long)spin.number, spin.finalWedge,
                  resultName(result), faultName(spin.fault));
  }
}

/* ========================================================================== */
/*                              FAULT PATH                                    */
/* ========================================================================== */
void restoreFaultLatch() {
#if PW_S1_ENABLE
  uint8_t raw = preferencesAvailable ? preferences.getUChar(PW_S1_NVS_KEY, 0) : 255;
  recoveryGuardRaw = preferencesAvailable ? preferences.getUChar(PW_RECOVERY_GUARD_KEY, 0) : 255;
  uint8_t restored = pwRestoreFaultWithRecoveryGuard(raw, recoveryGuardRaw,
      (uint8_t)FC_SELFSPIN_ABORT);
  PwStoredFault stored = pwDecodeStoredFault(restored, preferencesAvailable,
      (uint8_t)FC_CONTROL_OVERSPEED, (uint8_t)FC_UNKNOWN_PERSISTED);
  persistedFaultRaw = stored.raw;
  if (recoveryGuardRaw != 0) {
    Serial.printf("# RECOVERY_GUARD primary=%u guard=%u; interrupted recovery, outputs locked; diagnostic recovery required\n",
                  (unsigned)raw, (unsigned)recoveryGuardRaw);
  }
  if (stored.locked) {
    faultCode = (FaultCode)stored.code;
    state = ST_FAULT_LATCHED;
    stateEnteredMs = millis();
    Serial.printf("# S1: saved fault=%s raw=%u restored; outputs disabled%s\n",
                  faultName(faultCode), (unsigned)persistedFaultRaw,
                  stored.unknown ? "; unknown ID/storage unavailable: r disabled" : "");
  }
#endif
}

bool pwPersistFaultLatch(uint8_t code) {
  if (!preferencesAvailable) return false;
  uint8_t stored = preferences.getUChar(PW_S1_NVS_KEY, 0);
  uint8_t keep = pwFirstFaultByte(stored, code);
  bool saved = stored == keep || preferences.putUChar(PW_S1_NVS_KEY, keep) == sizeof(uint8_t);
  saved = saved && preferences.getUChar(PW_S1_NVS_KEY, 0) == keep;
  persistedFaultRaw = keep;
  if (!saved) Serial.println(F("# WARN: first fault could not be persisted"));
  return saved;
}

bool pwClearFaultLatch() {
  PwStoredFault known = pwDecodeStoredFault(persistedFaultRaw, preferencesAvailable,
      (uint8_t)FC_CONTROL_OVERSPEED, (uint8_t)FC_UNKNOWN_PERSISTED);
  if (!preferencesAvailable) return false;
  uint8_t reread = preferences.getUChar(PW_S1_NVS_KEY, 0);
  uint8_t guardReread = preferences.getUChar(PW_RECOVERY_GUARD_KEY, recoveryGuardRaw);
  if (recoveryGuardRaw != 0 || !pwMayClearStoredFault(known, reread, guardReread)) return false;
  if (reread != 0 &&
      (preferences.putUChar(PW_S1_NVS_KEY, 0) != sizeof(uint8_t) ||
       preferences.getUChar(PW_S1_NVS_KEY, 255) != 0)) return false;
  persistedFaultRaw = 0;
  return true;
}

void enterFault(FaultCode code, const char* detail) {
  // First remove torque; queued pulses may drain only with EN high. Never
  // perform serial or NVS work while a fault leaves the motor energized.
  digitalWrite(PIN_EN, HIGH);
  float faultCommand = cmdRevS;
  driverFreewheel();
  captureCycle.fault();
  fitLifecycle.discard();
  if (state == ST_FAULT_LATCHED) return;
  faultCode = code;
  PW_S1_PERSIST(code);  // S1: latch survives a power cycle; only r clears
  Serial.printf("FAULT code=%s detail=%s state=%s angle=%.1f wedge=%d omega=%.3f cmd=%.3f\n",
                faultName(code), detail, stateName(state), wheelAngleDeg(),
                currentWedge(), omega, faultCommand);
  // An open spin is NOT closed here: its summary must record the real resting
  // wedge, so ST_FAULT_LATCHED closes it once the wheel is actually still
  // (also prevents the residual coast from being re-counted as a new spin).
  spinOpenedDuringFault = false;
  state = ST_FAULT_LATCHED;
  stateEnteredMs = millis();
  settleStillSinceMs = 0;
}

void serviceFault() {
  // Outputs are already disabled. Wait for the dead queue to drain.
  digitalWrite(PIN_EN, HIGH);
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
  return edgeMargin >= 0.3f * WEDGE_DEG;   // 6 deg at 18 wedges (was 11 with 30 deg wedges)
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
  // An explicit attended probe can recover DIR_CAL only. Verify the stored
  // clear before enabling, so a write failure cannot leave a powered probe.
  if (!PW_S1_CLEAR()) {
    Serial.println(F("# DIR PROBE refused: saved fault could not be cleared"));
    return;
  }
  faultCode = FC_NONE;
  state = ST_IDLE_STOPPED;
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
  state = ST_DIR_PROBE;
  stateEnteredMs = millis();
  Serial.printf("# DIR PROBE leg 1/2: moving FAS+ %ld usteps; keep hands clear\n",
                (long)DIR_PROBE_USTEPS);
}

// Energized step from a resting dare into the adjacent safe wedge centre,
// downhill (gravity direction) when that neighbour is safe, else the other
// side. Returns false (nothing energized) when it cannot be done safely.
bool startDareRecovery(const char* why) {
  if (!stepper || !encoderPositionFresh() || stepper->isRunning()) return false;
  if (encoderVelocityValid && fabsf(omega) > 0.05f) return false;
  float angle = wheelAngleDeg();
  int wedge = currentWedge();
  float g = wheelGravity.forwardAccel(angle * DEG_TO_RAD, 1);
  int dir = (fabsf(g) < 0.08f) ? (spinDir != 0 ? spinDir : 1) : (g > 0.0f ? 1 : -1);
  int wt = wedge + dir;
  if (isDare(wt)) { dir = -dir; wt = wedge + dir; }
  if (isDare(wt)) return false;
  wt %= NUM_WEDGES; if (wt < 0) wt += NUM_WEDGES;
  float center = wt * WEDGE_DEG + 0.5f * WEDGE_DEG;
  float dist = forwardDistanceDeg(dir, angle, center);
  if (dist < 3.0f || dist > 2.0f * WEDGE_DEG) return false;
  int fasSign = fasSignForEncoderDirection(dir);
  if (!fasSign) return false;
  int32_t steps = (int32_t)lroundf(dist * WHEEL_USTEPS_PER_REV / 360.0f);
  bool wasEnergized = digitalRead(PIN_EN) == LOW;
  if (!wasEnergized) setCurrentStage(CS_PRECHARGE);
  driver.rms_current(RECOVERY_CURRENT_MA, 1.0);
  g_currentMa = RECOVERY_CURRENT_MA;
  stepper->setDirectionPin(PIN_DIR, fasSign > 0 ? INVERT_DIR : !INVERT_DIR);
  stepper->setCurrentPosition(0);
  stepper->setJumpStart(0);
  if (!fasSetSpeedHz(RECOVERY_SPEED_HZ) || !fasSetAcceleration(RECOVERY_ACCEL_SPS2) ||
      stepper->move(steps) != MoveResultCode::OK) {
    if (!wasEnergized) driverFreewheel();
    return false;
  }
  ++dareRecoveryAttempts;
  recoveryStillSinceMs = 0;
  state = ST_RECOVERY_NUDGE;
  stateEnteredMs = millis();
  Serial.printf("# DARE_RECOVERY (%s) attempt=%u from wedge %d angle=%.1f -> wedge %d dir=%+d %.1f deg\n",
                why, (unsigned)dareRecoveryAttempts, wedge, angle, wt, dir, dist);
  return true;
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
  Serial.printf("# DIR PROBE PASS: FAS+ is encoder dir=%+d, retrace err=%.2f deg; takeover=%d\n",
                motorPositiveEncoderSign, returnErrDeg, takeoverEnabled ? 1 : 0);
}

/* ========================================================================== */
/*                       CONTROL: RESERVE / CAPTURE / BRAKE                   */
/* ========================================================================== */
bool controlAvailable() {
  return faultCode == FC_NONE && takeoverEnabled && motorDirectionCalibrated && tmcOk &&
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

// Gravity beats friction 3:1 on this wheel. Braking on the DOWNHILL half means
// the motor must fight gravity plus the plan (pole slip / overspeed, 17:52 fault);
// on the UPHILL half gravity is the brake and the motor only shapes it. The
// whole brake arc [now, now + runway] must sit inside the uphill half
// (rest -> crest in the direction of travel), with margin at both ends.
const float UPHILL_MARGIN_DEG = 10.0f;
bool brakeArcUphill(int dir, float curAngle, float runwayDeg) {
  if (!wheelGravity.active()) return true;
  float s0 = fmodf((float)dir * (curAngle - wheelGravity.restAngleDeg()), 360.0f);
  if (s0 < 0.0f) s0 += 360.0f;
  return s0 >= UPHILL_MARGIN_DEG && s0 + runwayDeg <= 180.0f - UPHILL_MARGIN_DEG;
}

bool tryReserveTarget(uint32_t nowMs) {
  if (!encoderMotionReady() || !captureTimerReady || !captureCycle.available()) return false;
  if (stepper && stepper->isRunning()) return false;  // stale queue must die first
  // FORWARD speed in the latched spin direction: a wheel moving the other way
  // must never reserve for the stale direction (the caller handles reversal).
  float speed = omega * (float)spinDir;
  if (!isfinite(speed) || speed < 0.02f || speed > CAPTURE_MAX_WHEEL_REV_S) return false;

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
  finishFrictionCapture("reserve", PwFitEnd::Preview);

  TargetChoice choice = chooseSafeTarget(spinDir, wheelAngleDeg(), speed);
  if (!choice.found) return false;
  if (!brakeArcUphill(spinDir, wheelAngleDeg(), choice.runwayDeg)) return false;  // try again next tick
  // Reject short runway before reserving. Accepted pickup has one attempt,
  // including any arming failure, until a later qualified physical rest.
  if (choice.runwayDeg < MIN_RESERVE_RUNWAY_DEG) return false;

  // Invariant: a dare can never be reserved.
  if (isDare(choice.wedge)) {
    enterFault(FC_TARGET_INVARIANT, "chose dare wedge");
    return false;
  }

  takeoverDir = spinDir;
  dareRecoveryAttempts = 0;
  reserveCounts = encoderCountsMT;
  targetCountsMT = encoderCountsMT + takeoverDir * countsForDegrees(choice.runwayDeg);
  planDecelCapSps2 = choice.decelCapSps2;
  reserveMs = nowMs;

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

// Leave the chosen absolute prize unchanged. An unavailable plan is an honest
// unpowered miss; no retry or prize reselection until qualified physical rest.
void abandonCapture(const char* reason) {
  driverFreewheel(); // EN high and lease revoked before logging
  captureCycle.abandon();
  captureArm.verified = false;
  fitLifecycle.discard();
  releaseCompRevS = 0.0f;  // the motor may have changed the energy: legacy re-push test
  if (faultCode == FC_NONE) {
    state = ST_SPIN_RELEASED;
    stateEnteredMs = millis();
  }
  Serial.printf("# CAPTURE_ABANDON reason=%s attempted=%d energized=%d; no retry until fresh rest\n",
      reason, captureCycle.attempted(), captureCycle.energized());
}

bool captureDriverHealthy() {
  const uint32_t drv = driver.DRV_STATUS();
  const uint8_t gst = (uint8_t)driver.GSTAT();
  return drv != 0 && drv != 0xFFFFFFFFUL && !(drv & 0x1E000000UL) && gst == 0 &&
         driver.version() == 0x30 && driver.microsteps() == MICROSTEPS;
}

// Called initially and exactly once again after pulse verification. It never
// changes targetCountsMT, selects another wedge, resets STEP or enables EN.
bool prepareCapturePlan(uint32_t hz, float forward) {
  const float remaining = remainingTargetDeg();
  if (!isfinite(forward) || forward < 0.02f || forward > CAPTURE_MAX_WHEEL_REV_S ||
      hz < 40 || hz > 2400 || !isfinite(remaining) || remaining < 7.0f ||
      isDare(spin.targetWedge)) return false;
  // Preserve production's brake-reachable policy even if the inherited model
  // is pessimistic. Do not use the diagnostic's distant gentle-test target.
  const float naturalRemaining = naturalStopDistanceDeg(forward, takeoverDir);
  if (!isfinite(naturalRemaining) || remaining > naturalRemaining * 1.03f + 3.0f) return false;
  const PwBrakePlan plan = pwPlanBrake(hz, remaining, WHEEL_USTEPS_PER_REV, planDecelCapSps2);
  if (!plan.feasible || (spin.targetQuality == 0 &&
      plan.decelRevS2 > naturalDecelRevS2(forward, takeoverDir))) return false;
  planDecelRevS2 = plan.decelRevS2;
  return fasSetAcceleration(plan.accelerationSps2);
}

bool captureEnableOutput() {
  // In pinned FAS 1.2.7 with ordinary GPIO7 this is GPIO-only. The lease holds
  // the same lock as its cutoff callback around this EN edge.
  return stepper && stepper->enableOutputs();
}

bool launchCapture(uint32_t nowMs) {
  if (!controlAvailable() || !captureCycle.beginAttempt()) {
    abandonCapture("capture ownership unavailable"); return false;
  }
  if (!captureLease.arm()) {
    abandonCapture("capture lease unavailable"); return false;
  }
  const int fasSign = fasSignForEncoderDirection(takeoverDir);
  const float forward = omega * (float)takeoverDir;
  if (!fasSign || !stepper || stepper->isRunning() ||
      digitalRead(PIN_EN) != HIGH || !encoderMotionReady() ||
      !isfinite(forward) || forward < 0.02f || forward > CAPTURE_MAX_WHEEL_REV_S ||
      !captureDriverHealthy()) {
    abandonCapture("capture arming prerequisites"); return false;
  }
  captureEntryHz = (uint32_t)floorf(fminf(TRAIL_FRACTION * forward,
      CAPTURE_MAX_CMD_REV_S) * WHEEL_USTEPS_PER_REV);
  if (!prepareCapturePlan(captureEntryHz, forward) || !fasSetSpeedHz(captureEntryHz)) {
    abandonCapture("initial fixed-target runway/plan infeasible"); return false;
  }
  const uint32_t acceleration = stepper->getAcceleration();
  if (!acceleration) { abandonCapture("zero capture acceleration"); return false; }
  const uint32_t jumpStep = (uint32_t)lroundf((float)captureEntryHz * captureEntryHz /
      (2.0f * acceleration));
  stepper->setJumpStart(jumpStep);
  stepper->setCurrentPosition(0); // outputs off and queue empty above
  captureDirHigh = fasSign > 0 ? INVERT_DIR : !INVERT_DIR;
  stepper->setDirectionPin(PIN_DIR, captureDirHigh);
  // Close clean fitting before register/STEP preparation; no powered sample
  // can enter this fit even if a later state is reclassified as a free coast.
  finishFrictionCapture("power", PwFitEnd::Powered);
  state = ST_CAPTURE_ARMING;
  stateEnteredMs = nowMs;
  setCurrentStage(CS_CAPTURE_PREPARED);
  if (captureLease.expired() || !captureArm.begin(micros(), stepper->getCurrentPosition(),
      captureEntryHz, captureDirHigh) || digitalRead(PIN_EN) != HIGH || !fasRun(1)) {
    abandonCapture("pulse-first launch rejected"); return false;
  }
  cmdRevS = (float)captureEntryHz / WHEEL_USTEPS_PER_REV;
  lastAppliedHz = captureEntryHz;
  return true;
}

void serviceCaptureArming(uint32_t nowMs) {
  (void)nowMs;
  if (!controlAvailable() || !stepper || !captureCycle.attempted() || captureCycle.energized() ||
      currentStage != CS_CAPTURE_PREPARED || captureLease.expired()) {
    abandonCapture("capture arming interlock/deadline"); return;
  }
  float forward = omega * (float)takeoverDir;
  PwCaptureArm::Verdict decision = captureArm.update(micros(), stepper->getCurrentPosition(),
      digitalRead(PIN_EN) == HIGH, digitalRead(PIN_DIR) == HIGH,
      encoderMotionReady(), forward, WHEEL_USTEPS_PER_REV, TRAIL_FRACTION);
  if (decision == PwCaptureArm::WAIT) return;
  if (decision == PwCaptureArm::REJECT) {
    abandonCapture("pulse-first observation rejected");
    Serial.printf("# PULSEFIRST reject=%u hz=%.1f requested=%lu\n",
        (unsigned)captureArm.reason, captureArm.pulseHz, (unsigned long)captureEntryHz);
    return;
  }
  if (!stepper->isRunning() || !captureDriverHealthy() ||
      !prepareCapturePlan(captureEntryHz, forward)) {
    abandonCapture("final fixed-target health/runway/plan infeasible"); return;
  }
  stepper->applySpeedAcceleration();
  // Register traffic may age encoder evidence. Only a fresh synchronous proof
  // can authorize EN, under the independent 150 ms lease.
  forward = omega * (float)takeoverDir;
  decision = captureArm.update(micros(), stepper->getCurrentPosition(),
      digitalRead(PIN_EN) == HIGH, digitalRead(PIN_DIR) == HIGH,
      encoderMotionReady(), forward, WHEEL_USTEPS_PER_REV, TRAIL_FRACTION);
  const bool validProof = decision == PwCaptureArm::READY && controlAvailable() &&
      stepper->isRunning() && digitalRead(PIN_EN) == HIGH &&
      (digitalRead(PIN_DIR) == HIGH) == captureArm.expectedDirHigh;
  if (!captureLease.enable(validProof, captureArm.verifiedUs, captureEnableOutput) ||
      digitalRead(PIN_EN) != LOW) {
    abandonCapture("capture torque-on interlock"); return;
  }
  captureCycle.markEnergized();
  captureArm.verified = false;
  currentStage = CS_CAPTURE; // registers already contain 2200; no traffic at EN edge
  const uint32_t torqueOnMs = millis();
  lastPoweredHealthMs = torqueOnMs; // health was verified immediately before EN
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
  Serial.printf("# PULSEFIRST TORQUE_ON arm_us=%lu pulses=%ld hz=%.1f requested=%lu current=%u "
                "EN=%d DIR=%d targetW=%d remain=%.1f a=%lu phase_unverified=1\n",
      (unsigned long)(micros() - captureArm.startUs),
      (long)(stepper->getCurrentPosition() - captureArm.startPosition), captureArm.pulseHz,
      (unsigned long)captureEntryHz, g_currentMa, digitalRead(PIN_EN), digitalRead(PIN_DIR),
      spin.targetWedge, remainingTargetDeg(), (unsigned long)stepper->getAcceleration());
}

// Match the diagnostic's powered driver-health cadence. No GSTAT acknowledgement
// or automatic fault recovery. The legacy TMC_UART fault label also covers
// this board's SPI driver/status checks; enterFault removes torque first.
bool controlDriverSafe(uint32_t nowMs) {
  if (uint32_t(nowMs - lastPoweredHealthMs) < 100) return true;
  lastPoweredHealthMs = nowMs;
  if (!captureDriverHealthy()) {
    enterFault(FC_TMC_UART, "powered SPI driver health/config/status failed");
    return false;
  }
  return true;
}

// Immediate absolute ceiling from the bounded diagnostic, before debounced
// speed-up/fight monitors. HOLD keeps its separate guest-release interaction.
bool controlSpeedSafe() {
  if (encoderMotionReady() && pwControlOverspeed(omega)) {
    enterFault(FC_CONTROL_OVERSPEED, "absolute speed above 0.30 rps in powered control");
    return false;
  }
  return true;
}

// Shared safety monitors for the powered states.  Returns false if the state
// was changed (fault or release) and the caller must stop processing.
bool controlSafetyChecks(uint32_t nowMs) {
  if (!controlSpeedSafe() || !controlDriverSafe(nowMs)) return false;
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

  if (isDare(wedge) || dareDistanceDeg(angle) < DARE_PROXIMITY_FAULT_DEG) {
    if (dareRecoveryAttempts < 2 && startDareRecovery("after control")) return;
  }
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
  captureCycle.markSuccessfulLanding(faultCode == FC_NONE &&
      state == ST_LANDING_SETTLE && encoderMotionReady() && fabsf(omega) <= STILL_REV_S &&
      settleStillSinceMs != 0 && millis() - settleStillSinceMs >= SETTLE_MS &&
      fabsf(degreesForCounts(encoderCountsMT - settleWindowCounts)) <= 1.5f &&
      stepper && !stepper->isRunning());
  closeSpin(result);
  holdAnchorValid = false; holdDeflectSinceMs = 0;
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
  Serial.println(F("# build: imbalance-model-20260921 PARTY; based on plywood-capture-review-20260920; takeover defaults OFF"));
  Serial.println(F(
    "\n=== PRIZE WHEEL (correctness redesign) ===\n"
    " z  set current raw as wedge-0 anchor (wheel at rest, pointer on 17|0 line)\n"
    " p  attended two-leg direction probe (safe wedge center only)\n"
    " s  status\n"
    " d  arm high-rate RAM capture (dumps after true stop/fault)\n"
    " v  toggle live control logs\n"
    " e  toggle automatic takeover\n"
    " f  print friction model\n"
    " F  reset friction model to seeds\n"
    " D  print driver registers (read-only)\n"
    " g  print imbalance + encoder INL model\n"
    " G<g>,<phiDeg>,<a1>,<b1>,<a2>,<b2>+Enter  set it (at rest; all zero clears)\n"
    " x  clear stored direction calibration\n"
    " r  fault reset (re-checks TMC UART)\n"
    " m  print dare mask\n"
    " ?  help"));
  pwPartyHelpLines();
}

void printDriver() {
  // Read-only. GSTAT is clear-on-read: a flag that comes back on the next
  // print is being re-asserted by hardware (uv_cp = VM missing/low).
  uint32_t drv = driver.DRV_STATUS();
  uint8_t gst = (uint8_t)driver.GSTAT();
  Serial.printf("# drv DRV_STATUS=%08lX GSTAT=%02X IOIN=%08lX version=%02X usteps=%u healthy=%d\n",
      (unsigned long)drv, (unsigned)gst, (unsigned long)driver.IOIN(), (unsigned)driver.version(),
      (unsigned)driver.microsteps(),
      (drv != 0 && drv != 0xFFFFFFFFUL && !(drv & 0x1E000000UL) && gst == 0 &&
       driver.version() == 0x30 && driver.microsteps() == MICROSTEPS) ? 1 : 0);
}

void printStatus() {
  uint32_t ageUs = encoderPrimed ? (uint32_t)(micros() - lastGoodUs) : 0;
  Serial.printf("# state=%s fault=%s angle=%.2f wedge=%d omega=%.4f natStop=%.1f "
                "pos=%s vel=%s age_us=%lu dirCal=%d(sign %+d) tmc=%d takeover=%d "
                "rawZero=%u stage=%u cmd=%.3f savedFaultRaw=%u\n",
                stateName(state), faultName(faultCode), wheelAngleDeg(),
                currentWedge(), omega,
                encoderVelocityValid
                    ? naturalStopDistanceDeg(fabsf(omega), omega >= 0 ? 1 : -1)
                    : 0.0f,
                encoderPositionFresh() ? "FRESH" : "STALE",
                encoderVelocityValid ? "VALID" : "REPRIME",
                (unsigned long)ageUs, motorDirectionCalibrated ? 1 : 0,
                motorPositiveEncoderSign, tmcOk ? 1 : 0, takeoverEnabled ? 1 : 0,
                rawZero, (unsigned)currentStage, cmdRevS, (unsigned)persistedFaultRaw);
}

// WIFI_TASK: one shared single-char parser for the serial console AND the
// telnet clients.  Existing guards (z only while IDLE, etc.) apply to both.
void printImbalance() {
  const PwGravityModel& gm = wheelGravity.model();
  const PwInlModel& im = encoderInl.model();
  Serial.printf("# imbalance stored=%d g=%.4f rad/s2 phi=%.2f deg rest=%.1f deg | "
                "inl a1=%.2f b1=%.2f a2=%.2f b2=%.2f counts | raw=%u inl_now=%.2f\n",
                imbalanceLoaded ? 1 : 0, gm.g, gm.phiRad * RAD_TO_DEG,
                wheelGravity.active() ? wheelGravity.restAngleDeg() : 0.0f,
                im.a1, im.b1, im.a2, im.b2, (unsigned)lastGoodRaw,
                encoderInl.errorCounts(lastGoodRaw));
}

// Only reached at rest in IDLE/FAULT (the `G` gate), with a validated config.
void applyImbalanceConfig(const PwImbalanceConfig& config) {
  bool clear = config.gravity.g == 0.0f && config.inl.a1 == 0.0f && config.inl.b1 == 0.0f &&
      config.inl.a2 == 0.0f && config.inl.b2 == 0.0f;
  bool saved = preferencesAvailable &&
      (clear ? pwClearImbalance(preferences) : pwSaveImbalance(preferences, config));
  encoderInl.set(config.inl);
  wheelGravity.set(config.gravity);
  imbalanceLoaded = saved && !clear;
  if (encoderPrimed) primeEncoder(lastGoodRaw, micros(), true);
  Serial.printf("# imbalance model %s; persisted=%d\n", clear ? "cleared" : "set", saved ? 1 : 0);
  printImbalance();
}

void handleSerial() {
  if (!Serial.available()) return;
  char command = (char)Serial.read();
  if (imbalanceLine.open()) {
    PwImbalanceConfig parsed;
    bool consumed = false, rejected = false;
    bool complete = imbalanceLine.feed(command, millis(), &parsed, &consumed, &rejected);
    if (rejected) Serial.println(F("# G rejected: need six finite in-range numbers; nothing changed"));
    if (complete) applyImbalanceConfig(parsed);
    if (consumed) return;
  }
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
      if (preferencesAvailable) preferences.putBool("takeover", takeoverEnabled);
      Serial.printf("# takeoverEnabled=%d (persisted)\n", takeoverEnabled ? 1 : 0);
      break;
    case 'f':
      Serial.printf("# friction cw: c=%.4f b=%.4f fits=%u | ccw: c=%.4f b=%.4f fits=%u\n",
                    cw_c, cw_b, cwFitCount, ccw_c, ccw_b, ccwFitCount);
      break;
    case 'g':
      printImbalance();
      break;
    case 'G':
      if (state != ST_IDLE_STOPPED && state != ST_FAULT_LATCHED)
        Serial.println(F("# G ignored: controller busy"));
      else if (encoderVelocityValid && fabsf(omega) > STILL_REV_S)
        Serial.println(F("# G ignored: wheel must be at rest"));
      else imbalanceLine.begin(millis());
      break;
    case 'D':
      printDriver();
      break;
    case 'F':
      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {
        cw_c = FRICTION_SEED.c; cw_b = FRICTION_SEED.b;
        ccw_c = FRICTION_SEED.c; ccw_b = FRICTION_SEED.b;
        cwFitCount = 0; ccwFitCount = 0;
        bool savedCw = preferencesAvailable && pwSaveFrictionDirection(preferences, true, FRICTION_SEED);
        bool savedCcw = preferencesAvailable && pwSaveFrictionDirection(preferences, false, FRICTION_SEED);
        bool saved = savedCw && savedCcw && pwSaveFrictionVersion(preferences);
        Serial.printf("# friction model reset to v2 seeds; persisted=%d\n", saved ? 1 : 0);
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
        if (recoveryGuardRaw != 0) {
          Serial.printf("# r refused: recovery guard=%u; finish the diagnostic recovery procedure\n",
                        (unsigned)recoveryGuardRaw);
          break;
        }
        if (faultCode == FC_UNKNOWN_PERSISTED || !preferencesAvailable) {
          Serial.printf("# r refused: unknown saved fault raw=%u; use firmware that understands it\n",
                        (unsigned)persistedFaultRaw);
          break;
        }
        if (!encoderMotionReady() || fabsf(omega) > STILL_REV_S) {
          Serial.println(F("# r ignored: fresh, valid stopped-wheel feedback required"));
          break;
        }
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
        if (!PW_S1_CLEAR()) {
          Serial.println(F("# r: saved fault clear failed; fault remains latched"));
          break;
        }
        faultCode = FC_NONE;
        state = ST_IDLE_STOPPED;
        stateEnteredMs = millis();
        Serial.printf("# fault cleared; tmc=%d dirCal=%d%s\n",
                      tmcOk ? 1 : 0, motorDirectionCalibrated ? 1 : 0,
                      motorDirectionCalibrated ? "" : " (run p before guest use)");
      } else Serial.println(F("# r: no latched fault"));
      break;
    case 'm':
      Serial.printf("# dare_mask=0x%05lX; dare indices 2 7 12 15 = labels 3 8 13 16 (label = index+1)\n", (unsigned long)dare_mask);
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
  digitalWrite(PIN_EN, HIGH); // no boot hold; GPIO initialized before Arduino digitalWrite
  Serial.begin(115200);
  delay(300);
  randomSeed(esp_random());

  preferencesAvailable = preferences.begin("prizewheel", false);
  restoreFaultLatch(); // first: never overwrite an existing fault during boot checks
  rawZero = preferences.getUShort("rawZero", rawZero);  // label-true anchor
  motorPositiveEncoderSign = preferences.getInt("pos_sign", 0);
  motorDirectionCalibrated = preferences.getBool("dir_ok", false) &&
      (motorPositiveEncoderSign == 1 || motorPositiveEncoderSign == -1);
  if (preferencesAvailable) {
    PwFrictionLoad model = pwLoadFriction(preferences, FRICTION_SEED, FRICTION_BOUNDS);
    cw_c = model.cw.c; cw_b = model.cw.b; cwFitCount = model.cw.fits;
    ccw_c = model.ccw.c; ccw_b = model.ccw.b; ccwFitCount = model.ccw.fits;
    Serial.printf("# friction schema=%u migrated=%d resetCW=%d resetCCW=%d persisted=%d\n",
        (unsigned)PW_FRICTION_VERSION, model.migrated, model.resetCw, model.resetCcw, model.saved);
  }
  if (preferencesAvailable) {
    // Party 2026-09-22: a power blip must not silently disarm the wheel.
    takeoverEnabled = preferences.getBool("takeover", false);
    Serial.printf("# takeoverEnabled=%d (from NVS)\n", takeoverEnabled ? 1 : 0);
    PwImbalanceConfig imbalance = pwLoadImbalance(preferences, &imbalanceLoaded);
    encoderInl.set(imbalance.inl);
    wheelGravity.set(imbalance.gravity);
    printImbalance();
  }
  // rawZero/dir_ok/pos_sign are intentionally untouched: these legacy keys
  // have no hardware generation and still require physical verification.

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

  driverConfig();   // TMC5160T Pro over SPI - no serial begin needed

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP, DRIVER_MCPWM_PCNT);  /* S3: keep FAS off RMT so FastLED owns it (see S3_PORT.md) */
  if (stepper) {
    stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
    stepper->setEnablePin(PIN_EN, true);
    stepper->setAutoEnable(false);
    if (!pwFixStepperClock()) {
      enterFault(FC_STEPPER_API, "STEP clock configuration unsupported");
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

  captureTimerReady = captureLease.begin((gpio_num_t)PIN_EN);
  if (!captureTimerReady) enterFault(FC_STEPPER_API, "capture timer unavailable");

  // Party additions: DFPlayer, LED task, SoftAP. S1 was restored before boot checks.
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
  captureCycle.observe(nowMs, state == ST_IDLE_STOPPED && !spinOpen &&
      faultCode == FC_NONE && encoderMotionReady() && fabsf(omega) <= STILL_REV_S &&
      currentStage == CS_FREEWHEEL && digitalRead(PIN_EN) == HIGH &&
      stepper && !stepper->isRunning(), encoderCountsMT, countsForDegrees(1.0f));

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
        releaseCompRevS = wheelGravity.compensatedRevS(speed, wheelAngleDeg() * DEG_TO_RAD);
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
          finishFrictionCapture("reversed", PwFitEnd::Reversal);
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
      // A free coast can out-run its own peak on the heavy side coming down, but
      // it can never gain mechanical energy: require both before calling it a push.
      bool energyRose = !wheelGravity.active() || releaseCompRevS <= 0.0f ||
          wheelGravity.compensatedRevS(speed, wheelAngleDeg() * DEG_TO_RAD) >
              releaseCompRevS * 1.02f;
      if (speed > spin.peakRevS * 1.02f && speed > SPIN_DETECT_REV_S && energyRose) {
        spin.peakRevS = speed;
        lastPeakMs = nowMs;
        finishFrictionCapture("re-push", PwFitEnd::Repush);
        state = ST_SPIN_PUSH;
        stateEnteredMs = nowMs;
        break;
      }
      if (contactDetected(nowMs)) {
        spin.hadContact = 1;  // sticky for the close
        finishFrictionCapture("contact", PwFitEnd::Contact);
      }
      captureFrictionSample(nowMs);

      if (controlAvailable()) {
        if (tryReserveTarget(nowMs)) break;
        if (state != ST_SPIN_RELEASED) break;  // reservation may have faulted
      }

      // Wheel reaching stillness without engagement: honest accounting only.
      if (speed <= STILL_REV_S) {
        if (settleStillSinceMs == 0) settleStillSinceMs = nowMs;
        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          finishFrictionCapture("coast-end", PwFitEnd::CoastEnd);
          SpinResult res = !controlAvailable() ? RES_CONTROL_LOCKED
                          : (spin.hadContact ? RES_GUEST_STOPPED
                                             : RES_NO_REACHABLE_SAFE);
          if (controlAvailable() && takeoverEnabled &&
              (isDare(currentWedge()) || dareDistanceDeg(wheelAngleDeg()) < DARE_PROXIMITY_FAULT_DEG)) {
            dareRecoveryAttempts = 0;
            spin.targetWedge = -1;
            if (startDareRecovery("free coast")) break;
          }
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
      launchCapture(nowMs); // never reselect the absolute reserved prize
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
      if (!controlSpeedSafe() || !controlDriverSafe(nowMs)) break;
      // Pulse train has tapered to zero; the wheel settles under taper
      // current.  Landing requires BOTH interior position AND stillness.
      if (!encoderPositionFresh()) {
        enterFault(FC_ENCODER_STALE, "stale during settle");
        break;
      }
      setCurrentStage(CS_BRAKE);  // retain 2200 mA through verified settling
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
      if (!controlDriverSafe(nowMs)) break;
      // Persistent hold (2026-09-21): the unbalanced wheel rolls off any landing
      // on a slope once freewheeled, so holding torque stays on until the next
      // guest push. Release on sustained motion OR on a deflection from the
      // settled hold angle (a hand pushing against the field), before pole slip.
      uint32_t age = nowMs - stateEnteredMs;
      if (age >= 300 && !holdAnchorValid && encoderPositionFresh()) {
        holdAnchorDeg = wheelAngleDeg(); holdAnchorValid = true;
      }
      bool pushed = false;
      if (holdAnchorValid && encoderPositionFresh()) {
        float dev = fabsf(wheelAngleDeg() - holdAnchorDeg);
        if (dev > 180.0f) dev = 360.0f - dev;
        if (dev >= HOLD_RELEASE_DEFLECT_DEG) {
          if (holdDeflectSinceMs == 0) holdDeflectSinceMs = nowMs;
          else if (nowMs - holdDeflectSinceMs >= HOLD_RELEASE_CONFIRM_MS) pushed = true;
        } else holdDeflectSinceMs = 0;
      }
      if (pushed) Serial.printf("# HOLD released: pushed %.2f deg off anchor after %lu ms\n",
                                fabsf(wheelAngleDeg() - holdAnchorDeg), (unsigned long)age);
      if (pushed || (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S)) {
        holdAnchorValid = false; holdDeflectSinceMs = 0;
        driverFreewheel();
        captureCycle.holdReleased(faultCode == FC_NONE && currentStage == CS_FREEWHEEL &&
            digitalRead(PIN_EN) == HIGH && stepper && !stepper->isRunning());
        candidateStartCounts = encoderCountsMT;
        spinArmMs = 0;
        settleStillSinceMs = 0;   // never inherit the landing's stillness age
        state = ST_MOTION_CANDIDATE;
        stateEnteredMs = nowMs;
      }
      break;
    }

    case ST_RECOVERY_NUDGE: {
      if (!controlDriverSafe(nowMs)) break;
      if (!encoderPositionFresh()) { enterFault(FC_ENCODER_STALE, "stale during recovery"); break; }
      bool still = !stepper->isRunning() && encoderMotionReady() && fabsf(omega) <= STILL_REV_S;
      if (still) { if (recoveryStillSinceMs == 0) recoveryStillSinceMs = nowMs; }
      else recoveryStillSinceMs = 0;
      if (nowMs - stateEnteredMs > RECOVERY_TIMEOUT_MS) {
        stepper->forceStop();
        enterFault(FC_LANDING_UNSAFE, "dare recovery timed out");
        break;
      }
      if (recoveryStillSinceMs != 0 && nowMs - recoveryStillSinceMs >= SETTLE_MS) {
        Serial.printf("# DARE_RECOVERY settled angle=%.1f wedge=%d\n", wheelAngleDeg(), currentWedge());
        setCurrentStage(CS_BRAKE);
        state = ST_LANDING_SETTLE;   // verdict re-runs: safe -> hold, dare -> one more attempt, then latch
        settleStillSinceMs = nowMs - SETTLE_MS;
        settleWindowCounts = encoderCountsMT;
        landingVerdict();
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
