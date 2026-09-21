#include "pw_imbalance.h"
#include <cstdio>
#include <map>
#include <string>
struct FakeStore{ std::map<std::string,float> f; std::map<std::string,uint16_t> u;
 size_t putFloat(const char*k,float v){f[k]=v;return 4;} float getFloat(const char*k,float d){return f.count(k)?f[k]:d;}
 size_t putUShort(const char*k,uint16_t v){u[k]=v;return 2;} uint16_t getUShort(const char*k,uint16_t d){return u.count(k)?u[k]:d;}};
int fails=0;
#define CHECK(x) do{ if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++fails;} }while(0)
int main(){
  // INL LUT vs direct
  PwInlModel m={-12.3f,4.5f,-15.5f,7.25f}; PwEncoderInl inl; CHECK(!inl.active()); CHECK(inl.labelOffsetCounts(100,88)==0);
  inl.set(m); float worst=0; for(int r=0;r<4096;++r){ double x=2*M_PI*r/4096; double e=m.a1*sin(x)+m.b1*cos(x)+m.a2*sin(2*x)+m.b2*cos(2*x); worst=fmax(worst,fabs(e-inl.errorCounts(r))); }
  printf("INL LUT worst err counts %.4f\n",worst); CHECK(worst<0.05f);
  CHECK(inl.labelOffsetCounts(88,88)==0);
  // gravity legacy equivalence
  PwGravity g; float legacy=PwGravity::legacyTravelRad(3.0f,0.3f,0.15f);
  CHECK(fabsf(g.stopTravelRad(3.0f/PW_IMB_TWO_PI,1,1.0f,0.3f,0.15f,0)-legacy)<1e-6f);
  CHECK(g.compensatedRevS(0.5f,2.0f)==0.5f);
  // print travel table for python comparison
  PwGravityModel gm={0.2509f,1.779f}; g.set(gm);
  printf("rest deg %.2f\n",g.restAngleDeg());
  const float speeds[]={0.05f,0.12f,0.26f,0.5f,0.72f,1.5f,3.0f}; const float angs[]={0,1.0f,2.5f,4.0f,5.5f};
  const float cb[2][2]={{0.0756f,0.0082f},{0.30f,0.15f}};
  for(int k=0;k<2;++k)for(int d=-1;d<=1;d+=2)for(float sp:speeds)for(float a:angs){ g.invalidate();
    printf("T %d %d %.3f %.3f %.6f\n",k,d,sp,a,g.stopTravelRad(sp,d,a,cb[k][0],cb[k][1],0)); }
  // cache shortcut consistency
  g.invalidate(); float t0=g.stopTravelRad(0.3f,1,1.0f,0.0756f,0.0082f,1000); float t1=g.stopTravelRad(0.2999f,1,1.01f,0.0756f,0.0082f,1003);
  CHECK(fabsf((t0-0.01f)-t1)<1e-5f);
  float t2=g.stopTravelRad(0.2999f,1,1.01f,0.0756f,0.0082f,1020); CHECK(fabsf(t2-t1)<0.05f);
  // compensated speed monotone: checked in python against simulated coast
  // persistence
  FakeStore st; bool loaded=true; PwImbalanceConfig z=pwLoadImbalance(st,&loaded); CHECK(!loaded&&z.gravity.g==0);
  PwImbalanceConfig c={{0.25f,1.78f},{1,2,3,4}}; CHECK(pwSaveImbalance(st,c)); PwImbalanceConfig r=pwLoadImbalance(st,&loaded); CHECK(loaded&&r.inl.a2==3&&r.gravity.phiRad==1.78f);
  st.f["inlA2"]=500; r=pwLoadImbalance(st,&loaded); CHECK(!loaded&&r.inl.a1==0);
  st.f["inlA2"]=3; CHECK(pwClearImbalance(st)); r=pwLoadImbalance(st,&loaded); CHECK(!loaded);
  PwImbalanceConfig bad={{-1,0},{0,0,0,0}}; CHECK(!pwSaveImbalance(st,bad));
  // line parser
  PwImbalanceLine line; PwImbalanceConfig out; bool cons,rej; const char* txt="0.2509,101.93,-1.5,2.25,-10,3e0\n"; line.begin(0); bool got=false;
  for(const char*p=txt;*p;++p){ got=line.feed(*p,10,&out,&cons,&rej); CHECK(cons); }
  CHECK(got&&fabsf(out.gravity.phiRad-101.93f*PW_IMB_TWO_PI/360)<1e-5f&&out.inl.b2==3.0f&&!line.open());
  line.begin(0); const char* bad1="0.25,101\n"; for(const char*p=bad1;*p;++p) got=line.feed(*p,10,&out,&cons,&rej); CHECK(!got&&rej);
  line.begin(0); got=line.feed('x',10,&out,&cons,&rej); CHECK(!got&&rej&&!line.open());
  line.begin(0); got=line.feed('1',9000,&out,&cons,&rej); CHECK(!got&&rej&&!cons);
  got=line.feed('1',10,&out,&cons,&rej); CHECK(!cons);
  printf(fails?"FAILED %d\n":"ALL OK\n",fails); return fails;}
