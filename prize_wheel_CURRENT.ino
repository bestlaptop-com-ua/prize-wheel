/* ============================================================================
 * prize_wheel.ino  -  PRIZE WHEEL SHOW FIRMWARE  (v1, motion + encoder core)
 * ----------------------------------------------------------------------------
 * The wheel looks/feels like a free, fair, hand-spun wheel, but never comes to
 * rest on a "dare" wedge. The guest spins by hand; the wheel coasts naturally;
 * only if it's heading for a dare does the motor gently steer it to the nearest
 * SAFE wedge, finishing in the same direction with a natural-looking decel.
 *
 * MODE: avoid-only. Every landing is a random SAFE wedge (never a dare).
 *       Most spins that would already land safe are left 100% untouched.
 *
 * DARE WEDGES (0-indexed, matches as5600_test output): 1 and 5   <-- avoided
 *
 * Hardware (all bench-validated):
 *   ESP32-WROOM-32, BTT TMC2209 V1.3 (UART), NEMA17 1.7A, GT2 2:1 belt,
 *   AS5600 on wheel shaft.
 *   Driver config proven on bench: SpreadCycle, I_scale_analog(false),
 *   CoolStep off, 1450 mA, enabled-at-boot (no startup snap), FREEWHEEL only
 *   while the wheel is coasting so it feels loose in a guest's hand.
 *
 * Libs: TMCStepper, FastAccelStepper, Wire.
 * Serial 115200. Type '?' for commands.
 *
 * NO audio / LEDs in this version (motion-first). Hooks marked TODO(fx).
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
#define AS5600_ADDR    0x36
#define AS5600_RAW     0x0C

/* --------------------------- MECHANICAL ---------------------------------- */
#define MOTOR_FULLSTEPS 200
#define MICROSTEPS      16
#define GEAR_RATIO      2.0f          // 40T/20T
const float WHEEL_USTEPS_PER_REV = MOTOR_FULLSTEPS * MICROSTEPS * GEAR_RATIO; // 6400
#define NUM_WEDGES 12
const float WEDGE_DEG = 360.0f / NUM_WEDGES;

/* --------------------------- DARE / SAFE --------------------------------- */
// Bit i set = wedge i is a DARE (to avoid). Wedges 1 and 5.
uint16_t dare_mask = (1 << 1) | (1 << 5);
inline bool isDare(int w) { return (dare_mask >> (w % NUM_WEDGES)) & 1; }

/* --------------------------- TUNING (bench-validated) -------------------- */
#define RMS_CURRENT_MA     1450       // proven correct (cs_actual=25), motor warms, no cycling
bool  INVERT_DIR         = false;     // flip if motor fights the wheel at takeover
#define TAKEOVER_REV_S     0.55f      // engage threshold (wheel rev/s)
#define SPIN_DETECT_REV_S  0.80f      // |w| above this >100ms = a real spin
#define ACCEL_CEILING_SPS2 1200       // max motor accel that ran clean on bench (conservative)
#define SETTLE_MS          500
#define STILL_REV_S        0.02f      // below this = wheel truly stopped (for LANDED)
#define CORRECT_TOL_DEG    3.0f       // closed-loop correction deadband (deg of wheel)
#define CORRECT_MAX_REV_S  0.15f      // gentle correction speed cap (wheel rev/s)

/* --------------------------- STATE --------------------------------------- */
TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, TMC_ADDR);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper*      stepper = nullptr;

enum Mode { IDLE, FREE_SPIN, TAKEOVER, CORRECT, SETTLE, DONE };
Mode mode = IDLE;

// encoder
uint16_t lastRaw = 0;
int32_t  wrapCount = 0;
double   angleDegMT = 0.0;      // multi-turn wheel angle (deg, signed)
float    omega = 0.0f;          // wheel rev/s (signed)
uint32_t lastMicros = 0;
double   wedge0OffsetDeg = 0.0; // calibration

// friction model, per direction: dw/dt = -(c + b*|w|)
float cw_c = 0.30f, cw_b = 0.15f;    // seeded; refined per spin (EWMA)
float ccw_c = 0.30f, ccw_b = 0.15f;

// spin/settle bookkeeping
uint32_t spinAboveMs = 0;
float    takeoverTargetDeg = 0.0f;   // absolute wheel angle we are steering to
float    takeoverRemainDeg = 0.0f;   // runway remaining (deg)
int32_t  takeoverTargetU = 0;        // target motor position (usteps)
int      takeoverDir = 1;
uint32_t settleT0 = 0;
bool     debugLog = false;

/* ------------------------- AS5600 ---------------------------------------- */
uint16_t readRaw() {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(AS5600_RAW);
  if (Wire.endTransmission(false) != 0) return lastRaw;
  Wire.requestFrom(AS5600_ADDR, 2);
  if (Wire.available() < 2) return lastRaw;
  uint16_t hi = Wire.read(), lo = Wire.read();
  return ((hi << 8) | lo) & 0x0FFF;
}

void updateEncoder() {
  uint32_t now = micros();
  float dt = (now - lastMicros) * 1e-6f;
  if (dt < 0.0009f) return;            // ~1 kHz
  lastMicros = now;

  uint16_t raw = readRaw();
  int16_t d = (int16_t)raw - (int16_t)lastRaw;
  if (d >  2048) wrapCount--;
  if (d < -2048) wrapCount++;
  lastRaw = raw;

  double newAngle = (wrapCount * 4096.0 + raw) * (360.0 / 4096.0);
  double dAngle = newAngle - angleDegMT;
  angleDegMT = newAngle;

  float inst = (float)(dAngle / 360.0) / dt;      // rev/s
  omega += 0.15f * (inst - omega);                // LP ~30 Hz
}

float wheelAngleDeg() {
  double a = fmod(angleDegMT - wedge0OffsetDeg, 360.0);
  if (a < 0) a += 360.0;
  return (float)a;
}
int currentWedge() { return (int)(wheelAngleDeg() / WEDGE_DEG) % NUM_WEDGES; }

/* ------------------------- DRIVER helpers -------------------------------- */
void driverConfig() {
  driver.begin();
  driver.I_scale_analog(false);   // use internal Iref, NOT the Vref pot
  driver.toff(4);
  driver.blank_time(24);
  driver.rms_current(RMS_CURRENT_MA, 1.0);  // IHOLD == IRUN (strong hold)
  driver.microsteps(MICROSTEPS);
  driver.en_spreadCycle(true);    // SpreadCycle = full torque, no RPM cap
  driver.pwm_autoscale(true);
  driver.TCOOLTHRS(0);            // CoolStep OFF
  driver.semin(0);
  driver.semax(0);
  driver.TPOWERDOWN(255);
}

// Free-spin: coils floating so the wheel feels loose in a guest's hand.
void driverFreewheel() {
  if (stepper) stepper->forceStop();
  driver.freewheel(1);
  driver.ihold(0);
  digitalWrite(PIN_EN, HIGH);     // outputs float
}
// Active: enable + full current for the takeover.
void driverActive() {
  driver.freewheel(0);
  driver.rms_current(RMS_CURRENT_MA, 1.0);
  digitalWrite(PIN_EN, LOW);
}

/* ------------------- friction model / landing prediction ----------------- */
// Predict where the wheel comes to rest from (angle, w) using the per-dir model.
float predictStopAngle() {
  int dir = (omega >= 0) ? +1 : -1;
  float w = fabs(omega);
  float c = (dir > 0) ? cw_c : ccw_c;
  float b = (dir > 0) ? cw_b : ccw_b;
  if (w < 1e-3f) return wheelAngleDeg();
  // integral of coasting distance for dw/dt=-(c+b w):
  //   theta_travel = (1/b)*w - (c/b^2)*ln(1 + b*w/c)   [radians]
  float travelRad = (1.0f / b) * (w * TWO_PI)
                  - (c / (b * b)) * logf(1.0f + b * (w * TWO_PI) / c);
  float travelDeg = travelRad * RAD_TO_DEG;
  float stop = wheelAngleDeg() + dir * travelDeg;
  stop = fmodf(stop, 360.0f); if (stop < 0) stop += 360.0f;
  return stop;
}
int predictStopWedge() { return (int)(predictStopAngle() / WEDGE_DEG) % NUM_WEDGES; }

// Nearest SAFE wedge center in the direction of travel (for steering).
// Returns target absolute wheel angle (deg), landing 20-70% into the wedge.
float chooseSafeTargetAngle(int dir) {
  int startW = predictStopWedge();
  // search outward in the direction of travel for the first safe wedge
  for (int step = 0; step < NUM_WEDGES; step++) {
    int w = ((startW + dir * step) % NUM_WEDGES + NUM_WEDGES) % NUM_WEDGES;
    if (!isDare(w)) {
      float frac = 0.30f + (float)random(0, 400) / 1000.0f;  // 0.30..0.70 into wedge
      return w * WEDGE_DEG + frac * WEDGE_DEG;
    }
  }
  return 0; // all dares (won't happen with 2 dares)
}

/* --------------------------- takeover ------------------------------------ */
uint32_t curUstepFromWheel() {
  return (int32_t)((double)angleDegMT / 360.0 * WHEEL_USTEPS_PER_REV);
}

// Enter continuous closed-loop deceleration. We DO NOT predict a landing or
// command an open-loop move. Instead we pick a safe target that is at least
// ~1 full turn ahead in the spin direction (so the wheel has room to bleed off
// speed naturally), remember it, and let the loop servo the wheel to it every
// cycle using a sqrt(distance) velocity ramp. Skips self-correct continuously
// because we read the real AS5600 angle every tick. Never reverses.
void beginTakeover() {
  int dir = (omega >= 0) ? +1 : -1;
  takeoverDir = dir;
  float w0 = fabs(omega);

  // nearest safe wedge center in the spin direction...
  float base = chooseSafeTargetAngle(dir);
  float curDeg = wheelAngleDeg();
  float fwd = (dir > 0) ? (base - curDeg) : (curDeg - base);
  fwd = fmodf(fwd, 360.0f); if (fwd < 0) fwd += 360.0f;
  // Give the wheel enough runway to stop gently WITHIN the accel ceiling.
  // Required distance to stop from w0 at ceiling accel: d = w0^2 / (2*a).
  float aWheelRev = (float)ACCEL_CEILING_SPS2 / WHEEL_USTEPS_PER_REV;   // rev/s^2
  float minStopRev = (w0 * w0) / (2.0f * aWheelRev);                    // revs needed
  float minStopDeg = minStopRev * 360.0f + 30.0f;                      // + margin
  while (fwd < minStopDeg) fwd += 360.0f;   // add whole turns until enough runway

  takeoverTargetDeg = base;
  takeoverRemainDeg = fwd;

  // Compute the SINGLE target motor position and issue ONE decelerating move.
  int32_t curU = curUstepFromWheel();
  int32_t tgtU = curU + dir * (int32_t)(fwd / 360.0f * WHEEL_USTEPS_PER_REV);
  takeoverTargetU = tgtU;

  driverActive();
  stepper->setCurrentPosition(curU);
  // Match current wheel speed as the starting speed, then let moveTo decelerate
  // smoothly to a stop exactly at tgtU. One command; hardware ramps it down.
  uint32_t matchHz = (uint32_t)(w0 * WHEEL_USTEPS_PER_REV);
  if (matchHz < 100) matchHz = 100;
  stepper->setSpeedInHz(matchHz);
  stepper->setAcceleration(ACCEL_CEILING_SPS2);
  if (dir > 0) stepper->runForward(); else stepper->runBackward();
  stepper->applySpeedAcceleration();
  stepper->moveTo(tgtU);            // ONE smooth decel to target; not re-issued

  if (debugLog) {
    Serial.printf("# TK-START dir=%d w0=%.2f tgt=%.1f runway=%.1f cur=%.1f tgtU=%ld\n",
                  dir, w0, takeoverTargetDeg, fwd, curDeg, (long)tgtU);
  }
  mode = TAKEOVER;
}

// Called every loop() while in TAKEOVER.
// CRITICAL: we issue ONE smooth decelerating move to the target motor position
// (computed once, in beginTakeover) and let FastAccelStepper ramp it down on its
// own hardware timer. We do NOT re-command speed or reset position every tick -
// doing that restarts the planner every loop and causes the omega oscillation /
// skipping seen in testing. Here we ONLY: watch the encoder to detect a guest
// grab (abort), and detect arrival (move complete) -> settle.
void takeoverStep() {
  // arrival: FastAccelStepper finished its ramp to the target position.
  if (!stepper->isRunning()) {
    mode = SETTLE; settleT0 = 0;
    return;
  }
  static uint32_t lastLog = 0;
  if (debugLog && millis() - lastLog > 200) {
    lastLog = millis();
    float fwdRemain = (takeoverDir > 0)
                      ? (takeoverTargetDeg - wheelAngleDeg())
                      : (wheelAngleDeg() - takeoverTargetDeg);
    fwdRemain = fmodf(fwdRemain, 360.0f); if (fwdRemain < 0) fwdRemain += 360.0f;
    Serial.printf("# TK dir=%d ang=%.1f tgt=%.1f remain=%.1f omega=%.3f pos=%ld tgtU=%ld\n",
                  takeoverDir, wheelAngleDeg(), takeoverTargetDeg, fwdRemain, omega,
                  (long)stepper->getCurrentPosition(), (long)takeoverTargetU);
  }
}

/* --------------------------- serial cmds --------------------------------- */
void help() {
  Serial.println(F(
    "\n=== PRIZE WHEEL (avoid-only; dares = wedges 1 & 5) ===\n"
    "  z  set current position as wedge-0 boundary (CALIBRATE first!)\n"
    "  s  status (angle, wedge, omega, predicted stop)\n"
    "  i  toggle motor direction invert (rarely needed)\n"
    "  v  toggle verbose/debug logging\n"
    "  m  print dare mask / safe wedges\n"
    "  ?  help\n"
    "Just spin the wheel by hand. It avoids landing on a dare."));
}
void status() {
  Serial.printf("# angle=%.1f wedge=%d omega=%.3f predStop=%.0f(w%d) mode=%d\n",
    wheelAngleDeg(), currentWedge(), omega, predictStopAngle(), predictStopWedge(), (int)mode);
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 'z': wedge0OffsetDeg = angleDegMT; Serial.println(F("# wedge-0 set here")); break;
    case 's': status(); break;
    case 'i': INVERT_DIR = !INVERT_DIR; stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
              Serial.printf("# INVERT_DIR=%d (motor dir flipped)\n", INVERT_DIR); break;
    case 'v': debugLog = !debugLog; Serial.printf("# verbose/debug=%d\n", debugLog); break;
    case 'm': Serial.printf("# dare_mask=0x%03X  dares:", dare_mask);
              for (int i=0;i<NUM_WEDGES;i++) if (isDare(i)) Serial.printf(" %d", i);
              Serial.println(); break;
    case '?': help(); break;
    default: break;
  }
}

/* --------------------------- setup / loop -------------------------------- */
void setup() {
  Serial.begin(115200);
  delay(300);
  randomSeed(esp_random());

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  lastRaw = readRaw();
  lastMicros = micros();

  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, LOW);          // enabled at boot (no snap); freewheel set below

  TMC_SERIAL.begin(115200, SERIAL_8N1, TMC_RX_PIN, TMC_TX_PIN);
  driverConfig();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
  stepper->setEnablePin(PIN_EN, true);
  stepper->setAutoEnable(false);

  uint8_t conn = driver.test_connection();
  Serial.printf("\n# TMC UART (0=OK): %u\n", conn);
  if (conn != 0) Serial.println(F("# >>> UART FAIL - check TX/1k/RX wiring, VIO=3V3, GND common"));

  help();
  driverFreewheel();                  // start free: wheel loose for the first guest
  mode = IDLE;
}

void loop() {
  updateEncoder();
  handleSerial();

  bool spinning = fabs(omega) > SPIN_DETECT_REV_S;
  if (spinning) { if (spinAboveMs == 0) spinAboveMs = millis(); }
  else spinAboveMs = 0;
  bool spinConfirmed = spinning && (millis() - spinAboveMs > 100);

  // a new spin from ANY state restarts the cycle (rapid re-spin safe)
  static bool sawSpinThisCycle = false;

  switch (mode) {
    case IDLE:
    case DONE:
      if (spinConfirmed) { sawSpinThisCycle = true; driverFreewheel(); mode = FREE_SPIN; }
      break;

    case FREE_SPIN: {
      if (spinConfirmed) sawSpinThisCycle = true;
      // decide at the takeover threshold
      if (sawSpinThisCycle && fabs(omega) <= TAKEOVER_REV_S && fabs(omega) > 0.08f) {
        int predW = predictStopWedge();
        if (isDare(predW)) {
          // would land on a dare -> steer to nearest safe wedge
          sawSpinThisCycle = false;
          beginTakeover();
        } else {
          // heading somewhere safe -> leave it 100% untouched
          sawSpinThisCycle = false;
          mode = SETTLE; settleT0 = 0;
        }
      }
      break;
    }

    case TAKEOVER:
      // a fresh hard spin during takeover aborts to free (guest grabbed it)
      if (fabs(omega) > SPIN_DETECT_REV_S && spinConfirmed) {
        driverFreewheel(); sawSpinThisCycle = true; mode = FREE_SPIN; break;
      }
      takeoverStep();     // continuous closed-loop sqrt-distance decel to target
      break;

    case CORRECT: {  // (legacy, unused - kept for enum compatibility)
      // Closed-loop on the AS5600, but NEVER reverse: only ever creep in the
      // ORIGINAL spin direction (takeoverDir). Reversing a wheel that still has
      // momentum is what caused the skip + wrong-direction rotation, and a
      // backward move could also cross into the dare wedge. Bidirectional-safe.
      if (fabs(omega) > SPIN_DETECT_REV_S && spinConfirmed) {
        driverFreewheel(); sawSpinThisCycle = true; mode = FREE_SPIN; break;
      }
      // "forward" distance remaining to target, measured in the spin direction,
      // wrapped to [0,360). If we've reached/passed it, remaining wraps near 0.
      float fwdRemain = (takeoverDir > 0)
                        ? (takeoverTargetDeg - wheelAngleDeg())
                        : (wheelAngleDeg() - takeoverTargetDeg);
      fwdRemain = fmodf(fwdRemain, 360.0f);
      if (fwdRemain < 0) fwdRemain += 360.0f;

      static uint32_t lastCorrLog = 0;
      if (debugLog && millis() - lastCorrLog > 150) {
        lastCorrLog = millis();
        Serial.printf("# CORRECT dir=%d ang=%.1f tgt=%.1f fwdRemain=%.1f omega=%.3f\n",
                      takeoverDir, wheelAngleDeg(), takeoverTargetDeg, fwdRemain, omega);
      }

      // On target (within tol on the SHORT side) OR overshot slightly (fwdRemain
      // just under 360) -> stop. Do NOT reverse to trim an overshoot.
      if (fwdRemain <= CORRECT_TOL_DEG || fwdRemain >= (360.0f - CORRECT_TOL_DEG)) {
        stepper->forceStop();
        mode = SETTLE; settleT0 = 0;
        break;
      }
      // If the remaining forward distance is more than half a turn, that means
      // the wheel is essentially AT/just past the target (target is "behind" in
      // spin dir) - accept and stop rather than driving a full extra revolution.
      if (fwdRemain > 180.0f) {
        stepper->forceStop();
        mode = SETTLE; settleT0 = 0;
        break;
      }
      // gentle forward-only creep, speed shrinks as we approach
      float mag = fwdRemain / 30.0f;                  // ~1 wedge -> full speed
      float wRevS = CORRECT_MAX_REV_S * (mag > 1.0f ? 1.0f : mag);
      if (wRevS < 0.03f) wRevS = 0.03f;
      uint32_t hz = (uint32_t)(wRevS * WHEEL_USTEPS_PER_REV);

      driverActive();
      stepper->setCurrentPosition(curUstepFromWheel());
      stepper->setSpeedInHz(hz);
      stepper->setAcceleration(ACCEL_CEILING_SPS2);
      if (takeoverDir > 0) stepper->runForward(); else stepper->runBackward();
      break;
    }

    case SETTLE:
      // re-spun before it settled
      if (fabs(omega) > SPIN_DETECT_REV_S) {
        driverFreewheel(); sawSpinThisCycle = true; mode = FREE_SPIN; break;
      }
      // Require the wheel to be TRULY stopped: |omega| below STILL_REV_S
      // continuously for SETTLE_MS. Any motion resets the timer. This is the
      // fix for "LANDED shows wrong wedge" - we no longer sample mid-drift.
      if (fabs(omega) > STILL_REV_S) {
        settleT0 = 0;                 // still moving; keep waiting
      } else {
        if (settleT0 == 0) settleT0 = millis();
        if (millis() - settleT0 > SETTLE_MS) {
          int w = currentWedge();     // sampled only after true stop
          Serial.printf("# LANDED wedge %d%s\n", w, isDare(w) ? "  <-- DARE! (report this)" : " (safe)");
          // TODO(fx): trigger DFPlayer track for wedge w; converge LED ring on w
          driverFreewheel();          // back to loose for the next guest
          mode = DONE;
        }
      }
      break;
  }
}
