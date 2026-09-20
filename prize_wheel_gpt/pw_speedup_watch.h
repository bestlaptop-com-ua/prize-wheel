#pragma once
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
