// Playlist v2: all six party sounds, LEFT-JUSTIFIED (MSB) format, 32-bit slots (BCK = 64 fs), 22.05 kHz.
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "samples.h"
static i2s_chan_handle_t tx;
static int32_t buf[512];           // 32-bit slots, stereo
const float GAIN = 0.5f;
static void out(size_t frames) { size_t w; i2s_channel_write(tx, buf, frames * 8, &w, portMAX_DELAY); }
static void playSample(const Sample& s) {
  Serial.printf("play %s (%.2f s)\n", s.name, (float)s.n / SAMPLE_RATE);
  for (uint32_t i = 0; i < s.n;) {
    size_t f = min<uint32_t>(s.n - i, 256);
    for (size_t k = 0; k < f; k++) { int32_t v = (int32_t)(s.data[i + k] * GAIN) << 16; buf[2*k] = v; buf[2*k+1] = v; }
    out(f); i += f;
  }
}
static void silence(uint32_t ms) { memset(buf, 0, sizeof(buf)); for (uint32_t n = (uint64_t)SAMPLE_RATE * ms / 1000; n;) { size_t f = min<uint32_t>(n, 256); out(f); n -= f; } }
void setup() {
  Serial.begin(115200); delay(500);
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&cc, &tx, NULL);
  i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_15, .ws = GPIO_NUM_16, .dout = GPIO_NUM_17, .din = I2S_GPIO_UNUSED,
                  .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } },
  };
  i2s_channel_init_std_mode(tx, &std);
  i2s_channel_enable(tx);
  Serial.println("PLAYLIST v2: MSB, 32-bit slots, 22050 Hz; tick ratchet drumroll fanfare ambience guest");
}
void loop() {
  for (int k = 0; k < NUM_SAMPLES; k++) { playSample(SAMPLES[k]); silence(1000); }
  Serial.println("--- loop ---"); silence(2000);
}
