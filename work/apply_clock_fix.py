from pathlib import Path
root=Path(r'C:\Users\Mill\Desktop\prize-wheel')
header='''#pragma once
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
  Serial.printf("# STEP clock before: group=%lu timer=%lu\\n",
                (unsigned long)group, (unsigned long)timer);
  if (group != 4 || (timer != 4 && timer != 0)) return false;
  MCPWM0.timer[0].timer_cfg0.timer_prescale = 0;
  const bool ok = MCPWM0.timer[0].timer_cfg0.timer_prescale == 0;
  Serial.printf("# STEP clock: 16 MHz correction %s\\n", ok ? "OK" : "FAILED");
  return ok;
#else
  return true;
#endif
}
'''
(root/'prize_wheel_gpt'/'pw_step_clock.h').write_text(header,encoding='utf-8')
(root/'work'/'pulse_clock_audit'/'pw_step_clock.h').write_text(header,encoding='utf-8')
p=root/'prize_wheel_gpt'/'prize_wheel_gpt.ino'
s=p.read_bytes().decode('utf-8')
changes=[
('#include <FastAccelStepper.h>','#include <FastAccelStepper.h>\n#include "pw_step_clock.h"'),
('# build: v2-diag-freeze-20260918; control parameters unchanged','# build: v2-step-clock-fix-20260918; baseline control + fault capture'),
('digitalWrite(PIN_EN, LOW);  // hold at boot; freewheel selected below','digitalWrite(PIN_EN, HIGH);  // keep outputs disabled during initialization'),
('    stepper->setAutoEnable(false);','    stepper->setAutoEnable(false);\n    if (!pwFixStepperClock()) enterFault(FC_STEPPER_API, "unsupported STEP clock layout");')]
for a,b in changes:
 a=a.replace('\n','\r\n');b=b.replace('\n','\r\n')
 assert s.count(a)==1,repr(a)
 s=s.replace(a,b)
p.write_bytes(s.encode('utf-8'))
p=root/'work'/'pulse_clock_audit'/'pulse_clock_audit.ino'
s=p.read_text()
s=s.replace('#include <soc/mcpwm_struct.h>','#include <soc/mcpwm_struct.h>\n#include "pw_step_clock.h"')
s=s.replace('  stepper->setDirectionPin(DIR_PIN,true);','  if(!pwFixStepperClock()){Serial.println("ABORT unsupported clock layout");return;}\n  stepper->setDirectionPin(DIR_PIN,true);')
s=s.replace('pulse-clock-audit 20260918:', 'pulse-clock-audit FIXED 20260918:')
p.write_text(s,encoding='utf-8')
print('Restored baseline control with fault capture; added shared clock fix; audit uses identical header.')
