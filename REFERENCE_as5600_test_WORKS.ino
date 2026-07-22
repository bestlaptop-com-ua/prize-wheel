/* as5600_test.ino - verify AS5600 magnetic encoder in isolation.
 * No motor, no driver. Just reads angle + magnet status and prints live.
 *
 * Wiring:
 *   AS5600 SDA -> ESP32 GPIO21 (D21)
 *   AS5600 SCL -> ESP32 GPIO22 (D22)
 *   AS5600 VCC -> 3V3   (NOT 5V)
 *   AS5600 GND -> GND
 *   AS5600 DIR -> GND
 *
 * Serial 115200. Prints ~10x/sec:
 *   raw(0-4095)  angle_deg  wedge(0-11)  MAGNET[status]
 * Turn the wheel by hand and watch angle change smoothly 0..360 and wrap.
 * MAGNET status must read OK. If MH/ML/no-magnet, fix gap/alignment.
 */
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22
#define AS5600_ADDR   0x36
#define REG_RAWANGLE  0x0C   // 12-bit raw (unfiltered) hi/lo
#define REG_STATUS    0x0B   // MD/ML/MH magnet status bits
#define REG_AGC       0x1A   // automatic gain control (gap indicator)
#define REG_MAGNITUDE 0x1B   // CORDIC magnitude

#define NUM_WEDGES 12
const float WEDGE_DEG = 360.0f / NUM_WEDGES;

uint16_t read16(uint8_t reg) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  Wire.requestFrom(AS5600_ADDR, 2);
  if (Wire.available() < 2) return 0xFFFF;
  uint16_t hi = Wire.read(), lo = Wire.read();
  return ((hi << 8) | lo) & 0x0FFF;
}
uint8_t read8(uint8_t reg) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom(AS5600_ADDR, 1);
  if (Wire.available() < 1) return 0xFF;
  return Wire.read();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  // presence check
  Wire.beginTransmission(AS5600_ADDR);
  uint8_t err = Wire.endTransmission();
  Serial.println("\n=== AS5600 TEST ===");
  if (err == 0) Serial.println("# AS5600 responding on I2C 0x36. Turn wheel by hand.");
  else { Serial.print("# NO I2C DEVICE at 0x36 (err="); Serial.print(err);
         Serial.println("). Check SDA=21 SCL=22, 3V3, GND, pull-ups."); }
  Serial.println("# columns: raw  angle_deg  wedge  MAGNET  AGC  mag");
}

void loop() {
  uint16_t raw = read16(REG_RAWANGLE);
  uint8_t  st  = read8(REG_STATUS);
  uint8_t  agc = read8(REG_AGC);
  uint16_t mg  = read16(REG_MAGNITUDE);

  if (raw == 0xFFFF) { Serial.println("# I2C read error"); delay(200); return; }

  float deg = raw * 360.0f / 4096.0f;
  int wedge = (int)(deg / WEDGE_DEG) % NUM_WEDGES;

  // status bits: MD(bit5)=magnet detected, ML(bit4)=too weak, MH(bit3)=too strong
  bool md = st & 0x20, ml = st & 0x10, mh = st & 0x08;
  const char* mag = md ? (ml ? "OK-but-WEAK(raise magnet)" :
                          (mh ? "OK-but-STRONG(lower magnet)" : "OK"))
                       : "NO MAGNET DETECTED";

  Serial.print(raw);        Serial.print('\t');
  Serial.print(deg, 1);     Serial.print("deg\t");
  Serial.print("w");        Serial.print(wedge); Serial.print('\t');
  Serial.print(mag);        Serial.print("\tAGC="); Serial.print(agc);
  Serial.print("\tmag=");   Serial.println(mg);

  delay(100);
}
