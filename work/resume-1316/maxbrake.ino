/*
 * wheel_v2_maxbrake - bare capture-and-stop test. No wedges, no friction
 * model, no targeting, no error watchdog. Detect a real hand-spin,
 * jump-start the field to match its measured speed, hold briefly to
 * confirm real coupling, then brake at maximum current with a decel rate
 * scaled to the wheel's actual size (targets ~3s to stop regardless of
 * entry speed, rather than a fixed rate). After stopping, stays LOCKED
 * at full current - does not release - until 'n' is sent to arm the
 * next spin. Reuses the production sketch's NVS direction calibration
 * (namespace "prizewheel", keys "pos_sign"/"dir_ok").
 */
#include <Wire.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include <Preferences.h>

#define PIN_SDA 38
#define PIN_SCL 39
#define PIN_SCK 12
#define PIN_MOSI 11
#define PIN_MISO 13
#define PIN_CS  10
#define PIN_STEP 5
#define PIN_DIR  6
#define PIN_EN   7
#define R_SENSE 0.075f
#define MICROSTEPS 16
#define STEPS_PER_REV (200L * MICROSTEPS)
#define AS5600_ADDR 0x36
#define MAX_CURRENT_MA 2300      // near the driver's ~2385 mA hard ceiling
#define SPIN_ARM_REV_S 0.10f     // minimum speed to call it "a real spin"
#define TARGET_STOP_S 3.0f       // brake scaled to actual captured speed

TMC5160Stepper driver(PIN_CS, R_SENSE, PIN_MOSI, PIN_MISO, PIN_SCK);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;
Preferences prefs;

int posSign = 0;   // +1 or -1, from NVS; 0 = uncalibrated

static uint16_t encRaw() {
  Wire.beginTransmission(AS5600_ADDR); Wire.write(0x0C);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)2) != 2) return 0xFFFF;
  uint8_t h = Wire.read(), l = Wire.read();
  return ((h & 0x0F) << 8) | l;
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println(F("=== bare capture-and-stop test: no wedges, no friction model, no error latch ==="));

  pinMode(PIN_EN, OUTPUT);   digitalWrite(PIN_EN, HIGH);
  pinMode(PIN_STEP, OUTPUT); digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  prefs.begin("prizewheel", true);   // read-only, reuse production calibration
  posSign = prefs.getInt("pos_sign", 0);
  bool dirOk = prefs.getBool("dir_ok", false);
  prefs.end();
  Serial.printf("[CAL] posSign=%d dirOk=%d (from production NVS)\n", posSign, dirOk);
  if (posSign != 1 && posSign != -1) {
    Serial.println(F("[CAL] no valid direction calibration found - aborting"));
    while (1) delay(1000);
  }

  driver.begin();
  delay(20);
  if (driver.version() != 0x30) {
    Serial.println(F("[TMC] NO SPI LINK - aborting"));
    while (1) delay(1000);
  }
  driver.GSTAT(0b111);
  driver.toff(4);
  driver.microsteps(MICROSTEPS);
  driver.en_pwm_mode(false);      // SpreadCycle
  driver.pwm_autoscale(true);
  driver.rms_current(200, 1.0);   // idle low; raised at capture
  Serial.println(F("[TMC] ok"));

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP, DRIVER_MCPWM_PCNT);
  if (!stepper) { Serial.println(F("[FAS] connect failed - aborting")); while (1) delay(1000); }
  stepper->setDirectionPin(PIN_DIR);
  stepper->setEnablePin(PIN_EN, true);
  stepper->setAutoEnable(false);
  stepper->setAcceleration(4000);

  Serial.println(F("=== waiting for a real hand spin ==="));
}

static float readOmegaOnce() {
  static uint16_t lastRaw = 0xFFFF;
  static uint32_t lastUs = 0;
  uint16_t raw = encRaw();
  uint32_t nowUs = micros();
  if (raw == 0xFFFF) return 0.0f;
  float omega = 0.0f;
  if (lastRaw != 0xFFFF && lastUs != 0) {
    int16_t d = (int16_t)raw - (int16_t)lastRaw;
    if (d > 2048) d -= 4096;
    if (d < -2048) d += 4096;
    uint32_t dtUs = nowUs - lastUs;
    if (dtUs > 0 && dtUs < 200000UL) {
      omega = ((float)d / 4096.0f) * (1000000.0f / (float)dtUs);
    }
  }
  lastRaw = raw; lastUs = nowUs;
  return omega;
}

void loop() {
  // ---- phase 1: wait for a real spin, tracking a short rolling estimate ----
  float omega = 0.0f;
  uint32_t armedSinceMs = 0;
  Serial.println(F("[WAIT] spin the wheel now"));
  while (true) {
    omega = readOmegaOnce();
    if (fabsf(omega) > SPIN_ARM_REV_S) {
      if (armedSinceMs == 0) armedSinceMs = millis();
      if (millis() - armedSinceMs > 150) break;   // sustained, not a twitch
    } else {
      armedSinceMs = 0;
    }
    delay(2);
  }

  // ---- phase 2: settle a clean speed estimate over a short window ----
  float sum = 0; int n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 120) {
    float o = readOmegaOnce();
    if (fabsf(o) > 0.02f) { sum += o; n++; }
    delay(2);
  }
  float capturedOmega = n > 0 ? sum / n : omega;
  int dir = (capturedOmega * (float)posSign > 0) ? 1 : -1;
  float speed = fabsf(capturedOmega);
  Serial.printf("[CAPTURE] omega=%.3f rev/s  dir=%d  (posSign=%d)\n", capturedOmega, dir, posSign);

  // ---- phase 3: jump-start the field to match, hold briefly, then brake ----
  uint32_t hz = (uint32_t)(speed * STEPS_PER_REV);
  if (hz < 40) { Serial.println(F("[SKIP] too slow to bother catching")); delay(500); return; }

  stepper->setDirectionPin(PIN_DIR, dir > 0);
  stepper->setCurrentPosition(0);
  driver.rms_current(MAX_CURRENT_MA, 1.0);
  digitalWrite(PIN_EN, LOW);
  stepper->enableOutputs();

  // Decel scaled to the actual captured speed: targets TARGET_STOP_S seconds
  // to stop regardless of entry speed, instead of a fixed rate that would
  // stop a slow spin fast and a fast spin slow.
  uint32_t brakeSps2 = (uint32_t)((speed / TARGET_STOP_S) * STEPS_PER_REV);
  if (brakeSps2 < 50) brakeSps2 = 50;
  uint32_t jumpStep = (uint32_t)(((float)hz * (float)hz) / (2.0f * (float)brakeSps2));
  stepper->setAcceleration(brakeSps2);
  stepper->setSpeedInHz(hz);
  stepper->setJumpStart(jumpStep);
  if (dir > 0) stepper->runForward(); else stepper->runBackward();
  uint32_t captureMs = millis();
  Serial.printf("[BRAKE] jump-start hz=%lu jumpStep=%lu, current=%u mA, decel=%lu sps2 (target %.1fs) - GO\n",
                (unsigned long)hz, (unsigned long)jumpStep, MAX_CURRENT_MA,
                (unsigned long)brakeSps2, TARGET_STOP_S);

  delay(300);   // hold the matched speed briefly - confirms real coupling before braking
  stepper->stopMove();   // NOW ramp down at brakeSps2, from an established speed

  uint32_t lastLogMs = 0;
  while (stepper->isRunning() && millis() - captureMs < 15000) {   // generous - no premature abort
    if (millis() - lastLogMs >= 20) {
      lastLogMs = millis();
      float w = readOmegaOnce();
      int32_t fasHz = stepper->getCurrentSpeedInMilliHz(true) / 1000;
      Serial.printf("t=%4lums  wheel=%+7.3f rev/s  fas=%+6ld Hz (%.3f rev/s)\n",
                    (unsigned long)(millis() - captureMs), w,
                    (long)fasHz, (float)fasHz / STEPS_PER_REV);
    }
  }

  // Hold locked at full current - do NOT release; only an explicit
  // 'n' arms the next spin. Deliberate, not a bug.
  Serial.printf("[DONE] total %lu ms, final wheel omega=%.3f rev/s - LOCKED at %u mA\n",
                (unsigned long)(millis() - captureMs), readOmegaOnce(), MAX_CURRENT_MA);
  Serial.println(F("send 'n' to release and arm for the next spin"));
  int32_t lockStartCounts = 0;
  { uint16_t r = encRaw(); lockStartCounts = (r == 0xFFFF) ? 0 : (int32_t)r; }
  uint32_t lockMs = millis();
  uint32_t lastHoldLogMs = 0;
  while (true) {
    if (Serial.available() && Serial.read() == 'n') break;
    if (millis() - lastHoldLogMs >= 250) {
      lastHoldLogMs = millis();
      uint16_t r = encRaw();
      if (r != 0xFFFF) {
        int32_t d = (int32_t)r - lockStartCounts;
        if (d > 2048) d -= 4096; if (d < -2048) d += 4096;
        Serial.printf("[HOLD] t=%5lums  drift=%+6.2f deg since lock\n", (unsigned long)(millis()-lockMs), (float)d * 360.0f / 4096.0f);
      }
    }
    delay(20);
  }
  digitalWrite(PIN_EN, HIGH);
  stepper->disableOutputs();
  driver.rms_current(200, 1.0);
  Serial.println(F("=== released - waiting for the next spin ==="));
}
