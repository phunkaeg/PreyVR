// Actual production motion adapter and carry hook, with native/XR boundaries replaced.
#include "../src/dll/CarryLane.cpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace preyvr;using namespace preyvr::dll;
GameplayPoseFrame current{};bool tracking=true,gameplay=true;std::uint64_t reference=2;
unsigned updates=0;
alignas(16) unsigned char player[0xD00]{};
std::uintptr_t __fastcall GetPlayer(){return reinterpret_cast<std::uintptr_t>(player);}
std::uint64_t __fastcall NativeUpdate(std::uintptr_t,float){++updates;return 123;}
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
}
namespace preyvr::lifecycle {void Log(std::string_view){}}
namespace preyvr::dll {
bool EnsureMinHook(){return false;}
bool HudGameplayInputAllowed(){return gameplay;}
unsigned long long HeadTrackingReferenceGeneration(){return reference;}
bool TryGetGameplayPoseFrame(GameplayPoseFrame& f,bool){f=current;return tracking;}
bool TryGetTrackingFrame(TrackingFrame& f){f=current.tracking;return tracking;}
bool TryGetUsePointer(UsePointer&){return false;}
float UseCarryHoldSeconds(std::uint32_t){return -1;}
void QueueHaptic(Hand,haptics::Event,const TrackingFrame&){}
}
int main(){try{
 using namespace preyvr;using namespace preyvr::dll;
 current.referenceGeneration=reference;current.cameraCentreValid=current.headYawUsable=true;
 current.tracking.epoch=1;current.tracking.sequence=1;current.tracking.displayTime=900000000000ll;
 current.tracking.publishedNs=current.publishedNs=MonotonicNanoseconds();
 auto& hand=current.tracking.hands[0];hand.gripValidity={true,true,true,true,0};
 hand.gripVelocityValid=true;hand.gripLinearVelocity={5,0,0};
 auto record=[&]{++current.tracking.sequence;current.tracking.displayTime+=10000000;
   current.tracking.publishedNs=MonotonicNanoseconds();RecordCarryMotion(current.tracking);};
 record();record();carry::Motion m{};Check(EstimateHandMotion(0,m)&&m.speed==5,"live motion positive control");
 tracking=false;Check(!EstimateHandMotion(0,m)&&!m.valid,"lost publication rejects stored peak");tracking=true;
 current.tracking.publishedNs-=100000000;Check(!EstimateHandMotion(0,m),"stalled publication rejects motion");
 current.tracking.publishedNs=MonotonicNanoseconds();
 hand.gripValidity={};record();Check(!EstimateHandMotion(0,m),"invalid hand clears history");
 hand.gripValidity={true,true,true,true,0};record();record();Check(EstimateHandMotion(0,m),"reacquired valid runtime velocity");
 reference+=2;current.referenceGeneration=reference;Check(!EstimateHandMotion(0,m),"recenter rejects prior reference");
 record();record();Check(EstimateHandMotion(0,m),"new reference can provide fresh motion");
 ++current.tracking.epoch;Check(!EstimateHandMotion(0,m),"session epoch rejects old motion");
 record();record();Check(EstimateHandMotion(0,m),"new epoch positive control");
 gInstallTried=gInstalled=true;gHand=true;gHolding=true;gGrab.entity=42;
 RequestCarryRelease();Check(gPending.active&&gPending.entity==42,"physical release request scoped to held entity");
 SetCarryHand(0);Check(!gPending.active&&!CarryHoldingInHand()&&!EstimateHandMotion(0,m),"stop withdraws request, ownership and motion");
 gBase=reinterpret_cast<std::uintptr_t>(&GetPlayer)-kGetPlayerRva;gUpdate=&NativeUpdate;
 gGrab.active=true;gPending.active=true;
 Check(UpdateHook(PlayerCarry(GetPlayer()),.01f)==123&&updates==1&&!gGrab.active&&!gPending.active,"disabled hook forwards native and clears game-thread state");
 gameplay=false;gHand=true;gHolding=true;RequestCarryRelease();Check(!gPending.active,"modal neutralization queues no carry throw");
 std::cout<<"carry runtime: actual history adapter, tracking/recenter/session boundaries and disabled hook PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
