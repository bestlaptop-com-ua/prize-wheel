// LED strip test for the 36 in prize wheel (2026-09-23, Timur: "Led does not work. Flash led test").
// Same wiring as the wheel firmware: GPIO40 -> 74AHCT125 -> WS2815 DIN, 300 LEDs, GRB.
// The motor stays OFF the whole time (TMC5160 EN held high): the wheel is not steered while this runs.
// Serial 115200:  a auto cycle (default)   r g b w  solid colour   c chase   e ends (first/mid/last)
//                 0 all off   + / -  brightness   ?  status
#include <FastLED.h>
#include <esp_system.h>

#define PIN_EN   7    // TMC5160 enable, active LOW: held HIGH = outputs off
#define PIN_STEP 5
#define PIN_DIR  6
#define TMC_CS   10
#define LED_PIN  40
#define NUM_LEDS 300

CRGB leds[NUM_LEDS];
uint8_t bright = 64;          // 25 %
char mode = 'a';
uint8_t autoStep = 0;
uint32_t stepAtMs = 0, frames = 0, lastReportMs = 0;
uint16_t chasePos = 0;

const char* stepName(uint8_t s) {
  static const char* names[] = {"RED", "GREEN", "BLUE", "WHITE", "CHASE", "ENDS (first red, middle green, last blue)"};
  return names[s % 6];
}

void render(char m, uint8_t s) {
  switch (m) {
    case 'r': fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0)); break;
    case 'g': fill_solid(leds, NUM_LEDS, CRGB(0, 255, 0)); break;
    case 'b': fill_solid(leds, NUM_LEDS, CRGB(0, 0, 255)); break;
    case 'w': fill_solid(leds, NUM_LEDS, CRGB(160, 160, 160)); break;
    case 'c':
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      for (int k = 0; k < 10; ++k) leds[(chasePos + k) % NUM_LEDS] = CRGB(255, 80, 0);
      chasePos = (chasePos + 3) % NUM_LEDS;
      break;
    case 'e':
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      for (int k = 0; k < 3; ++k) {
        leds[k] = CRGB(255, 0, 0);
        leds[NUM_LEDS / 2 - 1 + k] = CRGB(0, 255, 0);
        leds[NUM_LEDS - 1 - k] = CRGB(0, 0, 255);
      }
      break;
    case '0': fill_solid(leds, NUM_LEDS, CRGB::Black); break;
    case 'a': {
      static const char seq[] = {'r', 'g', 'b', 'w', 'c', 'e'};
      render(seq[s % 6], s);
      break;
    }
  }
}

void status() {
  Serial.printf("# LEDTEST mode=%c step=%s bright=%u frames/s=%lu reset=%d heap=%u\n",
                mode, mode == 'a' ? stepName(autoStep) : "-", bright,
                (unsigned long)frames, (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap());
}

void setup() {
  pinMode(PIN_EN, OUTPUT);   digitalWrite(PIN_EN, HIGH);    // motor off first
  pinMode(PIN_STEP, OUTPUT); digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);  digitalWrite(PIN_DIR, LOW);
  pinMode(TMC_CS, OUTPUT);   digitalWrite(TMC_CS, HIGH);    // SPI driver deselected
  Serial.begin(115200);
  delay(300);
  Serial.println(F("# build: led-test-20260923 (motor OFF, wheel not steered; flash the wheel firmware back after the test)"));
  Serial.printf("# reset reason %d (%s)\n", (int)esp_reset_reason(),
                esp_reset_reason() == ESP_RST_BROWNOUT ? "BROWNOUT" : "ok");
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(bright);
  FastLED.clear(true);
  Serial.printf("# LEDTEST WS2812-protocol x%d on GPIO%d, GRB. Commands: a r g b w c e 0 + - ?\n", NUM_LEDS, LED_PIN);
  stepAtMs = millis();
  Serial.printf("# LEDTEST step %s\n", stepName(autoStep));
}

void loop() {
  const uint32_t now = millis();
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '+' || ch == '-') {
      bright = (ch == '+') ? (uint8_t)min(255, bright + 32) : (uint8_t)max(8, bright - 32);
      FastLED.setBrightness(bright);
      status();
    } else if (ch == '?') {
      status();
    } else if (strchr("argbwce0", ch)) {
      mode = ch;
      if (mode == 'a') { autoStep = 0; stepAtMs = now; }
      Serial.printf("# LEDTEST mode %c\n", mode);
    }
  }
  if (mode == 'a' && now - stepAtMs >= 3000) {
    stepAtMs = now;
    autoStep = (autoStep + 1) % 6;
    Serial.printf("# LEDTEST step %s\n", stepName(autoStep));
  }
  render(mode, autoStep);
  FastLED.show();
  ++frames;
  if (now - lastReportMs >= 10000) {
    frames = frames / 10;
    status();
    frames = 0;
    lastReportMs = now;
  }
  delay(20);
}
