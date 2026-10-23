#include "preyvr/ThrowLifecycle.h"
#include <iostream>
#include <stdexcept>
using namespace preyvr::carry;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
 TrackedMotionHistory h;TrackingStamp s{7,2,1,50000000000ull,900000000000ll};
 MotionSample m{};m.time=s.displayTime;m.velocityValid=true;m.velocity={5,0,0};
 h.Push(0,s,m,true);CHECK(!h.Estimate(0,s,s.publishedNs).valid);
 ++s.sequence;s.displayTime+=10000000;s.publishedNs+=10000000;m.time=s.displayTime;
 h.Push(0,s,m,true);CHECK(h.Estimate(0,s,s.publishedNs).valid);
 CHECK(!h.Estimate(0,s,s.publishedNs+61000000).valid); // cached samples cannot stay live indefinitely
 auto other=s;other.epoch++;CHECK(!h.Estimate(0,other,s.publishedNs).valid);
 other=s;other.reference+=2;CHECK(!h.Estimate(0,other,s.publishedNs).valid);
 h.Push(0,s,m,false);CHECK(!h.Estimate(0,s,s.publishedNs).valid);
 // Finite differences must never bridge a reference jump, even only 10ms apart.
 h.Reset();m.velocityValid=false;h.Push(1,s,m,true);
 s.sequence++;s.displayTime+=30000000;s.publishedNs+=30000000;m.time=s.displayTime;m.position={.15f,0,0};h.Push(1,s,m,true);
 CHECK(h.Estimate(1,s,s.publishedNs).valid);
 s.reference+=2;s.sequence++;s.displayTime+=10000000;s.publishedNs+=10000000;m.time=s.displayTime;m.position={1.15f,0,0};h.Push(1,s,m,true);
 CHECK(!h.Estimate(1,s,s.publishedNs).valid);
 // Epoch changes and tracking invalidity also discard the finite-difference baseline.
 ++s.sequence;s.displayTime+=10000000;s.publishedNs+=10000000;m.time=s.displayTime;
 m.velocityValid=true;m.velocity={2,0,0};h.Push(1,s,m,true);CHECK(h.Estimate(1,s,s.publishedNs).valid);
 h.Push(1,s,m,true);CHECK(!h.Estimate(1,s,s.publishedNs).valid); // duplicate publications cannot refresh old samples
 h.Reset();CHECK(!h.Estimate(1,s,s.publishedNs).valid);
 FireGate g;
 auto tick=[&](bool pressed,bool tracked=true,bool allowed=true,bool forced=false,bool chord=false,std::uint64_t epoch=1,std::uint64_t ref=2){
   return g.Update(tracked,allowed,forced,chord,pressed,!pressed,epoch,ref);};
 CHECK(!tick(true).pressed);CHECK(!tick(true).pressed);CHECK(tick(false).physical);CHECK(tick(true).pressed);
 CHECK(!tick(false,true,false).physical); // modal neutralization is not a physical release
 CHECK(!tick(true).pressed);tick(false);CHECK(tick(true).pressed);
 CHECK(!tick(false,false).physical);CHECK(!tick(true).pressed);tick(false);CHECK(tick(true).pressed);
 CHECK(!tick(false,true,true,true).physical);CHECK(!tick(true).pressed);tick(false);CHECK(tick(true).pressed);
 CHECK(!tick(false,true,true,false,true).physical);CHECK(!tick(true).pressed);tick(false);CHECK(tick(true).pressed);
 CHECK(!tick(false,true,true,false,false,2).physical);tick(false,true,true,false,false,2);CHECK(tick(true,true,true,false,false,2).pressed);
 CHECK(!tick(false,true,true,false,false,2,4).physical);tick(false,true,true,false,false,2,4);
 CHECK(tick(true,true,true,false,false,2,4).pressed);CHECK(tick(false,true,true,false,false,2,4).physical);
 std::cout<<"throw lifecycle: freshness, separate clocks, invalid hand, epoch/recenter reset, physical vs safety releases PASS\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
