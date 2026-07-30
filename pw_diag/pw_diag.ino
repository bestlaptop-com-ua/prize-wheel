/*
 * pw_diag v3 - NVS-free, reboot-proof wheel frame.
 * Anchor: RAW_SCREW = encoder reading with the rim screw under the
 * pointer (physical zero). A constant of the mounting; survives reboots.
 * Prints BOTH direction candidates until the sign is confirmed:
 *   A+ = norm(rawDeg - screwDeg)   [raw increases with wheel angle]
 *   A- = norm(screwDeg - rawDeg)   [raw decreases with wheel angle]
 * Boot burst-read instruments corrupted-first-read; NVS shown for monitoring only.
 */
#include <Wire.h>
#include <Preferences.h>
#include <math.h>

#define PIN_SDA 21
#define PIN_SCL 22
#define AS5600_ADDR 0x36
#define AS5600_RAW  0x0C
#define NUM_WEDGES 12
static const float    WEDGE_DEG = 360.0f / NUM_WEDGES;
static const uint16_t RAW_SCREW = 3807;   // wheel parked at rim-screw zero, re-anchored 2026-07-28

Preferences preferences;
uint32_t i2cErrors = 0, lastPrintMs = 0, lastStatusMs = 0;
uint16_t lastRaw = 0;
bool     haveRaw = false;
double   nvsWedge0 = NAN;
bool     nvsOk = false;

bool readRaw(uint16_t &raw) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(AS5600_RAW);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)2) != 2) return false;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  raw = ((hi << 8) | lo) & 0x0FFF;
  return true;
}

double norm360(double a) { a = fmod(a, 360.0); if (a < 0) a += 360.0; return a; }

void printStatus() {
  Serial.printf("# pw_diag v3  screwRaw=%u nvsOk=%d nvsWedge0=%.2f i2cErr=%lu\n",
                RAW_SCREW, nvsOk ? 1 : 0, nvsWedge0, (unsigned long)i2cErrors);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  nvsOk = preferences.begin("prizewheel", false);  // monitoring only; never writes
  nvsWedge0 = preferences.getDouble("wedge0", NAN);
  preferences.end();
  char buf[96]; int n = snprintf(buf, sizeof(buf), "# bootRaw:");
  for (int i = 0; i < 8; i++) {
    uint16_t r;
    if (readRaw(r)) n += snprintf(buf + n, sizeof(buf) - n, " %u", r);
    else            n += snprintf(buf + n, sizeof(buf) - n, " ERR");
    delay(3);
  }
  Serial.println(buf);
  printStatus();
}

void loop() {
  uint16_t raw;
  if (readRaw(raw)) { lastRaw = raw; haveRaw = true; }
  else i2cErrors++;

  uint32_t now = millis();
  if (haveRaw && now - lastPrintMs >= 100) {
    lastPrintMs = now;
    double rd = lastRaw * 360.0 / 4096.0;
    double sd = RAW_SCREW * 360.0 / 4096.0;
    double ap = norm360(rd - sd);
    double am = norm360(sd - rd);
    Serial.printf("raw %4u | A+ %6.2f w+ %2d | A- %6.2f w- %2d\n",
                  lastRaw, ap, (int)(ap / WEDGE_DEG) % NUM_WEDGES,
                  am, (int)(am / WEDGE_DEG) % NUM_WEDGES);
  }
  if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    printStatus();
  }
  delay(5);
}

