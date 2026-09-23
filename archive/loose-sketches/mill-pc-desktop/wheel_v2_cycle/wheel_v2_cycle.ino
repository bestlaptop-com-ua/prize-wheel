/*
 * wheel_v2_cycle - accelerate slowly, cruise, decelerate to a stop,
 * hold 3 s, repeat. Gentle settings while the motor mount is sorted out.
 * Keys: s=pause/resume  +/-=cruise speed  r=reverse  x=abort to free
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

#define US_STOPPED 9000UL          // slowest period before we call it stopped
#define CRUISE_MS  3000UL          // how long to hold cruise speed
#define HOLD_MS    3000UL          // dwell at standstill, energised

TMC5160Stepper driver(PIN_CS, R_SENSE, PIN_MOSI, PIN_MISO, PIN_SCK);

uint32_t stepUs   = US_STOPPED;
uint32_t targetUs = 700;          // cruise ~6.4 s/rev, deliberately slow
bool running = true;
bool dirFwd  = true;
uint32_t stepCount = 0;
uint8_t  phase = 0;                // 0 accel, 1 cruise, 2 decel, 3 hold
uint32_t phaseStart = 0;
uint32_t cycles = 0;

static uint8_t encReg8(uint8_t r) {
  Wire.beginTransmission(AS5600_ADDR); Wire.write(r);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}
static uint16_t encRaw() {
  Wire.beginTransmission(AS5600_ADDR); Wire.write(0x0C);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)2) != 2) return 0xFFFF;
  uint8_t h = Wire.read(), l = Wire.read();
  return ((h & 0x0F) << 8) | l;
}
static const char *phaseName() {
  switch (phase) { case 0: return "accel"; case 1: return "cruise";
                   case 2: return "decel"; default: return "hold "; }
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println(F("=== wheel v2 cycle: accel -> cruise -> decel -> hold 3 s -> repeat ==="));
  Serial.println(F("keys: s=pause/resume  +/-=cruise speed  r=reverse  x=free"));

  pinMode(PIN_EN, OUTPUT);   digitalWrite(PIN_EN, HIGH);
  pinMode(PIN_STEP, OUTPUT); digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);  digitalWrite(PIN_DIR, dirFwd ? LOW : HIGH);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  driver.begin();
  delay(20);
  if (driver.version() != 0x30) {
    Serial.println(F("[TMC] NO SPI LINK - motor not enabled"));
    running = false;
    return;
  }
  driver.GSTAT(0b111);
  driver.toff(4);
  driver.microsteps(MICROSTEPS);
  driver.rms_current(2000);
  driver.en_pwm_mode(false);        // SpreadCycle
  driver.pwm_autoscale(true);
  Serial.printf("[TMC] ok, %u mA, %u microsteps\n", driver.rms_current(), driver.microsteps());
  digitalWrite(PIN_EN, LOW);
  phase = 0; phaseStart = millis();
  Serial.println(F("[CYC] cycle 1 - accel"));
}

void loop() {
  static uint32_t lastReport = 0;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's') {
      running = !running;
      digitalWrite(PIN_EN, running ? LOW : HIGH);
      if (running) { phase = 0; stepUs = US_STOPPED; phaseStart = millis(); }
      Serial.println(running ? F("[CYC] resumed") : F("[CYC] paused, motor free"));
    } else if (c == '+') {
      if (targetUs > 600) targetUs -= 100;
      Serial.printf("[CYC] cruise %lu us (%.1f s/rev)\n", (unsigned long)targetUs,
                    targetUs * STEPS_PER_REV / 1e6);
    } else if (c == '-') {
      if (targetUs < 8000) targetUs += 100;
      Serial.printf("[CYC] cruise %lu us (%.1f s/rev)\n", (unsigned long)targetUs,
                    targetUs * STEPS_PER_REV / 1e6);
    } else if (c == 'r') {
      dirFwd = !dirFwd;
      digitalWrite(PIN_DIR, dirFwd ? LOW : HIGH);
      Serial.println(dirFwd ? F("[CYC] FWD") : F("[CYC] REV"));
    } else if (c == 'x') {
      running = false; digitalWrite(PIN_EN, HIGH);
      Serial.println(F("[CYC] aborted, motor free"));
    }
  }

  if (running) {
    if (phase != 3) {                       // step in accel / cruise / decel
      digitalWrite(PIN_STEP, HIGH); delayMicroseconds(4);
      digitalWrite(PIN_STEP, LOW);  delayMicroseconds(stepUs);
      stepCount++;
    }
    switch (phase) {
      case 0: {                             // accelerate
        uint32_t n = (uint32_t)(stepUs / 1.0015f);
        stepUs = (n <= targetUs || n >= stepUs) ? targetUs : n;
        if (stepUs == targetUs) {
          phase = 1; phaseStart = millis();
          Serial.println(F("[CYC] cruise"));
        }
        break;
      }
      case 1:                               // cruise
        if (millis() - phaseStart >= CRUISE_MS) {
          phase = 2; phaseStart = millis();
          Serial.println(F("[CYC] decel"));
        }
        break;
      case 2: {                             // decelerate
        uint32_t n = (uint32_t)(stepUs * 1.003f) + 1;
        stepUs = n;
        if (stepUs >= US_STOPPED) {
          phase = 3; phaseStart = millis();
          Serial.println(F("[CYC] hold 3 s"));
        }
        break;
      }
      default:                              // hold, still energised
        if (millis() - phaseStart >= HOLD_MS) {
          cycles++;
          phase = 0; stepUs = US_STOPPED; phaseStart = millis();
          Serial.printf("[CYC] cycle %lu - accel\n", (unsigned long)(cycles + 1));
        } else {
          delay(2);
        }
        break;
    }
  }

  if (millis() - lastReport >= 1000) {
    lastReport = millis();
    uint8_t st = encReg8(0x0B), agc = encReg8(0x1A);
    uint32_t s = driver.DRV_STATUS();
    Serial.printf("[%s] cyc=%lu %5lu us  steps=%lu (%.2f rev)  ENC raw=%u MD=%d ML=%d AGC=%u  CS=%lu ot=%lu\n",
                  phaseName(), (unsigned long)cycles, (unsigned long)stepUs,
                  (unsigned long)stepCount, (double)stepCount / STEPS_PER_REV,
                  encRaw(), (st >> 5) & 1, (st >> 4) & 1, agc,
                  (unsigned long)((s >> 16) & 0x1F), (unsigned long)((s >> 25) & 1));
  }
}
