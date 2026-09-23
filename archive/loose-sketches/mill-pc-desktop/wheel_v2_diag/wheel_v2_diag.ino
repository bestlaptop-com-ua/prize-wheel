/*
 * wheel_v2_diag - READ-ONLY bring-up check for the v2 loom.
 * Does NOT move the motor: EN is held HIGH (disabled) the whole time.
 * Prints a one-shot report, then a live AS5600 angle every 500 ms.
 */
#include <Wire.h>
#include <TMCStepper.h>

#define PIN_SDA   38
#define PIN_SCL   39
#define PIN_SCK   12
#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_CS    10
#define PIN_STEP   5
#define PIN_DIR    6
#define PIN_EN     7
#define R_SENSE   0.075f
#define AS5600_ADDR 0x36

TMC5160Stepper driver(PIN_CS, R_SENSE, PIN_MOSI, PIN_MISO, PIN_SCK);

static bool as5600_reg(uint8_t reg, uint8_t n, uint8_t *out) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) out[i] = Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println(F("=== wheel v2 diag (read-only, motor stays disabled) ==="));

  // Motor OFF and kept off.
  pinMode(PIN_EN, OUTPUT);   digitalWrite(PIN_EN, HIGH);   // active low -> HIGH = disabled
  pinMode(PIN_STEP, OUTPUT); digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);  digitalWrite(PIN_DIR, LOW);
  Serial.println(F("[EN ] held HIGH  = driver disabled, motor free"));

  Serial.printf("[MCU] flash %u MB, PSRAM %u bytes, free heap %u\n",
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap());

  // ---- I2C / AS5600 ----
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  delay(50);
  uint8_t found = 0;
  Serial.print(F("[I2C] scan 38/39:"));
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
  }
  if (!found) Serial.print(F(" (nothing)"));
  Serial.println();

  uint8_t b[2];
  if (as5600_reg(0x0B, 1, b)) {
    uint8_t st = b[0];
    Serial.printf("[ENC] STATUS 0x%02X  MD=%d ML=%d MH=%d -> %s\n", st,
                  (st >> 5) & 1, (st >> 4) & 1, (st >> 3) & 1,
                  ((st >> 5) & 1) ? (((st >> 4) & 1) || ((st >> 3) & 1) ? "magnet detected but gap is off"
                                                                        : "magnet OK")
                                  : "NO MAGNET DETECTED");
    if (as5600_reg(0x1A, 1, b)) Serial.printf("[ENC] AGC %u (want roughly mid-scale, 40-200)\n", b[0]);
    if (as5600_reg(0x0C, 2, b)) Serial.printf("[ENC] RAW_ANGLE %u / 4096\n",
                                              (unsigned)(((b[0] & 0x0F) << 8) | b[1]));
  } else {
    Serial.println(F("[ENC] AS5600 NOT RESPONDING at 0x36"));
  }

  // ---- SPI / TMC5160 ----
  driver.begin();
  delay(20);
  uint8_t ver = driver.version();
  uint32_t ioin = driver.IOIN();
  Serial.printf("[TMC] version 0x%02X (expect 0x30)  IOIN 0x%08lX  test_connection=%u\n",
                ver, (unsigned long)ioin, driver.test_connection());
  if (ver == 0x30) {
    driver.GSTAT(0b111);
    Serial.printf("[TMC] GCONF 0x%08lX  DRV_STATUS 0x%08lX\n",
                  (unsigned long)driver.GCONF(), (unsigned long)driver.DRV_STATUS());
    Serial.println(F("[TMC] SPI link OK"));
  } else {
    Serial.println(F("[TMC] NO SPI LINK - check VIO/3V3, CS, the bent CFG pins, and CLK->GND"));
  }

  Serial.println(F("=== live AS5600 angle; spin the shaft by hand ==="));
}

void loop() {
  uint8_t b[2];
  if (as5600_reg(0x0C, 2, b)) {
    uint16_t raw = ((b[0] & 0x0F) << 8) | b[1];
    Serial.printf("raw=%4u  deg=%6.1f\n", raw, raw * 360.0f / 4096.0f);
  } else {
    Serial.println(F("raw=?? (I2C read failed)"));
  }
  delay(500);
}
