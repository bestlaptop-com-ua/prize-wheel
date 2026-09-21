#include "pw_imbalance.h"
#include <cstdio>
#include <chrono>
int main(){ PwGravity g; g.set({0.2509f,1.779f});
 float A0=4.48f*PW_IMB_TWO_PI/360; float w=1.6777f/PW_IMB_TWO_PI;
 printf("predicted travel deg %.1f (actual 901.1)  legacy no-gravity %.1f\n", g.stopTravelRad(w,-1,A0,0.0756f,0.0082f,0)*360/PW_IMB_TWO_PI, PwGravity::legacyTravelRad(1.6777f,0.0756f,0.0082f)*360/PW_IMB_TWO_PI);
 // later point in the same coast: t=8.0s unw=6791 counts, speed ~ (6829-6753)/(8.079-7.920)=478 c/s
 float A1=A0-6791*PW_IMB_TWO_PI/4096; float w1=478.0f/4096;
 g.invalidate(); printf("from t=8s: predicted remaining %.1f (actual %.1f)\n", g.stopTravelRad(w1,-1,A1,0.0756f,0.0082f,0)*360/PW_IMB_TWO_PI,(10252-6791)*360.0/4096);
 auto t0=std::chrono::steady_clock::now(); float acc=0; for(int i=0;i<2000;++i){ g.invalidate(); acc+=g.stopTravelRad(0.72f,1,(float)(i%63)/10,0.0756f,0.0082f,0);} 
 auto dt=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count()/2000; printf("host time per uncached call %.1f us (acc %.0f)\n",dt,acc);}
