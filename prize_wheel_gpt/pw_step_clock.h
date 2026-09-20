#pragma once
#include <Arduino.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <soc/mcpwm_struct.h>
#endif

// Compatibility workaround for FastAccelStepper 1.2.7 / Arduino ESP32 3.3.10.
// This sketch allocates its only stepper first: MCPWM group 0, timer 0.
// IDF leaves timer divide-by-5, then FAS applies group divide-by-5 too.
// FAS expects 16 MHz: retain its group divider and clear the timer divider.
// On-device GPIO edge counts verified both the 5x error and this correction.
// Reject other clock layouts; review this workaround when upgrading FAS/core.
static bool pwFixStepperClock() {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  const uint32_t group = MCPWM0.clk_cfg.clk_prescale;
  const uint32_t timer = MCPWM0.timer[0].timer_cfg0.timer_prescale;
  Serial.printf("# STEP clock before: group=%lu timer=%lu\n",
                (unsigned long)group, (unsigned long)timer);
  if (group != 4 || (timer != 4 && timer != 0)) return false;
  MCPWM0.timer[0].timer_cfg0.timer_prescale = 0;
  const bool ok = MCPWM0.timer[0].timer_cfg0.timer_prescale == 0;
  Serial.printf("# STEP clock: 16 MHz correction %s\n", ok ? "OK" : "FAILED");
  return ok;
#else
  return true;
#endif
}
