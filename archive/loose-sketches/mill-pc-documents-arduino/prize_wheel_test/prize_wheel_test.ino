/* ============================================================================
 * prize_wheel_test.ino  ???  BRING-UP / TEST firmware  (v1)
 * ----------------------------------------------------------------------------
 * Purpose: validate HARDWARE before any scripted-show logic exists.
 * Implements brief ??11:
 *   1) FREEWHEEL ten-spin test        -> command `f`, hand-spin, watch settle
 *   2) CSV log of free spins          -> command `l`, feeds the friction fit
 *   3) TAKEOVER REHEARSAL             -> command `t`, logs commanded vs measured
 *   4) Wedge-0 calibration            -> command `z`
 *
 * This is NOT the show firmware. No script, no near-miss, no audio/LEDs.
 * It exists to answer: is the wheel balanced, is detent low enough, does the
 * belt grip without slip/skip, is direction correct, does the friction model fit.
 *
 * Board : ESP32-WROOM-32
 * Libs  : TMCStepper (>=0.7)   https://github.com/teemuatlut/TMCStepper
 *         FastAccelStepper (>=0.30) https://github.com/gin66/FastAccelStepper
 *         Wire (builtin) for AS5600
 *
 * Serial: 115200 baud. Type `?` for the command list.
 * ========================================================================== */

#include <Wire.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>

/* ----------------------------- PIN MAP ----------------------------------- */
// TMC2209 over UART (single-wire; tie TX/RX through 1k if using one pin).
#define TMC_SERIAL      Serial2
#define TMC_RX_PIN      16          // ESP32 RX  <- TMC TX (PDN/UART)
#define TMC_TX_PIN      17          // ESP32 TX  -> TMC RX (PDN/UART, via 1k)
#define TMC_ADDR        0b00        // MS1/MS2 -> GND/GND
#define R_SENSE         0.11f       // 0.11 for BTT/most 2209 modules; check yours

#define PIN_EN          25          // TMC enable (active LOW)
#define PIN_STEP        26
#define PIN_DIR         27

// AS5600 on default I2C
#define PIN_SDA         21
#define PIN_SCL         22
#define AS5600_ADDR     0x36
#define AS5600_RAWANGLE 0x0C        // 12-bit RAW angle (unfiltered) hi/lo

/* --------------------------- MECHANICAL ---------------------------------- */
#define MOTOR_FULLSTEPS 200         // 1.8 deg motor
#define MICROSTEPS      16
#define GEAR_RATIO      2.0f        // 40T wheel / 20T motor = 2:1 (motor spins 2x)
// microsteps of MOTOR per one WHEEL revolution:
const float WHEEL_USTEPS_PER_REV = MOTOR_FULLSTEPS * MICROSTEPS * GEAR_RATIO; // 6400
#define NUM_WEDGES      12
const float WEDGE_DEG = 360.0f / NUM_WEDGES; // 30 deg

// Set true if takeover rehearsal shows the motor FIGHTING the wheel (wrong dir).
bool INVERT_DIR = false;

/* --------------------------- TUNING -------------------------------------- */
#define RMS_CURRENT_MA    900       // takeover run current (NEMA17). start modest.
#define TAKEOVER_REV_S    0.5f      // engage threshold (wheel rev/s)
#define SPIN_DETECT_REV_S 0.8f      // |w| above this for >100ms = a spin
#define MAX_MATCH_HZ      40000     // clamp motor step rate (safety)
#define SETTLE_MS         500

/* --------------------------- STATE --------------------------------------- */
TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, TMC_ADDR);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper*      stepper = nullptr;

enum Mode { IDLE_FREE, LOG_FREE, ARM_TAKEOVER, RUN_TAKEOVER, SETTLE };
Mode mode = IDLE_FREE;

// encoder
volatile uint16_t rawAngle = 0;
int32_t   wrapCount = 0;          // accumulated full-turn wraps
uint16_t  lastRaw   = 0;
double    angleDegMT = 0.0;       // multi-turn wheel angle, deg (signed accum)
float     omega = 0.0f;           // wheel rev/s (signed, +CW as raw increases)
double    wedge0OffsetDeg = 0.0;  // calibration

// timing
uint32_t  lastMicros = 0;
uint32_t  lastLogMs  = 0;
uint32_t  spinAboveMs = 0;

// takeover rehearsal params
int   targetWedge = 0;
float lastTakeoverAmax = 0;

/* ------------------------- AS5600 READ ----------------------------------- */
uint16_t readRawAngle() {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(AS5600_RAWANGLE);
  if (Wire.endTransmission(false) != 0) return rawAngle; // keep last on error
  Wire.requestFrom(AS5600_ADDR, 2);
  if (Wire.available() < 2) return rawAngle;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  return ((hi << 8) | lo) & 0x0FFF; // 0..4095
}

// Unwrap + velocity. Call as fast as possible (~1 kHz).
void updateEncoder() {
  uint32_t now = micros();
  float dt = (now - lastMicros) * 1e-6f;
  if (dt < 0.0009f) return;         // ~1 kHz cap
  lastMicros = now;

  rawAngle = readRawAngle();
  int16_t d = (int16_t)rawAngle - (int16_t)lastRaw;
  if (d >  2048) wrapCount--;       // wrapped 4095->0 going + ... handle shortest path
  if (d < -2048) wrapCount++;
  lastRaw = rawAngle;

  double newAngle = (wrapCount * 4096.0 + rawAngle) * (360.0 / 4096.0);
  double dAngle = newAngle - angleDegMT;    // deg this tick
  angleDegMT = newAngle;

  // velocity in rev/s, low-passed (single pole, fc ~30 Hz)
  float instRevS = (float)(dAngle / 360.0) / dt;
  const float a = 0.15f;            // ~ (2*pi*30*dt)/(1+...) approx; tune if noisy
  omega += a * (instRevS - omega);
}

float wheelAngleDeg() { // 0..360 relative to wedge0 offset
  double a = angleDegMT - wedge0OffsetDeg;
  a = fmod(a, 360.0); if (a < 0) a += 360.0;
  return (float)a;
}
int currentWedge() { return (int)(wheelAngleDeg() / WEDGE_DEG) % NUM_WEDGES; }

/* ------------------------- DRIVER HELPERS -------------------------------- */
void driverFreewheel() {            // truest "free": disable outputs entirely
  if (stepper) stepper->forceStop();
  digitalWrite(PIN_EN, HIGH);       // disable -> coils float -> detent only
  driver.ihold(0);
  driver.freewheel(1);
}
void driverEnable(uint16_t rms) {
  driver.rms_current(rms);
  digitalWrite(PIN_EN, LOW);
}

/* --------------------------- SETUP --------------------------------------- */
void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  lastRaw = readRawAngle();
  lastMicros = micros();

  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);       // start disabled = free

  TMC_SERIAL.begin(115200, SERIAL_8N1, TMC_RX_PIN, TMC_TX_PIN);
  driver.begin();
  driver.toff(4);
  driver.blank_time(24);
  driver.rms_current(RMS_CURRENT_MA);
  driver.microsteps(MICROSTEPS);
  driver.en_spreadCycle(false);     // StealthChop at low speed
  driver.pwm_autoscale(true);
  driver.TPWMTHRS(0);               // 0 = stealth everywhere for the test speeds
  driver.ihold(0);                  // needed for real freewheel at standstill
  driver.freewheel(1);

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
  stepper->setEnablePin(PIN_EN, true);  // active low
  stepper->setAutoEnable(false);        // WE manage enable (for freewheel)
  stepper->setAcceleration(50000);

  Serial.println(F("\n=== PRIZE WHEEL TEST FIRMWARE ==="));
  Serial.print(F("wheel usteps/rev = ")); Serial.println(WHEEL_USTEPS_PER_REV);
  printHelp();
  driverFreewheel();
}

/* --------------------------- COMMANDS ------------------------------------ */
void printHelp() {
  Serial.println(F(
    "commands:\n"
    "  f  freewheel (disable driver) - use for the ten-spin test\n"
    "  l  toggle CSV logging of free spins (friction fit data)\n"
    "  t  arm takeover rehearsal on the NEXT spin\n"
    "  w<n> set target wedge for takeover (e.g. w7)\n"
    "  z  set current position as wedge-0 boundary (calibration)\n"
    "  d  toggle motor direction invert (fix if motor fights wheel)\n"
    "  s  status\n"
    "  ?  this help\n"
    "CSV columns: t_ms,raw,angle_mt_deg,omega_rev_s,wedge,cmd_ustep,meas_ustep,state"));
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 'f': mode = IDLE_FREE; driverFreewheel();
              Serial.println(F("# FREEWHEEL. Hand-spin x10 each dir, watch settle.")); break;
    case 'l': mode = (mode == LOG_FREE) ? IDLE_FREE : LOG_FREE;
              if (mode == LOG_FREE){ driverFreewheel(); Serial.println(F("# LOGGING on")); }
              else Serial.println(F("# LOGGING off")); break;
    case 't': mode = ARM_TAKEOVER; driverFreewheel();
              Serial.print(F("# ARMED takeover -> wedge ")); Serial.println(targetWedge); break;
    case 'w': { int n = Serial.parseInt();
              if (n >= 0 && n < NUM_WEDGES){ targetWedge = n;
                Serial.print(F("# target wedge = ")); Serial.println(targetWedge);} } break;
    case 'z': wedge0OffsetDeg = angleDegMT;
              Serial.println(F("# wedge-0 boundary set at current position")); break;
    case 'd': INVERT_DIR = !INVERT_DIR;
              stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
              Serial.print(F("# INVERT_DIR = ")); Serial.println(INVERT_DIR); break;
    case 's': printStatus(); break;
    case '?': printHelp(); break;
    default: break;
  }
}

void printStatus() {
  Serial.print(F("# angle_mt=")); Serial.print(angleDegMT,1);
  Serial.print(F(" wheel=")); Serial.print(wheelAngleDeg(),1);
  Serial.print(F(" omega=")); Serial.print(omega,3);
  Serial.print(F(" wedge=")); Serial.print(currentWedge());
  Serial.print(F(" mode=")); Serial.println((int)mode);
}

/* ------------------------ TAKEOVER REHEARSAL ----------------------------- */
// Match motor speed to wheel, then decelerate to land on targetWedge in the
// SAME direction. Logs commanded vs measured so slip/skip is visible on a plot.
void beginTakeover() {
  int dir = (omega >= 0) ? +1 : -1;
  float w0 = fabs(omega);                         // rev/s

  // target angle: 45% into the wedge (single fixed offset for the test)
  float tgtWheelDeg = targetWedge * WEDGE_DEG + 0.45f * WEDGE_DEG;
  float curWheelDeg = wheelAngleDeg();
  // signed forward distance to target in direction of travel, within [0,360)
  float fwd = (dir > 0) ? (tgtWheelDeg - curWheelDeg) : (curWheelDeg - tgtWheelDeg);
  fwd = fmodf(fwd, 360.0f); if (fwd < 0) fwd += 360.0f;

  // choose n extra revs so decel is gentle & natural (test: pick smallest that
  // keeps a_req modest; show firmware does the +/-15% naturalness match instead)
  int nBest = 1; float aBest = 1e9f;
  for (int n = 1; n <= 3; n++) {
    float dTheta = fwd + n * 360.0f;              // deg
    float dRad   = dTheta * DEG_TO_RAD;
    float w0rad  = w0 * TWO_PI;                    // rad/s
    float a = (w0rad * w0rad) / (2.0f * dRad);     // rad/s^2
    if (a < aBest) { aBest = a; nBest = n; }
  }
  float dTheta = fwd + nBest * 360.0f;
  lastTakeoverAmax = aBest;

  // Frame sync: tell FastAccelStepper where the WHEEL is, in motor usteps.
  int32_t curUstep = (int32_t)((double)angleDegMT / 360.0 * WHEEL_USTEPS_PER_REV);
  stepper->setCurrentPosition(curUstep);

  // Matched speed & target position in motor usteps
  uint32_t matchHz = (uint32_t)min((float)MAX_MATCH_HZ,
                       w0 * WHEEL_USTEPS_PER_REV);
  int32_t  tgtUstep = curUstep + dir * (int32_t)(dTheta / 360.0f * WHEEL_USTEPS_PER_REV);
  uint32_t aHz2 = (uint32_t)(aBest / TWO_PI * WHEEL_USTEPS_PER_REV); // usteps/s^2

  driverEnable(RMS_CURRENT_MA);
  stepper->setSpeedInHz(matchHz);
  stepper->setAcceleration(2000000);    // step gen catches wheel speed in ~ms
  if (dir > 0) stepper->runForward(); else stepper->runBackward();
  stepper->setAcceleration(aHz2);       // now the gentle profile decel
  stepper->applySpeedAcceleration();
  stepper->moveTo(tgtUstep);            // decelerate into target

  Serial.print(F("# TAKEOVER dir=")); Serial.print(dir);
  Serial.print(F(" w0=")); Serial.print(w0,2);
  Serial.print(F(" n=")); Serial.print(nBest);
  Serial.print(F(" a=")); Serial.print(aBest,3);
  Serial.print(F(" rad/s^2  matchHz=")); Serial.println(matchHz);
  mode = RUN_TAKEOVER;
}

/* --------------------------- LOG ----------------------------------------- */
void logRow(const char* state) {
  int32_t cmd  = stepper ? stepper->getCurrentPosition() : 0;
  int32_t meas = (int32_t)((double)angleDegMT / 360.0 * WHEEL_USTEPS_PER_REV);
  Serial.print(millis());        Serial.print(',');
  Serial.print(rawAngle);        Serial.print(',');
  Serial.print(angleDegMT, 2);   Serial.print(',');
  Serial.print(omega, 4);        Serial.print(',');
  Serial.print(currentWedge());  Serial.print(',');
  Serial.print(cmd);             Serial.print(',');
  Serial.print(meas);            Serial.print(',');
  Serial.println(state);
}

/* --------------------------- LOOP ---------------------------------------- */
void loop() {
  updateEncoder();
  handleSerial();

  bool spinning = fabs(omega) > SPIN_DETECT_REV_S;
  if (spinning) spinAboveMs = (spinAboveMs == 0) ? millis() : spinAboveMs;
  else          spinAboveMs = 0;
  bool spinConfirmed = spinning && (millis() - spinAboveMs > 100);

  switch (mode) {
    case IDLE_FREE:
      break;

    case LOG_FREE:
      if (millis() - lastLogMs >= 2) { lastLogMs = millis(); logRow("FREE"); }
      break;

    case ARM_TAKEOVER: {
      if (millis() - lastLogMs >= 2) { lastLogMs = millis(); logRow("ARMED"); }
      // Latch that a real spin occurred, then fire when it decays to threshold.
      static bool sawSpin = false;
      if (spinConfirmed) sawSpin = true;
      if (sawSpin && fabs(omega) <= TAKEOVER_REV_S && fabs(omega) > 0.08f) {
        sawSpin = false;
        beginTakeover();              // -> RUN_TAKEOVER
      }
    } break;

    case RUN_TAKEOVER:
      if (millis() - lastLogMs >= 2) { lastLogMs = millis(); logRow("TAKEOVER"); }
      if (!stepper->isRunning()) {
        Serial.println(F("# takeover complete -> SETTLE"));
        lastLogMs = millis();
        mode = SETTLE;
      }
      break;

    case SETTLE: {
      static uint32_t t0 = 0;
      if (t0 == 0) t0 = millis();
      if (millis() - lastLogMs >= 5) { lastLogMs = millis(); logRow("SETTLE"); }
      if (millis() - t0 > SETTLE_MS) {
        t0 = 0;
        Serial.print(F("# LANDED wedge=")); Serial.print(currentWedge());
        Serial.print(F(" target=")); Serial.print(targetWedge);
        Serial.print(F(" err_deg="));
        float err = wheelAngleDeg() - (targetWedge*WEDGE_DEG + 0.45f*WEDGE_DEG);
        Serial.println(err,1);
        driverFreewheel();            // return to free within test
        mode = IDLE_FREE;
      }
    } break;
  }
}
