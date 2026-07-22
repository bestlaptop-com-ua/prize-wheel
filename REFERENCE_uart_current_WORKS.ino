/* uart_current.ino - verify TMC2209 UART + set motor current in SOFTWARE.
 * This OVERRIDES the Vref pot. Requires UART wired:
 *   ESP32 TX2(GPIO17) -> 1k -> driver RX pad
 *   ESP32 RX2(GPIO16) -> driver RX pad (direct)
 *   driver TX unconnected
 *
 * Serial 115200:
 *   c  print UART connection test + IFCNT (0 = OK)
 *   1  spin slow (0.25 wheel rev/s)
 *   2  spin fast (1.0 wheel rev/s)
 *   r  reverse
 *   s  ramp to stop and HOLD
 *   0  release (disable, wheel free)
 *   +  raise current 100mA   -  lower current 100mA   (live)
 */
#include <TMCStepper.h>
#include <FastAccelStepper.h>

#define TMC_SERIAL   Serial2
#define TMC_RX_PIN   16
#define TMC_TX_PIN   17
#define TMC_ADDR     0b00
#define R_SENSE      0.11f

#define PIN_EN   25
#define PIN_STEP 26
#define PIN_DIR  27

const float USTEPS_PER_WHEEL_REV = 200.0f * 16.0f * 2.0f; // 6400 (2:1 belt)
uint16_t current_mA = 1450;   // 85% of your 1.7A motor. Software-set.
uint32_t accel_sps2 = 1000;   // GENTLE ramp for high-inertia 24" wheel (was 4000). Live-adjust with a+/a-.

TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, TMC_ADDR);
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper* st = nullptr;
int dir = +1;

void applyCurrent() {
  driver.rms_current(current_mA, 1.0);   // holdMult=1.0 -> IHOLD == IRUN (strong hold)
  driver.TCOOLTHRS(0);
  driver.semin(0);
  Serial.print("# current set to "); Serial.print(current_mA); Serial.println(" mA RMS");
}

void uartTest() {
  uint8_t res = driver.test_connection();   // 0 = OK
  Serial.print("# test_connection (0=OK): "); Serial.println(res);
  Serial.print("# IFCNT (increments on writes): "); Serial.println(driver.IFCNT());
  Serial.print("# GCONF: 0x"); Serial.println(driver.GCONF(), HEX);
  Serial.print("# IRUN (CS 0-31): "); Serial.println(driver.irun());
  Serial.print("# vsense (0=full,1=high-sens): "); Serial.println(driver.vsense());
  Serial.print("# cs_actual (running CS): "); Serial.println(driver.cs_actual());
  Serial.print("# I_scale_analog (want 0): "); Serial.println(driver.I_scale_analog());
  if (res != 0) Serial.println("# >>> UART NOT WORKING - current is set by POT, not software. Check TX/1k/RX wiring.");
}

void applyRun(float wheelRevS) {
  uint32_t hz = (uint32_t)(wheelRevS * USTEPS_PER_WHEEL_REV);
  digitalWrite(PIN_EN, LOW);
  driver.ihold(16);               // restore hold current if a soft-release lowered it
  st->setSpeedInHz(hz);
  st->setAcceleration(accel_sps2);
  if (dir > 0) st->runForward(); else st->runBackward();
  delay(50);
  Serial.print("# run "); Serial.print(wheelRevS); Serial.print(" rev/s dir "); Serial.print(dir);
  Serial.print("  | IRUN="); Serial.print(driver.irun());
  Serial.print(" cs_actual(running)="); Serial.print(driver.cs_actual());
  Serial.print(" TSTEP="); Serial.print(driver.TSTEP());
  Serial.print(" accel="); Serial.println(accel_sps2);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, LOW);      // ENABLE at boot: rotor held, no energize-snap on first run

  TMC_SERIAL.begin(115200, SERIAL_8N1, TMC_RX_PIN, TMC_TX_PIN);
  driver.begin();
  driver.I_scale_analog(false);   // <<< FIX: use internal Iref, NOT the Vref pot. Without this, rms_current is scaled down by the pot.
  driver.toff(4);
  driver.blank_time(24);
  driver.microsteps(16);
  driver.en_spreadCycle(true);    // SpreadCycle = torque + high RPM (was stealthChop = weak/RPM-limited)
  driver.pwm_autoscale(true);
  driver.rms_current(current_mA, 1.0);   // holdMult=1.0 -> IHOLD == IRUN
  driver.TCOOLTHRS(0);            // CoolStep OFF (explicit)
  driver.semin(0);                // <<< 0 = CoolStep fully disabled
  driver.semax(0);
  driver.iholddelay(0);
  driver.TPOWERDOWN(255);         // slow standstill power-down (keep torque at rest)

  engine.init();
  st = engine.stepperConnectToPin(PIN_STEP);
  st->setDirectionPin(PIN_DIR);
  st->setAutoEnable(false);

  Serial.println("\n=== UART CURRENT TEST ===");
  Serial.println("c=uarttest 1=slow 2=fast r=rev s=stop&hold 0=release +/-=current");
  uartTest();
  applyCurrent();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 'c': uartTest(); break;
    case '1': applyRun(0.25f); break;
    case '2': applyRun(1.0f); break;
    case 'r': dir = -dir; Serial.print("# dir="); Serial.println(dir);
              if (st->isRunning()) { st->setAcceleration(accel_sps2); if(dir>0) st->runForward(); else st->runBackward(); } break;
    case 's': st->setAcceleration(accel_sps2); st->stopMove(); Serial.println("# stop&hold (gentle)"); break;
    case '0': st->forceStop(); driver.ihold(2); Serial.println("# soft release (low hold, still energized - no snap next run)"); break;
    case 'F': st->forceStop(); digitalWrite(PIN_EN, HIGH); Serial.println("# FULL release (disabled, wheel free, WILL snap next run)"); break;
    case '+': if (current_mA < 1700) current_mA += 100; applyCurrent(); break;
    case '-': if (current_mA > 300)  current_mA -= 100; applyCurrent(); break;
    case 'A': if (accel_sps2 < 20000) accel_sps2 += 250; Serial.print("# accel=");Serial.println(accel_sps2); break;
    case 'Z': if (accel_sps2 > 250)   accel_sps2 -= 250; Serial.print("# accel=");Serial.println(accel_sps2); break;
    case 't': { static bool sc = true; sc = !sc; driver.en_spreadCycle(sc);
                Serial.print("# mode = "); Serial.println(sc ? "SpreadCycle (torque)" : "StealthChop (quiet)"); } break;
    default: break;
  }
}
