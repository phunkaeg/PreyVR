#include "../src/dll/GrenadeLane.cpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace preyvr;using namespace preyvr::dll;
GameplayPoseFrame current{};bool tracking=true,gameplay=true,queryRuns=true;float hitDistance=-1;
unsigned queries=0,releases=0,updates=0;float observedRange=-1;
alignas(16) unsigned char weaponBytes[0x3000]{},playerBytes[0x100]{};
std::uintptr_t table[64]{};
std::uintptr_t __fastcall GetPlayer(){return reinterpret_cast<std::uintptr_t>(playerBytes);}
bool __fastcall NativeRelease(std::uintptr_t){++releases;return true;}
void __fastcall NativeUpdate(std::uintptr_t w,std::uintptr_t){++updates;observedRange=*reinterpret_cast<float*>(w-8+kDeployRange);}
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
}
namespace preyvr::lifecycle {void Log(std::string_view){}}
namespace preyvr::dll {
bool EnsureMinHook(){return false;}
bool HudGameplayInputAllowed(){return gameplay;}
bool TryGetGameplayPoseFrame(GameplayPoseFrame& f,bool){f=current;return tracking;}
bool TryGetEquippedRig(std::uintptr_t,EquippedRig& r){r.weapon=reinterpret_cast<std::uintptr_t>(weaponBytes);return true;}
bool EstimateHandMotion(unsigned,carry::Motion& m){m.valid=true;m.velocity={5,0,0};return true;}
void QueueHaptic(Hand,haptics::Event,const TrackingFrame&){}
bool QuerySegment(const GameplayPoseFrame&,Vec3,Vec3,scene::Hit& hit){++queries;hit.distance=hitDistance;return queryRuns;}
}
int main(){try{
 using namespace preyvr;using namespace preyvr::dll;
 gInstallTried=gInstalled=true;gHand=true;gSpawnMode=1;
 gBase=reinterpret_cast<std::uintptr_t>(&GetPlayer)-kGetPlayerRva;
 const auto weapon=reinterpret_cast<std::uintptr_t>(weaponBytes);
 *reinterpret_cast<std::uintptr_t*>(weaponBytes)=reinterpret_cast<std::uintptr_t>(table);table[kThrowSlot/8]=gBase+kThrowRva;
 current.tracking.epoch=7;current.referenceGeneration=2;current.cameraCentreValid=true;current.player=GetPlayer();
 current.tracking.hands[1].gripValidity={true,true,true,true,0};current.tracking.hands[1].gripPose.position={.4f,0,-.3f};
 NoteGrenadeTrigger(true,true);NoteGrenadeTrigger(false,true);Check(gTrigger.valid,"real tracked press/release authorizes throw");
 NoteGrenadeTrigger(true,true);NoteGrenadeTrigger(false,false);Check(!gTrigger.valid&&!gPressNs,"forced release cancels");
 gRelease=&NativeRelease;Check(ReleaseHook(weapon)&&releases==1&&!gSkip.weapon,"forced release adds no early throw");
 NoteGrenadeTrigger(true,true);tracking=false;NoteGrenadeTrigger(false,true);Check(!gTrigger.valid,"lost tracking cannot authorize throw");tracking=true;
 NoteGrenadeTrigger(true,true);++current.tracking.epoch;NoteGrenadeTrigger(false,true);Check(!gTrigger.valid,"epoch discontinuity cancels press");
 NoteGrenadeTrigger(true,true);current.referenceGeneration+=2;NoteGrenadeTrigger(false,true);Check(!gTrigger.valid,"reference discontinuity cancels press");
 NoteGrenadeTrigger(true,true);current.tracking.hands[1].gripValidity={};NoteGrenadeTrigger(false,true);Check(!gTrigger.valid,"head valid but hand invalid cancels");
 current.tracking.hands[1].gripValidity={true,true,true,true,0};
 auto scope=[&]{Scope s{};s.steer=s.haveFrame=true;s.frame=current;s.release.motion.valid=true;s.release.motion.velocity={5,0,0};return s;};
 Vec3 position{.1f,.2f,.3f},direction{0,1,0};const auto original=position;auto s=scope();queryRuns=false;
 Steer(s,position,direction);Check(queries==1&&!s.steer&&position.x==original.x&&position.y==original.y&&position.z==original.z,"query refusal preserves native spawn and velocity policy");
 queryRuns=true;s=scope();Steer(s,position,direction);Check(s.steer&&s.spawnFrom==1,"valid miss permits controller spawn");
 position=original;hitDistance=.1f;s=scope();Steer(s,position,direction);Check(s.steer&&s.pulledBack>0,"valid hit pulls back");
 gSkip={weapon,MonotonicNanoseconds()};SetGrenadeHand(0);Check(!gTrigger.valid&&gSkip.weapon==weapon,"disable cancels pending input but preserves duplicate-event suppression");
 gUpdate=&NativeUpdate;*reinterpret_cast<float*>(weapon+kDeployRange)=2;
 UpdateHook(weapon+8,0);Check(observedRange==2&&updates==1,"disabled hook preserves native deploy range");
 gHand=true;tracking=false;UpdateHook(weapon+8,0);Check(observedRange==2,"untracked hook preserves native deployment");
 tracking=true;UpdateHook(weapon+8,0);Check(observedRange==0&&*reinterpret_cast<float*>(weapon+kDeployRange)==2,"active suppression positive control restores field");
 std::cout<<"grenade runtime: actual trigger adapter, native hooks, query refusal/miss/hit, disabled deploy and delayed-event marker PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
