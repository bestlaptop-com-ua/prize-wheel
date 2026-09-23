/*
 * wheel_v2_spin - GENTLE motor test. Low current, slow, short, then disabled.
 * 400 mA RMS, ~4 s per revolution, 2 revolutions, then EN back HIGH.
 * Reports DRV_STATUS before / during / after so open-load and short flags show.
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

static uint16_t encRaw() {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(0x0C);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)2) != 2) return 0xFFFF;
  uint8_t h = Wire.read(), l = Wire.read();
  return ((h & 0x0F) << 8) | l;
}

static void status(const char *tag) {
  uint32_t s = driver.DRV_STATUS();
  Serial.printf("[%s] DRV_STATUS 0x%08lX  SG=%lu stst=%lu ot=%lu otpw=%lu s2ga=%lu s2gb=%lu ola=%lu olb=%lu\n",
                tag, (unsigned long)s, (unsigned long)(s & 0x3FF), (unsigned long)((s >> 31) & 1),
                (unsigned long)((s >> 25) & 1), (unsigned long)((s >> 26) & 1),
                (unsigned long)((s >> 27) & 1), (unsigned long)((s >> 28) & 1),
                (unsigned long)((s >> 29) & 1), (unsigned long)((s >> 30) & 1));
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println(F("=== wheel v2 gentle spin test (400 mA, ~4 s/rev, 2 rev) ==="));

  pinMode(PIN_EN, OUTPUT);   digitalWrite(PIN_EN, HIGH);   // disabled
  pinMode(PIN_STEP, OUTPUT); digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);  digitalWrite(PIN_DIR, LOW);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  driver.begin();
  delay(20);
  uint8_t ver = driver.version();
  Serial.printf("[TMC] version 0x%02X  IOIN 0x%08lX\n", ver, (unsigned long)driver.IOIN());
  if (ver != 0x30) { Serial.println(F("[TMC] NO SPI LINK - aborting, not touching the motor")); return; }

  driver.GSTAT(0b111);
  driver.toff(4);
  driver.microsteps(MICROSTEPS);
  driver.rms_current(400);        // deliberately low for a first spin
  driver.en_pwm_mode(true);       // StealthChop
  driver.pwm_autoscale(true);
  Serial.printf("[TMC] toff=%u microsteps=%u rms_current=%u mA\n",
                driver.toff(), driver.microsteps(), driver.rms_current());
  status("pre ");

  uint16_t e0 = encRaw();
  Serial.printf("[ENC] before: raw=%u\n", e0);

  Serial.println(F("[RUN] enabling and stepping..."));
  digitalWrite(PIN_EN, LOW);      // active low -> enabled
  delay(50);

  const uint32_t us = 1250;       // ~800 steps/s -> ~4 s per revolution
  for (long i = 0; i < STEPS_PER_REV * 2; i++) {
    digitalWrite(PIN_STEP, HIGH); delayMicroseconds(4);
    digitalWrite(PIN_STEP, LOW);  delayMicroseconds(us);
    if (i == STEPS_PER_REV / 2) status("mid ");
  }

  status("post");
  uint16_t e1 = encRaw();
  Serial.printf("[ENC] after: raw=%u  (delta %d)\n", e1, (int)e1 - (int)e0);

  digitalWrite(PIN_EN, HIGH);     // disabled again
  driver.toff(0);
  Serial.println(F("[RUN] done - driver disabled, motor free"));
  Serial.println(F("If the shaft did not turn: check 24 V at VM, the four motor wires, and phase pairing (A1/A2 one coil, B1/B2 the other)."));
}

void loop() {}
