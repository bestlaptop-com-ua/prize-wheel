// Click test (2026-09-28, Timur 12:19 "Play clicks").  The wheel's own tick sound
// (track 1 of flash 6's pw_samples.h) repeating like a spin slowing down:
// 25 clicks, gap 120 ms growing to ~760 ms, then 2.5 s quiet, repeat.
// Same audio path as flash 6: I2S0 left-justified, 32-bit slots, 22.05 kHz,
// BCK 15 / LRCK 16 / DIN 17 -> PCM5102A; gain 1.0 = flash-6 volume 30.
// Motor driver held OFF (EN high) and STEP/DIR low while this runs.
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "tick.h"

static i2s_chan_handle_t tx;
static int32_t buf[512];          // 256 stereo frames
const float GAIN = 1.0f;

static void writeFrames(const int16_t* src, uint32_t n) {   // src == nullptr: silence
  while (n) {
    uint32_t f = n > 256 ? 256 : n;
    for (uint32_t k = 0; k < f; k++) {
      int32_t v = src ? ((int32_t)(src[k] * GAIN) << 16) : 0;
      buf[2 * k] = v; buf[2 * k + 1] = v;
    }
    size_t w = 0;
    i2s_channel_write(tx, buf, f * 8, &w, portMAX_DELAY);
    if (src) src += f;
    n -= f;
  }
}

static void click(uint32_t periodMs) {
  uint32_t period = (uint32_t)((uint64_t)periodMs * PW_SAMPLE_RATE / 1000u);
  uint32_t n = period < TICK_N ? period : TICK_N;
  writeFrames(pcm_tick, n);
  writeFrames(nullptr, period - n);
}

void setup() {
  pinMode(7, OUTPUT); digitalWrite(7, HIGH);   // TMC5160 EN (active low): power stage off
  pinMode(5, OUTPUT); digitalWrite(5, LOW);    // STEP
  pinMode(6, OUTPUT); digitalWrite(6, LOW);    // DIR
  Serial.begin(115200);
  delay(300);
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&cc, &tx, NULL);
  i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PW_SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_15, .ws = GPIO_NUM_16, .dout = GPIO_NUM_17, .din = I2S_GPIO_UNUSED,
                  .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } },
  };
  i2s_channel_init_std_mode(tx, &std);
  i2s_channel_enable(tx);
  Serial.println("CLICKTEST: wheel tick (track 1), 25 clicks 120 -> 760 ms, 2.5 s pause, repeat; gain 1.0; motor off");
}

void loop() {
  static uint32_t run = 0;
  Serial.printf("click run %lu\n", (unsigned long)++run);
  float gap = 120.0f;
  for (int i = 0; i < 25; i++) { click((uint32_t)gap); gap *= 1.08f; }
  writeFrames(nullptr, (uint32_t)PW_SAMPLE_RATE * 5u / 2u);
}
