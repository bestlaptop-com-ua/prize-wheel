#include <Arduino.h>
#include <FastAccelStepper.h>
#include "pw_step_clock.h"
#include "pw_brake_profile.h"
static constexpr uint8_t STEP_PIN=5, DIR_PIN=6, EN_PIN=7;
FastAccelStepperEngine engine;
FastAccelStepper* stepper=nullptr;
volatile uint32_t edges=0;
void ARDUINO_ISR_ATTR countEdge(){++edges;}
struct Sample { uint32_t us, edges; int32_t reported; };
Sample samples[200];
bool mathTests(){
  if(pwPlanBrake(2064,293.8f,3200,650).feasible) return false;
  if(!pwPlanBrake(2064,400.0f,3200,650).feasible) return false;
  if(pwPlanBrake(0,100,3200,650).feasible ||
     pwPlanBrake(1000,0,3200,650).feasible ||
     pwPlanBrake(1000,NAN,3200,650).feasible) return false;
  uint32_t checks=0;
  for(uint32_t hz=40;hz<=2200;hz+=17){
    for(float deg=7;deg<=800;deg+=13){
      PwBrakePlan p=pwPlanBrake(hz,deg,3200,650);
      float v=(float)hz/3200;
      float required=v*v*180/deg;
      if(p.feasible){
        if(p.accelerationSps2>650 || p.accelerationSps2==0 ||
           pwBrakeDistanceDeg(v,p.decelRevS2)>deg+0.002f ||
           sqrtf(2*p.decelRevS2*deg/360)+0.00001f<v) return false;
        for(float dt: {0.001f,0.025f,0.2f}){
          for(float desired: {0.0f,v*0.5f,v*1.2f}){
            float n=pwLimitBrakeCommand(v,desired,p.decelRevS2,dt);
            if(n>v+0.000001f || n<0 ||
               v-n>p.decelRevS2*fminf(dt,0.05f)+0.000001f) return false;
            ++checks;
          }
        }
      }else if(required<650.0f/3200-0.00001f) return false;
      ++checks;
    }
  }
  Serial.printf("MATH PASS checks=%lu; recorded infeasible capture rejected\n",(unsigned long)checks);
  return true;
}
bool ramp(uint32_t hz,uint32_t accel,bool dir){
  digitalWrite(EN_PIN,HIGH);
  if(digitalRead(EN_PIN)!=HIGH)return false;
  stepper->setDirectionPin(DIR_PIN,dir);
  if(stepper->setAcceleration(accel)!=0 || stepper->setSpeedInHz(hz)!=0)return false;
  stepper->setJumpStart((uint32_t)lroundf((float)hz*hz/(2.0f*accel)));
  stepper->setCurrentPosition(0);
  uint32_t t0=micros(),n0=edges,last=0;size_t count=0;bool stopped=false;
  if(stepper->runForward()!=MoveResultCode::OK)return false;
  while(micros()-t0<15000000){
    uint32_t age=micros()-t0;
    if(digitalRead(EN_PIN)!=HIGH){stepper->forceStop();return false;}
    if(!stopped && age>=25000){stepper->stopMove();stopped=true;}
    if(age-last>=50000 && count<200){
      samples[count++]={age,edges-n0,stepper->getCurrentSpeedInMilliHz(true)};last=age;
    }
    if(stopped && !stepper->isRunning())break;
    delay(1);
  }
  uint32_t duration=micros()-t0,total=edges-n0;
  float expected=hz*0.025f+(float)hz*hz/(2.0f*accel);
  float expectedS=0.025f+(float)hz/accel;
  bool ok=!stepper->isRunning() && fabsf(total-expected)<expected*0.05f+20 &&
          fabsf(duration*1e-6f-expectedS)<expectedS*0.1f+0.1f;
  Serial.printf("RAMP hz=%lu a=%lu dir=%d edges=%lu expected=%.1f us=%lu expected_s=%.4f EN=%d pass=%d\n",
    (unsigned long)hz,(unsigned long)accel,dir,(unsigned long)total,expected,
    (unsigned long)duration,expectedS,digitalRead(EN_PIN),ok);
  for(size_t i=0;i<count;++i)Serial.printf("R,%lu,%lu,%ld\n",
    (unsigned long)samples[i].us,(unsigned long)samples[i].edges,(long)samples[i].reported);
  return ok;
}
void setup(){
  pinMode(EN_PIN,OUTPUT);digitalWrite(EN_PIN,HIGH);
  Serial.begin(115200);delay(1500);
  Serial.println("=== SMOOTH RAMP AUDIT: MOTOR DISABLED ===");
  if(!mathTests()){Serial.println("AUDIT FAIL math");return;}
  engine.init();stepper=engine.stepperConnectToPin(STEP_PIN,DRIVER_MCPWM_PCNT);
  if(!stepper || !pwFixStepperClock()){Serial.println("AUDIT FAIL init");return;}
  stepper->setAutoEnable(false);attachInterrupt(STEP_PIN,countEdge,RISING);
  bool ok=true;
  ok=ramp(2064,650,true)&&ok;delay(100);
  ok=ramp(1600,400,false)&&ok;delay(100);
  ok=ramp(480,100,true)&&ok;
  Serial.println(ok?"AUDIT PASS ALL; MOTOR DISABLED":"AUDIT FAIL ramp; MOTOR DISABLED");
}
void loop(){digitalWrite(EN_PIN,HIGH);delay(10);}
