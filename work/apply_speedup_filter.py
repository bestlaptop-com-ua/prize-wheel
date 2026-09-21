from pathlib import Path
import shutil,csv
p=Path(__file__).parent;root=p.parent;src=root/'prize_wheel_gpt/prize_wheel_gpt.ino'
backup=p/'before-speedup-filter';backup.mkdir(exist_ok=True);shutil.copy2(src,backup/src.name)
h=r'''#pragma once
#include <math.h>
#include <stdint.h>

// Seven velocity samples 25ms apart reject short engagement disturbances.
// Only the median can establish a new low or start the 400ms rise debounce.
class PwSpeedupWatch {
 public:
  void reset() {
    count_=head_=0; sampled_=baselineValid_=above_=false;
    lastMs_=sinceMs_=0; median_=baseline_=rise_=0.0f;
  }
  bool update(uint32_t nowMs, float forward, float noise, uint32_t confirmMs) {
    if (!isfinite(forward) || forward <= 0.0f) { reset(); return false; }
    if (sampled_) {
      uint32_t gap=nowMs-lastMs_;
      if (gap<25) return false;
      // Do not let missing observations count as sustained speed-up.
      if (gap>100) reset();
    }
    sampled_=true;lastMs_=nowMs;
    values_[head_]=forward;head_=(head_+1)%7;
    if(count_<7)++count_;
    if(count_<7)return false;
    float sorted[7];
    for(uint8_t i=0;i<7;++i)sorted[i]=values_[i];
    for(uint8_t i=1;i<7;++i){
      float v=sorted[i];uint8_t j=i;
      while(j>0 && sorted[j-1]>v){sorted[j]=sorted[j-1];--j;}
      sorted[j]=v;
    }
    median_=sorted[3];
    if(!baselineValid_ || median_<baseline_){
      baseline_=median_;baselineValid_=true;above_=false;rise_=0.0f;
      return false;
    }
    rise_=median_-baseline_;
    if(rise_<=noise){above_=false;return false;}
    if(!above_){above_=true;sinceMs_=nowMs;return false;}
    return nowMs-sinceMs_>=confirmMs;
  }
  float median() const {return median_;}
  float baseline() const {return baseline_;}
  float rise() const {return rise_;}
  uint32_t sustainedMs(uint32_t nowMs) const {return above_?nowMs-sinceMs_:0;}
 private:
  float values_[7]={};
  uint8_t count_=0,head_=0;
  bool sampled_=false,baselineValid_=false,above_=false;
  uint32_t lastMs_=0,sinceMs_=0;
  float median_=0,baseline_=0,rise_=0;
};
'''
(root/'prize_wheel_gpt/pw_speedup_watch.h').write_bytes(h.replace('\n','\r\n').encode())
s=src.read_text()
assert 'float lowestForwardRevS = 0.0f;' in s
s=s.replace('#include "pw_brake_profile.h"','#include "pw_brake_profile.h"\n#include "pw_speedup_watch.h"')
s=s.replace('float lowestForwardRevS = 0.0f;\nuint32_t speedupSinceMs = 0;','PwSpeedupWatch speedupWatch;')
s=s.replace('  lowestForwardRevS = forward;\n  speedupSinceMs = 0;','  speedupWatch.reset();')
s=s.replace('  if (!encoderVelocityValid) {\n    if (velocityLossSinceMs == 0)', '  if (!encoderVelocityValid) {\n    speedupWatch.reset();  // incomplete velocity evidence cannot age the debounce\n    if (velocityLossSinceMs == 0)')
a=s.index('  // 5. Wheel speed-up detector:')
b=s.index('  // 6. Fight watchdog:',a)
s=s[:a]+'''  // 5. A short low outlier must not poison the reference for the rest of
  // the spin. Preserve the speed-rise threshold and confirmation time,
  // but establish the baseline and rising condition from robust samples.
  bool sustainedSpeedup = speedupWatch.update(nowMs, forward,
      SPEEDUP_NOISE_REV_S, SPEEDUP_FAULT_MS);
  if (speedupWatch.rise() > spin.maxSpeedRiseRevS)
    spin.maxSpeedRiseRevS = speedupWatch.rise();
  if (sustainedSpeedup) {
    Serial.printf("# SPEEDUP median=%.3f baseline=%.3f rise=%.3f sustained_ms=%lu\\n",
                  speedupWatch.median(), speedupWatch.baseline(), speedupWatch.rise(),
                  (unsigned long)speedupWatch.sustainedMs(nowMs));
    enterFault(FC_SUSTAINED_SPEEDUP, "sustained robust speed rise under power");
    return false;
  }

'''+s[b:]
s=s.replace('v2-smooth-ramp-20260918; planned pulse ramp; 1650mA persistent hold; 16K trace','v2-speedup-filter-20260918; smooth ramp; robust speedup; hold1650; 16K trace')
assert 'lowestForwardRevS' not in s and 'speedupSinceMs' not in s
src.write_bytes(s.replace('\n','\r\n').encode())
# Replay recorded samples using the exact same C++ helper on the ESP32.
q=p/'speedup_replay';q.mkdir(exist_ok=True);shutil.copy2(root/'prize_wheel_gpt/pw_speedup_watch.h',q/'pw_speedup_watch.h')
rows=list(csv.DictReader((p/'smooth_spin1.csv').open()))
first=next(r for r in rows if int(r['state'])==6);start=int(first['done_us'])
rows=[r for r in rows if int(r['done_us'])>=start]
points=',\n'.join('{%d,%.6ff}'%((int(r['done_us'])-start)//1000,-int(r['omega_mrev'])/1000) for r in rows)
(q/'recorded_trace.h').write_text('struct Point {uint32_t ms;float speed;};\nconst Point trace[]={\n'+points+'\n};\n')
print('Prepared speedup filter and',len(rows),'recorded samples')