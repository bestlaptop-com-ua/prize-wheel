from pathlib import Path
p=Path(__file__).parent;q=p/'speedup_replay'
s=r'''#include <Arduino.h>
#include "pw_speedup_watch.h"
#include "recorded_trace.h"
static int failures=0,checks=0;
void check(bool ok,const char* label){++checks;Serial.printf("TEST %s %s\n",label,ok?"PASS":"FAIL");if(!ok)++failures;}
float wave(int mode,uint32_t t){
  float seconds=t*0.001f;
  if(mode==0)return fmaxf(.02f,.6f-.1f*seconds)+.003f*sinf(seconds*71);
  if(mode==1)return .5f-.06f*seconds-((t>=100 && t<145)?.20f:0);
  if(mode==2)return .5f-.06f*seconds+((t>=600 && t<645)?.20f:0);
  if(mode==3)return t<500?.4f:.6f;
  if(mode==4)return .4f+(t<500?0:(t-500)*.0002f);
  if(mode==5)return .2f+t*.0002f;
  if(mode==6)return .4f+t*.00003f;
  return .4f;
}
int32_t synthetic(int mode,uint32_t duration,uint32_t offset=0){
  PwSpeedupWatch w;
  for(uint32_t t=0;t<=duration;++t)
    if(w.update(offset+t,wave(mode,t),.05f,400))return (int32_t)t;
  return -1;
}
void setup(){
  pinMode(7,OUTPUT);digitalWrite(7,HIGH);
  Serial.begin(115200);delay(1000);
  Serial.println("=== SPEEDUP REPLAY: EN HIGH, NO STEP GENERATOR ===");
  PwSpeedupWatch w;bool newFault=false,oldAbove=false;float oldLow=trace[0].speed;
  uint32_t oldSince=0;int32_t oldFault=-1;
  for(const auto& point:trace){
    newFault|=w.update(point.ms,point.speed,.05f,400);
    if(point.speed<oldLow){oldLow=point.speed;oldAbove=false;}
    else if(point.speed>oldLow+.05f){
      if(!oldAbove){oldAbove=true;oldSince=point.ms;}
      if(point.ms-oldSince>=400 && oldFault<0)oldFault=point.ms;
    }else oldAbove=false;
  }
  check(!newFault,"recorded_563_samples_no_false_fault");
  const auto& tail=trace[sizeof(trace)/sizeof(trace[0])-1];
  for(uint32_t t=tail.ms+1;t<=tail.ms+1000;++t){
    newFault|=w.update(t,tail.speed,.05f,400);
    if(oldAbove && t-oldSince>=400 && oldFault<0)oldFault=t;
  }
  Serial.printf("OLD_FILTER fault_ms=%ld baseline=%.3f held_tail_speed=%.3f\n",(long)oldFault,oldLow,tail.speed);
  check(oldFault>=400 && oldFault<=650,"old_detector_reproduces_recorded_mechanism");
  check(!newFault,"recorded_recovery_stays_clear_with_held_tail");
  check(synthetic(0,5000)<0,"noisy_deceleration");
  check(synthetic(1,5000)<0,"brief_negative_outlier");
  check(synthetic(2,5000)<0,"brief_positive_outlier");
  int32_t at=synthetic(3,2000);Serial.printf("STEP_RISE fault_ms=%ld\n",(long)at);
  check(at>=900 && at<=1100,"sustained_step_rise_detected");
  at=synthetic(4,2500);Serial.printf("ACCELERATION fault_ms=%ld\n",(long)at);
  check(at>=1100 && at<=1500,"sustained_acceleration_detected");
  at=synthetic(5,2000);Serial.printf("EARLY_ACCELERATION fault_ms=%ld\n",(long)at);
  check(at>=600 && at<=1000,"acceleration_from_capture_detected");
  at=synthetic(6,3500);Serial.printf("SLOW_ACCELERATION fault_ms=%ld\n",(long)at);
  check(at>=1900 && at<=2600,"slow_sustained_rise_detected");
  check(synthetic(3,2000,0xFFFFFE00U)==synthetic(3,2000),"millis_wraparound");
  w.reset();bool fault=false;
  for(uint32_t t=0;t<850;++t)fault|=w.update(t,t<500?.4f:.6f,.05f,400);
  check(!fault && !w.update(1100,.6f,.05f,400),"sampling_gap_cancels_debounce");
  check(!w.update(1101,NAN,.05f,400),"invalid_velocity_clears_history");
  w.reset();fault=false;
  for(uint32_t t=0;t<2000;++t)fault|=w.update(t,.4f,.05f,400);
  check(!fault,"new_spin_reset_and_steady_speed");
  w.reset();fault=false;
  for(const auto& point:trace)fault|=w.update(point.ms,point.speed,.05f,400);
  int32_t later=-1;
  for(uint32_t dt=1;dt<=2000;++dt)
    if(w.update(tail.ms+dt,tail.speed+dt*.0003f,.05f,400) && later<0)later=dt;
  check(!fault && later>0 && later<1000,"real_acceleration_after_recorded_transient");
  Serial.printf("REPLAY %s checks=%d failures=%d EN=%d\n",failures?"FAIL":"PASS",checks,failures,digitalRead(7));
}
void loop(){digitalWrite(7,HIGH);delay(10);}
'''
(q/'speedup_replay.ino').write_text(s)
b=(p/'compile_smooth_suite.py').read_text().replace("[('smooth_audit',p/'smooth_ramp_audit'),('smooth_wheel',p.parent/'prize_wheel_gpt')]","[('speedup_replay',p/'speedup_replay'),('speedup_wheel',p.parent/'prize_wheel_gpt')]").replace("p/'smooth_build.error'","p/'speedup_build.error'")
(p/'compile_speedup_suite.py').write_text(b)
print('Prepared exact C++ replay and adversarial tests, motor disabled')