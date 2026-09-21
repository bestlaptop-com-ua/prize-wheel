#include <Arduino.h>
#include <FastAccelStepper.h>
#include <soc/mcpwm_struct.h>
#include "pw_step_clock.h"

// Pulse timing audit only. EN is always HIGH and is never given to FAS.
static constexpr uint8_t STEP_PIN=5, DIR_PIN=6, EN_PIN=7;
FastAccelStepperEngine engine;
FastAccelStepper* stepper=nullptr;
volatile uint32_t edges=0;
void ARDUINO_ISR_ATTR countEdge() { ++edges; }

void showClock() {
  Serial.printf("CLOCK group0=%u timer0=%u group1=%u timer1=%u EN=%d\n",
    MCPWM0.clk_cfg.clk_prescale, MCPWM0.timer[0].timer_cfg0.timer_prescale,
    MCPWM1.clk_cfg.clk_prescale, MCPWM1.timer[0].timer_cfg0.timer_prescale,
    digitalRead(EN_PIN));
}
void audit() {
  if (!stepper) return;
  showClock();
  for (uint32_t hz : {500U,1000U,2000U}) {
    digitalWrite(EN_PIN,HIGH);
    if(digitalRead(EN_PIN)!=HIGH) { Serial.println("ABORT EN not HIGH"); return; }
    stepper->setAcceleration(100000);
    stepper->setSpeedInHz(hz);
    stepper->setJumpStart(0);
    if(stepper->runForward()!=MoveResultCode::OK) { Serial.println("ABORT runForward"); return; }
    delay(750);
    uint32_t n0=edges, t0=micros();
    int32_t pos0=stepper->getCurrentPosition();
    delay(1000);
    uint32_t dt=micros()-t0, n=edges-n0;
    int32_t posDelta=stepper->getCurrentPosition()-pos0;
    Serial.printf("PULSE requested=%lu reported=%.3f gpio_edges=%lu dt_us=%lu actual=%.3f position_delta=%ld EN=%d\n",
      (unsigned long)hz,stepper->getCurrentSpeedInMilliHz(true)/1000.0,
      (unsigned long)n,(unsigned long)dt,n*1000000.0/dt,(long)posDelta,digitalRead(EN_PIN));
    stepper->forceStop();
    while(stepper->isRunning()) delay(1);
    delay(100);
  }
  Serial.println("AUDIT DONE: motor remains disabled. t=repeat, c=test timer prescaler zero.");
}
void setup() {
  pinMode(EN_PIN,OUTPUT);digitalWrite(EN_PIN,HIGH);
  Serial.begin(115200);delay(1500);
  Serial.println("=== pulse-clock-audit FIXED 20260918: MOTOR DISABLED ===");
  engine.init();
  stepper=engine.stepperConnectToPin(STEP_PIN,DRIVER_MCPWM_PCNT);
  if(!stepper){Serial.println("ABORT no stepper");return;}
  if(!pwFixStepperClock()){Serial.println("ABORT unsupported clock layout");return;}
  stepper->setDirectionPin(DIR_PIN,true);
  stepper->setAutoEnable(false);
  attachInterrupt(STEP_PIN,countEdge,RISING);
  audit();
}
void loop() {
  digitalWrite(EN_PIN,HIGH);
  if(Serial.available()){
    char c=Serial.read();
    if(c=='t')audit();
    if(c=='c'){
      if(stepper && !stepper->isRunning()){
        MCPWM0.timer[0].timer_cfg0.timer_prescale=0;
        Serial.println("TEST: group0 timer0 prescaler=0; library on disk unchanged");
        audit();
      }
    }
  }
  delay(5);
}
