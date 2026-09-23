// Strip test v2. Boot: 30 s DC test on GPIO 40 (HIGH 3 s / LOW 3 s) for meter checks, then color cycle.
#include <FastLED.h>
#define LED_PIN 40
#define NUM_LEDS 300
CRGB leds[NUM_LEDS];
uint32_t t0;
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("STRIPTEST v2: DC test 30s, then colors");
  pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_PIN, HIGH); Serial.println("DC HIGH (pin3 should read 5V)"); delay(3000);
    digitalWrite(LED_PIN, LOW);  Serial.println("DC LOW  (pin3 should read 0V)"); delay(3000);
  }
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(96);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  Serial.println("color cycle: red/green/blue/chase/rainbow 3s each");
  t0 = millis();
}
void loop() {
  uint32_t s = (millis() - t0) / 1000;
  uint8_t phase = (s / 3) % 5;
  static uint8_t last = 255;
  if (phase != last) { last = phase; Serial.printf("phase %u\n", phase); }
  if (phase == 0)      fill_solid(leds, NUM_LEDS, CRGB(255,0,0));
  else if (phase == 1) fill_solid(leds, NUM_LEDS, CRGB(0,255,0));
  else if (phase == 2) fill_solid(leds, NUM_LEDS, CRGB(0,0,255));
  else if (phase == 3) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    int p = (millis() / 10) % NUM_LEDS;
    for (int k = 0; k < 8; k++) leds[(p + k) % NUM_LEDS] = CRGB::White;
  } else fill_rainbow(leds, NUM_LEDS, (millis() / 20) & 255, 2);
  FastLED.show();
  delay(10);
}
