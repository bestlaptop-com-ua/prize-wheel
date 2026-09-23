/*
 * wheel_v2_audio - I2S audio test: S3 -> PCM5102A -> TPA3116 -> speaker.
 * Motor is explicitly DISABLED at boot (EN HIGH) so this is safe to flash
 * while the spin sketch is running.
 * Cycle: 1 kHz tone, sweep 200 Hz -> 2 kHz, three clicks, silence. Repeats.
 * Keys: +/- volume, t=tone, s=sweep, c=clicks, q=quiet
 */
#include <ESP_I2S.h>

#define PIN_BCK  15
#define PIN_LRCK 16
#define PIN_DIN  17
#define PIN_EN    7
#define SR 44100

I2SClass i2s;
int amp = 400;            // of 32767
char mode = 'a';           // a = auto cycle

static void writeTone(float hz, uint32_t ms) {
  const int N = 256;
  int16_t buf[N * 2];
  static float phase = 0;
  float dp = 2.0f * PI * hz / SR;
  uint32_t total = (uint32_t)((uint64_t)SR * ms / 1000);
  while (total) {
    int n = total > (uint32_t)N ? N : total;
    for (int i = 0; i < n; i++) {
      int16_t s = (int16_t)(sinf(phase) * amp);
      phase += dp;
      if (phase > 2 * PI) phase -= 2 * PI;
      buf[i * 2] = s; buf[i * 2 + 1] = s;
    }
    i2s.write((uint8_t *)buf, n * 4);
    total -= n;
  }
}

static void writeSweep(float f0, float f1, uint32_t ms) {
  const int N = 256;
  int16_t buf[N * 2];
  float phase = 0;
  uint32_t total = (uint32_t)((uint64_t)SR * ms / 1000), done = 0;
  while (total) {
    int n = total > (uint32_t)N ? N : total;
    for (int i = 0; i < n; i++) {
      float t = (float)(done + i) / ((float)SR * ms / 1000.0f);
      float hz = f0 + (f1 - f0) * t;
      phase += 2.0f * PI * hz / SR;
      if (phase > 2 * PI) phase -= 2 * PI;
      int16_t s = (int16_t)(sinf(phase) * amp);
      buf[i * 2] = s; buf[i * 2 + 1] = s;
    }
    i2s.write((uint8_t *)buf, n * 4);
    done += n; total -= n;
  }
}

static void writeClick() {
  const int N = 441;                  // 10 ms
  int16_t buf[N * 2];
  for (int i = 0; i < N; i++) {
    float env = expf(-(float)i / 60.0f);
    int16_t s = (int16_t)(sinf(2.0f * PI * 1800.0f * i / SR) * amp * env);
    buf[i * 2] = s; buf[i * 2 + 1] = s;
  }
  i2s.write((uint8_t *)buf, N * 4);
}

static void writeSilence(uint32_t ms) {
  const int N = 256;
  int16_t buf[N * 2] = {0};
  uint32_t total = (uint32_t)((uint64_t)SR * ms / 1000);
  while (total) {
    int n = total > (uint32_t)N ? N : total;
    i2s.write((uint8_t *)buf, n * 4);
    total -= n;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println(F("=== wheel v2 audio test (I2S -> PCM5102A -> amp) ==="));

  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);        // motor stays disabled
  Serial.println(F("[EN ] HIGH - motor disabled"));

  i2s.setPins(PIN_BCK, PIN_LRCK, PIN_DIN, -1, -1);
  if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println(F("[I2S] begin FAILED"));
    while (1) delay(1000);
  }
  Serial.printf("[I2S] ok  BCK=%d LRCK=%d DIN=%d  %d Hz 16-bit stereo\n",
                PIN_BCK, PIN_LRCK, PIN_DIN, SR);
  Serial.println(F("keys: +/- volume  t=tone  s=sweep  c=clicks  q=quiet  a=auto"));
  Serial.println(F("If nothing is heard: check SCK->GND and XSMT->3V3 on the DAC,"));
  Serial.println(F("the line cable into the amp's IN L, and 24 V on the amp."));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '+') { amp = min((int)(amp * 1.26f) + 2, 30000); Serial.printf("[VOL] %d\n", amp); }
    else if (c == '-') { amp = max((int)(amp / 1.26f), 5); Serial.printf("[VOL] %d\n", amp); }
    else if (c == 't' || c == 's' || c == 'c' || c == 'q' || c == 'a') {
      mode = c; Serial.printf("[MODE] %c\n", c);
    }
  }

  switch (mode) {
    case 't': writeTone(1000, 200); break;
    case 's': writeSweep(200, 2000, 2000); break;
    case 'c': writeClick(); writeSilence(200); break;
    case 'q': writeSilence(200); break;
    default:
      Serial.println(F("[PLAY] 1 kHz tone"));      writeTone(1000, 1000);  writeSilence(300);
      Serial.println(F("[PLAY] sweep 200-2000 Hz")); writeSweep(200, 2000, 2500); writeSilence(300);
      Serial.println(F("[PLAY] three clicks"));
      for (int i = 0; i < 3; i++) { writeClick(); writeSilence(250); }
      writeSilence(800);
      break;
  }
}
