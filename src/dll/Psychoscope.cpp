#include "Psychoscope.h"
#include "AimTakeover.h"
#include "HudBridge.h"
#include "VrOptionsRuntime.h"
#include "InputPost.h"
#include "preyvr/PsychoscopeGesture.h"
#include "preyvr/PsychoscopeNative.h"
#include "preyvr/LatestSnapshot.h"
#include "Logger.h"
#include <atomic>
#include <cstring>
namespace preyvr::dll {
namespace {
std::atomic<unsigned long long> sent{0},refused{0};
std::atomic<bool> fault{false},zone{false};
DWORD Toggle(std::uintptr_t player){
 __try {
  if(InputDrainThreadId()==0||InputDrainThreadId()!=GetCurrentThreadId())return ERROR_INVALID_THREAD_ID;
  const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
  if(!base||!player)return ERROR_NOT_READY;
  if(std::memcmp(reinterpret_cast<const void*>(base+scope::HandlerRva),scope::HandlerBytes.data(),scope::HandlerBytes.size()))
      return ERROR_BAD_EXE_FORMAT;
  const auto input=player+scope::InputOffset;
  if(*reinterpret_cast<const std::uintptr_t*>(input)!=base+scope::InputVtableRva||
     *reinterpret_cast<const std::uintptr_t*>(input+0x70)!=player)return ERROR_INVALID_ADDRESS;
  if(!*reinterpret_cast<const std::uintptr_t*>(base+0x224D9D8)||
     !*reinterpret_cast<const std::uintptr_t*>(player+0x678+0xE8))return ERROR_NOT_READY;
  // Same member-function ABI as the registered native action. This exact body
  // reads only this; entity/name/mode/value are unused (verified in all 208 bytes).
  using Action=bool(__fastcall*)(void*,unsigned,const void*,int,float);
  reinterpret_cast<Action>(base+scope::HandlerRva)(reinterpret_cast<void*>(input),0,nullptr,1,1.f);
  return 0; // dispatched, not proof that progression allowed activation
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
void UpdatePsychoscopeGesture(const GameplayPoseFrame& frame,bool tracking){
 // Called only by the live ArkPlayer producer hook, on its owning game thread.
 // The receiver is supplied by that call, not retained from an earlier level.
 static PsychoscopeGesture gesture;
 static std::uint64_t owner=0,reference=0,epoch=0;
 static long long previous=0;
 const auto& f=frame.tracking;
 if(owner!=frame.player||reference!=frame.referenceGeneration||epoch!=f.epoch){
  gesture={};previous=0;owner=frame.player;reference=frame.referenceGeneration;epoch=f.epoch;
 }
 const bool enabled=PsychoscopeGestureEnabled()&&!fault.load();
 if(!enabled||!tracking||!HudGameplayInputAllowed()||frame.twoHand.held){
  gesture={};previous=0;zone=false;return;
 }
 if(previous==f.displayTime)return; // one recognition step per coherent XR sample
 const float dt=previous&&f.displayTime>previous?static_cast<float>(f.displayTime-previous)*1e-9f:0;
 previous=f.displayTime;
 const auto& hand=f.hands[0];
 const bool usable=FreshSample(MonotonicNanoseconds(),f.publishedNs)&&
   IsPoseUsable(f.head,f.headValidity,200000000)&&IsPoseUsable(hand.gripPose,hand.gripValidity,200000000)&&
   !f.hands[1].gripPressed&&!hand.menuAccept&&!hand.menuCancel;
 const auto& q=f.head.orientation;
 const Vec3 delta{hand.gripPose.position.x-f.head.position.x,hand.gripPose.position.y-f.head.position.y,hand.gripPose.position.z-f.head.position.z};
 const auto local=Rotate(Quaternion{-q.x,-q.y,-q.z,q.w},delta);
 zone=PsychoscopeGesture::InZone(local);
 if(!gesture.Update(local,hand.gripPressed,usable,dt,f.epoch))return;
 const auto result=Toggle(frame.player);
 if(result){++refused;fault=true;}else ++sent;
 lifecycle::Log("preyvr_psychoscope dispatched="+std::to_string(result==0)+" result="+std::to_string(result)+
     " native_acceptance=unverified");
}
bool PsychoscopeZoneHasLeftHand(){return zone.load();}
std::string PsychoscopeReport(){return " dispatched="+std::to_string(sent.load())+" refused="+std::to_string(refused.load())+" fault="+std::to_string(fault.load());}
}
