// Format test: alternates Philips I2S vs left-justified (MSB) every pass, playing fanfare + guest each time.
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "samples.h"
static i2s_chan_handle_t tx;
static int16_t buf[512];
const float GAIN = 0.5f;
static void setFormat(bool philips) {
  i2s_channel_disable(tx);
  i2s_std_slot_config_t slot = philips ? (i2s_std_slot_config_t)I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)
                                       : (i2s_std_slot_config_t)I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  i2s_channel_reconfig_std_slot(tx, &slot);
  i2s_channel_enable(tx);
  Serial.println(philips ? "FORMAT: PHILIPS (standard I2S)" : "FORMAT: LEFT-JUSTIFIED (MSB)");
}
static void out(size_t frames) { size_t w; i2s_channel_write(tx, buf, frames * 4, &w, portMAX_DELAY); }
static void playSample(const Sample& s) {
  Serial.printf("  play %s\n", s.name);
  for (uint32_t i = 0; i < s.n;) {
    size_t f = min<uint32_t>(s.n - i, 256);
    for (size_t k = 0; k < f; k++) { int16_t v = (int16_t)(s.data[i + k] * GAIN); buf[2*k] = v; buf[2*k+1] = v; }
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
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_15, .ws = GPIO_NUM_16, .dout = GPIO_NUM_17, .din = I2S_GPIO_UNUSED,
                  .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } },
  };
  i2s_channel_init_std_mode(tx, &std);
  i2s_channel_enable(tx);
  Serial.println("FMTTEST: alternating Philips / left-justified, fanfare + guest each");
}
void loop() {
  static bool philips = true;
  setFormat(philips);
  playSample(SAMPLES[3]); silence(500); playSample(SAMPLES[5]); silence(1500);
  philips = !philips;
}
