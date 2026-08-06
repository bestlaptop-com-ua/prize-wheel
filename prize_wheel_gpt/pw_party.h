/* ============================================================================
 * pw_party.h - party-night additions: WiFi + FX + sanctioned fixes S1/S2/S3
 *
 * Included ONCE near the top of prize_wheel_gpt.ino, BEFORE the sketch body.
 * The implementations live in pw_party_impl.h, included at the very bottom of
 * the sketch, so they can see every control-core global and function without
 * any change to the frozen control logic.  See PARTY_TASK.md, WIFI_TASK.md,
 * FX_TASK.md, LED_HANDOFF.md and DELIVERY.md.
 *
 * Design rules honored here:
 *  - The 12-state machine and all control constants are untouched.  FX derives
 *    every event by OBSERVING state/spin globals once per loop pass; no event
 *    emission calls were inserted into control code.
 *  - FX + WiFi are fail-silent: no error in this module may fault the wheel.
 *  - All network and FX work in the control loop is non-blocking and budgeted;
 *    a micros() max-tracker (command 't') proves the <=2 ms rule.
 *  - LED rendering runs in a FreeRTOS task pinned to core 0 (FastLED.show()
 *    blocks ~10.6 ms at 300 LEDs - measured, LED_HANDOFF.md); the control loop
 *    keeps core 1 and exchanges volatile scalars only.
 * ========================================================================== */
#ifndef PW_PARTY_H
#define PW_PARTY_H

#include <WiFi.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <FastLED.h>

/* ------------------- owner-facing feature switches ----------------------- */
/* Sanctioned fixes (PARTY_TASK.md), each independently disableable.         */
#define PW_S1_ENABLE 1   /* persist the fault latch in NVS across power cycles */
#define PW_S2_ENABLE 1   /* periodic TMC config verify while IDLE (5 s)        */
#define PW_S3_ENABLE 1   /* fitRej= + fit counts appended to SPIN SUMMARY      */

#define PW_WIFI_ENABLE 1 /* SoftAP + telnet mirror/commands (WIFI_TASK.md)     */
#define PW_FX_AUDIO_ENABLE 1 /* DFPlayer Mini on UART1 (see README wiring)     */
#define PW_FX_LED_ENABLE   1 /* WS2812B helix on GPIO4 (LED_HANDOFF.md)        */

/* ------------------------------ WiFi ------------------------------------- */
#define PW_WIFI_PASSWORD  "spinthewheel"  /* CHANGE BEFORE THE PARTY           */
#define PW_WIFI_CHANNEL   6
#define PW_WIFI_HIDDEN    0               /* 1 = hidden SSID                   */
#define PW_WIFI_MAX_CLIENTS 2
#define PW_TELNET_PORT    23
/* 0 = telnet becomes telemetry-only (inbound bytes ignored).  Commands from a
 * phone include z/e/F, which can re-anchor the frame or disable takeover: if
 * the WPA2 password may have leaked to guests, set 0 and reflash.            */
#define PW_TELNET_COMMANDS 1
#define PW_RING_BYTES     4096            /* serial mirror ring (RAM)          */
#define PW_CATCHUP_BYTES  2048            /* replayed to a fresh client        */
#define PW_NET_CHUNK      512             /* max TX bytes per client per pass  */
#define PW_NET_HEARTBEAT_MS 10000
#define PW_WIFI_RETRY_MS  30000
#define PW_WIFI_MAX_RETRIES 5

/* --------------------------- DFPlayer audio ------------------------------ */
/* FX_TASK.md says "UART2 16/17" but in THIS firmware UART2 16/17 IS the      */
/* TMC2209 UART (TMC_SERIAL/TMC_RX_PIN/TMC_TX_PIN).  The DFPlayer therefore   */
/* goes on UART1 mapped to the pins LED_HANDOFF.md reserved for it.  See the  */
/* README wiring section and RISK_AUDIT.md.                                   */
#define PW_DFP_TX_PIN 32       /* ESP32 TX -> 1 kOhm series -> DFPlayer RX    */
#define PW_DFP_RX_PIN 33       /* DFPlayer TX -> ESP32 RX (optional, unread)  */
#define PW_DFP_BAUD   9600
#define PW_DFP_VOLUME 20       /* 0..30; live-adjust with V<n> + Enter        */
#define PW_DFP_CMD_GAP_MS 120  /* global command rate limit (FX_TASK.md)      */
#define PW_DFP_BOOT_DELAY_MS 2500
#define PW_FX_IDLE_AMBIENCE 0  /* 1 = loop track 5 while idle (optional)      */
/* /mp3 track numbers (media/ in this repo; copy /mp3 to the microSD root)    */
#define PW_TRK_TICK     1
#define PW_TRK_RATCHET  2
#define PW_TRK_DRUMROLL 3      /* asset provided; unused by default logic     */
#define PW_TRK_FANFARE  4
#define PW_TRK_AMBIENCE 5
#define PW_TRK_GUEST    6
/* ratchet <-> tick crossover with hysteresis (FX_TASK.md: 0.5 +/- 0.05)      */
#define PW_FX_RATCHET_ON_REV_S  0.55f
#define PW_FX_RATCHET_OFF_REV_S 0.45f
#define PW_FX_LANDED_PAUSE_MS   400

/* ------------------------------ WS2812B ---------------------------------- */
#define PW_LED_PIN    4        /* verified 3.3 V direct drive (LED_HANDOFF)   */
#define PW_NUM_LEDS   300      /* measured; helix around the pole             */
#define PW_LED_ORDER  GRB
#define PW_FX_MAX_MA  3000     /* buck headroom for TMC logic + DFPlayer      */
#define PW_LED_FPS    50
#define PW_LED_STANDBY_BRIGHT   55
#define PW_LED_CELEBRATE_BRIGHT 210
#define PW_LED_CELEBRATE_MS     3000
#define PW_LED_BAND_SPEED       1.6f  /* untuned-at-speed constant (doc)      */

/* --------------------- sanctioned-fix helper macros ----------------------- */
#define PW_S1_NVS_KEY "fltLatch"
#if PW_S1_ENABLE
#define PW_S1_PERSIST(code) preferences.putUChar(PW_S1_NVS_KEY, (uint8_t)(code))
#define PW_S1_CLEAR()       preferences.putUChar(PW_S1_NVS_KEY, 0)
#else
#define PW_S1_PERSIST(code) do {} while (0)
#define PW_S1_CLEAR()       do {} while (0)
#endif

#if PW_S3_ENABLE
static uint32_t fitRejectCount = 0;   /* explicit FRICTION_REJECT events      */
#define PW_S3_COUNT_REJECT() (++fitRejectCount)
#else
#define PW_S3_COUNT_REJECT() do {} while (0)
#endif

/* ----------------------- party module entry points ------------------------ */
void pwPartyBegin();                    /* end of setup()                      */
void pwPartyService(uint32_t loopStartUs); /* end of loop(); self-budgeted     */
bool pwPartyCommandChar(char c);        /* true = consumed (t/a/l/w/V...)      */
bool pwS2ReconfigVerify();              /* 'r' path: re-apply + verify config  */
void pwPartyHelpLines();                /* appended to help()                  */

/* ----------------------- serial mirror (WIFI_TASK) ------------------------ */
/* Every existing print in the sketch goes through ONE path: this Print       */
/* subclass forwards to the real UART0 and appends to a RAM ring that the     */
/* telnet clients drain.  The token substitution below routes all `Serial`    */
/* uses in this translation unit through the mirror without touching any of   */
/* the ~120 call sites.  `Serial1`/`Serial2`/TMC_SERIAL are separate tokens   */
/* and unaffected.  Ring writes are a few instructions; print cost is         */
/* unchanged within measurement error.                                        */
extern char pwRing[PW_RING_BYTES];
extern volatile uint32_t pwRingTotal;   /* monotonic byte count ever written  */

inline void pwRingPut(uint8_t b) {
  pwRing[pwRingTotal % PW_RING_BYTES] = (char)b;
  pwRingTotal = pwRingTotal + 1;
}

class PwMirrorSerial : public Print {
 public:
  void begin(unsigned long baud) { ::Serial.begin(baud); }
  size_t write(uint8_t b) override {
    ::Serial.write(b);
    pwRingPut(b);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t n) override {
    ::Serial.write(buf, n);
    for (size_t i = 0; i < n; ++i) pwRingPut(buf[i]);
    return n;
  }
  int available() { return ::Serial.available(); }
  int read() { return ::Serial.read(); }
  int peek() { return ::Serial.peek(); }
  void flush() { ::Serial.flush(); }
};
extern PwMirrorSerial PwSerial;

/* All `Serial` tokens below this line (entire sketch + impl) use the mirror. */
#define Serial PwSerial

#endif /* PW_PARTY_H */
