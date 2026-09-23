// Speaker test: S3 I2S -> PCM5102A. BCK 15, LRCK 16, DIN 17. PCM5102A: SCK->GND, XSMT->3V3.
// Cycle: 440 Hz 1 s, 880 Hz 1 s, silence 2 s. Amplitude ~ -18 dBFS to keep the TPA3116 civil.
#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>
I2SClass i2s;
const int SR = 44100;
static int16_t buf[512];
void tone(float hz, uint32_t ms, float amp) {
  static float ph = 0; float dp = 2 * PI * hz / SR;
  uint32_t n = (uint64_t)SR * ms / 1000;
  while (n) {
    size_t frames = min<uint32_t>(n, 256);
    for (size_t i = 0; i < frames; i++) { int16_t v = (int16_t)(sinf(ph) * 32767 * amp); ph += dp; if (ph > 2*PI) ph -= 2*PI; buf[2*i] = v; buf[2*i+1] = v; }
    i2s.write((uint8_t*)buf, frames * 4);
    n -= frames;
  }
}
void setup() {
  Serial.begin(115200); delay(500);
  i2s.setPins(15, 16, 17);
  if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) { Serial.println("I2S init FAILED"); while (1) delay(1000); }
  Serial.println("SPKTEST v3: 440Hz at 0.3/0.1/0.03/0.01/0.003 then silence");
}
void loop() {
  const float amps[] = {0.3f, 0.1f, 0.03f, 0.01f, 0.003f};
  for (float a : amps) { Serial.printf("amp %.3f\n", a); tone(440, 1000, a); }
  Serial.println("silence"); tone(440, 2000, 0.0f);
}
