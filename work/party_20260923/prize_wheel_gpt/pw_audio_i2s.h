#pragma once
/* I2S sample player: S3 I2S0 -> PCM5102A (left-justified, 32-bit slots, 22.05 kHz)
 * -> TPA3116D2.  Replaces the DFPlayer transport; the command queue in
 * pw_party_impl.h is unchanged and calls pwI2sPlay/SetLoop/Stop/SetVolume. */
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "pw_samples.h"

#define PW_I2S_BCLK GPIO_NUM_15
#define PW_I2S_LRCK GPIO_NUM_16
#define PW_I2S_DOUT GPIO_NUM_17
#define PW_I2S_GAIN_MAX 1.0f       /* volume 30 -> 1.0 (full scale); V<n> live */

static i2s_chan_handle_t pwI2sTx = nullptr;
static portMUX_TYPE pwI2sMux = portMUX_INITIALIZER_UNLOCKED;
static const PwSample* volatile pwI2sCur = nullptr;
static volatile uint32_t pwI2sPos = 0;
static volatile bool pwI2sLoop = false;
static volatile float pwI2sGain = 0.5f;
static bool pwI2sRunning = false;

static void pwI2sPlay(uint16_t track) {
  if (track < 1 || track > PW_NUM_SAMPLES) return;
  portENTER_CRITICAL(&pwI2sMux);
  pwI2sCur = &PW_SAMPLES[track - 1]; pwI2sPos = 0; pwI2sLoop = false;
  portEXIT_CRITICAL(&pwI2sMux);
}
static void pwI2sSetLoop(bool on) { pwI2sLoop = on; }
static void pwI2sStop() {
  portENTER_CRITICAL(&pwI2sMux);
  pwI2sCur = nullptr; pwI2sPos = 0; pwI2sLoop = false;
  portEXIT_CRITICAL(&pwI2sMux);
}
static void pwI2sSetVolume(uint16_t vol) { if (vol > 30) vol = 30; pwI2sGain = PW_I2S_GAIN_MAX * (float)vol / 30.0f; }

/* 2026-09-23 hand-stop alarm: synthesized two-tone alarm (988/1480 Hz square,
 * alternating every 125 ms).  It pre-empts any sample for its duration. */
static volatile uint32_t pwI2sAlarmLeft = 0;          /* samples still to play */
static const uint32_t PW_ALARM_INC_LO = (uint32_t)(988.0 * 4294967296.0 / PW_SAMPLE_RATE);
static const uint32_t PW_ALARM_INC_HI = (uint32_t)(1480.0 * 4294967296.0 / PW_SAMPLE_RATE);
static void pwI2sAlarm(uint32_t ms) {
  portENTER_CRITICAL(&pwI2sMux);
  pwI2sCur = nullptr; pwI2sPos = 0; pwI2sLoop = false;
  pwI2sAlarmLeft = (uint32_t)((uint64_t)ms * PW_SAMPLE_RATE / 1000u);
  portEXIT_CRITICAL(&pwI2sMux);
}

static void pwI2sTask(void*) {
  static int32_t buf[256 * 2];
  static uint32_t alarmPhase = 0, alarmT = 0;
  for (;;) {
    portENTER_CRITICAL(&pwI2sMux);
    const PwSample* s = pwI2sCur; uint32_t pos = pwI2sPos; bool loop = pwI2sLoop; float g = pwI2sGain;
    uint32_t alarmLeft = pwI2sAlarmLeft;
    portEXIT_CRITICAL(&pwI2sMux);
    if (alarmLeft) {
      for (size_t k = 0; k < 256; k++) {
        const bool hi = ((alarmT / (PW_SAMPLE_RATE / 8)) & 1u) != 0;
        alarmPhase += hi ? PW_ALARM_INC_HI : PW_ALARM_INC_LO;
        const int32_t sq = (alarmPhase & 0x80000000u) ? 16000 : -16000;
        int32_t v = (int32_t)(sq * g) << 16;
        buf[2 * k] = v; buf[2 * k + 1] = v;
        ++alarmT;
      }
      portENTER_CRITICAL(&pwI2sMux);
      pwI2sAlarmLeft = (pwI2sAlarmLeft > 256) ? pwI2sAlarmLeft - 256 : 0;
      portEXIT_CRITICAL(&pwI2sMux);
    } else if (!s) {
      memset(buf, 0, sizeof(buf));
    } else {
      for (size_t k = 0; k < 256; k++) {
        int32_t v = 0;
        if (pos >= s->n && loop) pos = 0;
        if (pos < s->n) v = (int32_t)(s->data[pos++] * g) << 16;
        buf[2 * k] = v; buf[2 * k + 1] = v;
      }
      portENTER_CRITICAL(&pwI2sMux);
      if (pwI2sCur == s) {
        if (pos >= s->n && !pwI2sLoop) { pwI2sCur = nullptr; pwI2sPos = 0; }
        else pwI2sPos = pos;
      }
      portEXIT_CRITICAL(&pwI2sMux);
    }
    size_t w = 0;
    i2s_channel_write(pwI2sTx, buf, sizeof(buf), &w, portMAX_DELAY);
  }
}

static bool pwI2sBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&cc, &pwI2sTx, NULL) != ESP_OK) return false;
  i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PW_SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PW_I2S_BCLK, .ws = PW_I2S_LRCK, .dout = PW_I2S_DOUT, .din = I2S_GPIO_UNUSED,
                  .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } },
  };
  if (i2s_channel_init_std_mode(pwI2sTx, &std) != ESP_OK) return false;
  if (i2s_channel_enable(pwI2sTx) != ESP_OK) return false;
  pwI2sRunning = xTaskCreatePinnedToCore(pwI2sTask, "pwi2s", 4096, nullptr, 2, nullptr, 0) == pdPASS;
  return pwI2sRunning;
}
