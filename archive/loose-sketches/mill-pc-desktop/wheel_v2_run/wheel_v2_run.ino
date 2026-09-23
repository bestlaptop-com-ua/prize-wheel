/*
 * wheel_v2_run - continuous slow spin for mechanical bring-up.
 * 400 mA RMS, ~4 s/rev, runs until power off or reflash.
 * Prints encoder + DRV_STATUS once a second so the magnet can be fitted live.
 * Send 's' to stop/start, '+'/'-' to change speed, 'r' to reverse.
 */
#include <Wire.h>
#include <TMCStepper.h>

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

TMC5160Stepper driver(PIN_CS, R_SENSE, PIN_MOSI, PIN_MISO, PIN_SCK);

volatile uint32_t stepUs   = 9000;  // current period (starts very slow)
volatile uint32_t targetUs = 1250;  // cruise period, ~4 s per revolution
bool running = true;
bool dirFwd  = true;
bool decel   = false;
uint32_t stepCount = 0;

static uint16_t encRaw() {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(0x0C);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)2) != 2) return 0xFFFF;
  uint8_t h = Wire.read(), l = Wire.read();
  return ((h & 0x0F) << 8) | l;
}
static uint8_t encReg8(uint8_t r) {
  Wire.beginTransmission(AS5600_ADDR); Wire.write(r);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}
uint8_t agcMin = 255, agcMax = 0;
static uint8_t encStatus() {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(0x0B);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println(F("=== wheel v2 continuous spin (2400 mA SpreadCycle, ~4 s/rev) ==="));
  Serial.println(F("keys: s=stop/start  +/-=faster/slower  r=reverse  d=decel to stop"));

  pinMode(PIN_EN, OUTPUT);   digitalWrite(PIN_EN, HIGH);
  pinMode(PIN_STEP, OUTPUT); digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);  digitalWrite(PIN_DIR, dirFwd ? LOW : HIGH);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  driver.begin();
  delay(20);
  if (driver.version() != 0x30) {
    Serial.println(F("[TMC] NO SPI LINK - not enabling the motor"));
    running = false;
    return;
  }
  driver.GSTAT(0b111);
  driver.toff(4);
  driver.microsteps(MICROSTEPS);
  driver.rms_current(2400);
  driver.en_pwm_mode(false);   // SpreadCycle = full torque
  driver.pwm_autoscale(true);
  Serial.printf("[TMC] ok, %u mA, %u microsteps, vsense=%u irun=%u GCONF=0x%08lX\n", driver.rms_current(), driver.microsteps(), (unsigned)((driver.CHOPCONF() >> 17) & 1), driver.irun(), (unsigned long)driver.GCONF());
  digitalWrite(PIN_EN, LOW);
  Serial.println(F("[RUN] spinning"));
}

void loop() {
  static uint32_t lastReport = 0;

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's') {
      running = !running;
      if (running) stepUs = 9000;   // soft start
      digitalWrite(PIN_EN, running ? LOW : HIGH);
      Serial.println(running ? F("[RUN] started") : F("[RUN] stopped, motor free"));
    } else if (c == '+') {
      if (targetUs > 250) targetUs -= 100;
      Serial.printf("[RUN] stepUs=%lu (%.1f s/rev)\n", (unsigned long)targetUs, targetUs * STEPS_PER_REV / 1e6);
    } else if (c == '-') {
      if (targetUs < 6000) targetUs += 100;
      Serial.printf("[RUN] stepUs=%lu (%.1f s/rev)\n", (unsigned long)targetUs, targetUs * STEPS_PER_REV / 1e6);
    } else if (c == 'm') {
      bool st = !driver.en_pwm_mode();
      driver.en_pwm_mode(st);
      Serial.println(st ? F("[RUN] StealthChop") : F("[RUN] SpreadCycle"));
    } else if (c == 'd') {
      decel = true;
      Serial.println(F("[RUN] decelerating to a stop"));
    } else if (c == 'r') {
      dirFwd = !dirFwd;
      digitalWrite(PIN_DIR, dirFwd ? LOW : HIGH);
      Serial.println(dirFwd ? F("[RUN] direction FWD") : F("[RUN] direction REV"));
    }
  }

  if (running) {
    digitalWrite(PIN_STEP, HIGH); delayMicroseconds(4);
    digitalWrite(PIN_STEP, LOW);  delayMicroseconds(stepUs);
    stepCount++;
    if (!decel && stepUs != targetUs) {
      if (stepUs > targetUs) {
        uint32_t n = (uint32_t)(stepUs / 1.00018f);
        stepUs = (n < targetUs || n >= stepUs) ? targetUs : n;
      } else {
        uint32_t n = (uint32_t)(stepUs * 1.00018f) + 1;
        stepUs = (n > targetUs) ? targetUs : n;
      }
    }
    if (decel) {
      stepUs = (uint32_t)(stepUs * 1.0006f) + 1;
      if (stepUs > 9000) {
        decel = false; running = false;
        Serial.println(F("[RUN] stopped (still energised - press s to release)"));
      }
    }
  }

  if (millis() - lastReport >= 1000) {
    lastReport = millis();
    uint8_t st = encStatus();
    uint8_t agc = encReg8(0x1A);
    if (agc < agcMin) agcMin = agc;
    if (agc > agcMax) agcMax = agc;
    uint16_t raw = encRaw();
    uint32_t s = driver.DRV_STATUS();
    Serial.printf("t=%lus steps=%lu (%.2f rev)  ENC raw=%u MD=%d ML=%d MH=%d AGC=%u(%u-%u)  CS=%lu ot=%lu ola=%lu olb=%lu\n",
                  (unsigned long)(millis() / 1000), (unsigned long)stepCount,
                  (double)stepCount / STEPS_PER_REV, raw,
                  (st >> 5) & 1, (st >> 4) & 1, (st >> 3) & 1, agc, agcMin, agcMax,
                  (unsigned long)((s >> 16) & 0x1F), (unsigned long)((s >> 25) & 1),
                  (unsigned long)((s >> 29) & 1), (unsigned long)((s >> 30) & 1));
    if (((s >> 25) & 1) || ((s >> 26) & 1)) {
      digitalWrite(PIN_EN, HIGH); running = false;
      Serial.println(F("[FAULT] over-temperature - motor disabled"));
    }
  }
}
