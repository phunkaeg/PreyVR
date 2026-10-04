#include "BodyEquipment.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "VrOptionsRuntime.h"
#include "SlotFeedback.h"
#include "WeaponAttachment.h"
#include "preyvr/EquipmentNative.h"
#include "preyvr/PsychoscopeNative.h"
#include "preyvr/LatestSnapshot.h"
#include <atomic>
#include <algorithm>

namespace preyvr::dll {
namespace {
struct Request {int slot=-1;std::uint64_t epoch=0,reference=0,stamp=0;};
struct Status {equipment::Vitals values;std::uint64_t epoch=0,stamp=0;};
LatestSnapshot<Status> status;
Request request; // input-drain / player-producer thread only
equipment::HolsterGesture gesture;
std::atomic<bool> gripOwned{false},clear{false},fault{false};
std::atomic<unsigned> dispatched{0},refused{0},occupied{0};
std::atomic<unsigned long long> wristSamples{0},wristFrames{0};
std::array<std::uint32_t,2> slots{};
std::array<std::uintptr_t,2> slotWeapons{};
std::uintptr_t owner=0,inventory=0;
std::uint64_t ownerEpoch=0;

bool Gate(std::uintptr_t base){
 for(const auto& c:equipment::native::Contracts){
  auto bytes=reinterpret_cast<const unsigned char*>(base+c.rva);
  std::uint64_t hash=14695981039346656037ull;
  for(std::size_t n=0;n<c.size;++n)hash=(hash^bytes[n])*1099511628211ull;
  if(hash!=c.hash)return false;
 }
 return true;
}
// These are direct Steam native functions, never inferred vtable indices.
// Entry/consumer bytes and complete ABIs are recorded in EquipmentNative.h.
DWORD Native(const GameplayPoseFrame& frame,int slot,bool readStats,Status* out,bool* didStow){
 __try {
  if(InputDrainThreadId()==0||InputDrainThreadId()!=GetCurrentThreadId())return ERROR_INVALID_THREAD_ID;
  const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
  if(!base||!frame.player)return ERROR_NOT_READY;
  static bool gated=false;
  if(!gated){if(!Gate(base))return ERROR_BAD_EXE_FORMAT;gated=true;}
  const auto player=reinterpret_cast<std::uintptr_t(__fastcall*)()>(base+0x157C990)();
  if(player!=frame.player)return ERROR_NOT_READY;
  const auto input=player+scope::InputOffset;
  if(*reinterpret_cast<const std::uintptr_t*>(input)!=base+scope::InputVtableRva||
     *reinterpret_cast<const std::uintptr_t*>(input+0x70)!=player)return ERROR_INVALID_ADDRESS;
  const auto inv=*reinterpret_cast<const std::uintptr_t*>(player+0x1810);
  const auto now=MonotonicNanoseconds();
  const bool cleared=clear.exchange(false);
  if(owner!=player||inventory!=inv||ownerEpoch!=frame.tracking.epoch||cleared){
   slots={};slotWeapons={};occupied=0;status.Clear();owner=player;inventory=inv;ownerEpoch=frame.tracking.epoch;
  }
  if(readStats){
   using Scalar=float(__fastcall*)(std::uintptr_t);
   // Health and maximum are native internal units: HUD's producer scales by .1.
   const float health=reinterpret_cast<Scalar>(base+0x158B4D0)(0);
   const float maximum=reinterpret_cast<Scalar>(base+0x158B4F0)(0);
   out->values.health=equipment::DisplayValue(health,.1f);
   out->values.maxHealth=equipment::DisplayValue(maximum,.1f);
   const auto psi=*reinterpret_cast<const std::uintptr_t*>(player+0x678);
   if(psi){out->values.psi=equipment::DisplayValue(reinterpret_cast<Scalar>(base+0x15ACFE0)(psi));
    out->values.maxPsi=equipment::DisplayValue(reinterpret_cast<Scalar>(base+0x15ACFA0)(psi));}
   const auto statuses=*reinterpret_cast<const std::uintptr_t*>(player+0x678+0xC0);
   if(statuses){
    const auto armor=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,int)>(base+0x148F000)(statuses,11);
    if(armor){const float maximumArmor=reinterpret_cast<Scalar>(base+0x10B1F40)(armor+0x30);
     const float damage=*reinterpret_cast<const float*>(armor+0x18);
     if(std::isfinite(maximumArmor)&&maximumArmor>0&&std::isfinite(damage)&&damage>=0&&damage<=maximumArmor)
      out->values.suit=equipment::DisplayValue((1-damage/maximumArmor)*100.f);
    }
   }
   out->stamp=now;out->epoch=frame.tracking.epoch;
  }
  if(slot<0)return 0;
  const auto weapon=player+0x14B8;
  if(!inv||*reinterpret_cast<const std::uintptr_t*>(weapon+0x48)!=player||
     *reinterpret_cast<const unsigned char*>(weapon+0x78)||
     *reinterpret_cast<const unsigned char*>(player+0x7BC)||
     *reinterpret_cast<const int*>(input+0x94)||
     !reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(base+0x1273EB0)(weapon))return ERROR_NOT_READY;
  const auto current=*reinterpret_cast<const std::uint32_t*>(weapon+0x58);
  if(!slots[slot]||slots[slot]==current){
   // Only a currently validated local weapon can be assigned; consumables and
   // guessed entity IDs never enter a holster. The native animation does stow.
   EquippedRig rig{};
   if(!current||!TryGetEquippedRig(player,rig)||rig.itemId!=current)return ERROR_NOT_READY;
   reinterpret_cast<void(__fastcall*)(std::uintptr_t,bool,bool)>(base+0x12773A0)(weapon,true,false);
   slots[slot]=current;slotWeapons[slot]=rig.weapon;
   *didStow=true;
  }else{
   const auto stored=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uint32_t)>(base+0x16A5650)(slots[slot]);
   if(!stored||stored!=slotWeapons[slot]||*reinterpret_cast<const std::uint32_t*>(stored+0x60)!=0x7777||
      *reinterpret_cast<const std::uint32_t*>(stored+0x38)!=slots[slot]){
    slots[slot]=0;slotWeapons[slot]=0;occupied=static_cast<unsigned>((slots[0]!=0)+(slots[1]!=0));
    return ERROR_NOT_READY;
   }
   // Equip retains IsEquippable, transition, progression and CanEquip checks.
   // Item no longer owned/equippable => no successful draw, never force fields.
   const bool accepted=reinterpret_cast<bool(__fastcall*)(std::uintptr_t,std::uint32_t)>(base+0x1274820)(weapon,slots[slot]);
   if(!accepted)return ERROR_NOT_READY;
  }
  occupied=static_cast<unsigned>((slots[0]!=0)+(slots[1]!=0));return 0;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
void UpdateHolsterInput(const TrackingFrame& f,bool valid){
 static std::uint64_t epoch=0,reference=0;static long long previous=0;
 if(!valid||!HolstersEnabled()||fault.load()||!HudGameplayInputAllowed()||
    !FreshSample(MonotonicNanoseconds(),f.publishedNs)){
  gesture.Reset();request={};gripOwned=false;previous=0;return;
 }
 const auto ref=HeadTrackingReferenceGeneration();
 if(epoch!=f.epoch||reference!=ref){gesture.Reset();request={};epoch=f.epoch;reference=ref;previous=0;}
 const float dt=previous&&f.displayTime>previous?static_cast<float>(f.displayTime-previous)*1e-9f:0;
 if(f.displayTime==previous)return;
 previous=f.displayTime;
 const auto& r=f.hands[1];const auto& l=f.hands[0];
 valid=valid&&HolstersEnabled()&&!fault.load()&&HudGameplayInputAllowed()&&!(ref&1)&&
  FreshSample(MonotonicNanoseconds(),f.publishedNs)&&
  IsPoseUsable(f.head,f.headValidity,200000000)&&IsPoseUsable(r.gripPose,r.gripValidity,200000000)&&
  !TwoHandedAimHeld()&&l.squeezeValue<.45f&&!r.triggerPressed&&!r.menuAccept&&!r.menuCancel&&!r.weaponWheelPressed;
 const int slot=gesture.Update(f.head,r.gripPose.position,r.gripPressed,valid,dt);
 gripOwned=gesture.OwnsGrip();
 if(!valid)request={};
 else if(slot>=0)request={slot,f.epoch,ref,f.publishedNs};
}
bool HolsterOwnsGrip(){return gripOwned.load();}
void ClearHolsters(){clear=true;}
void UpdateBodyEquipment(const GameplayPoseFrame& frame,bool valid){
 if(InputDrainThreadId()!=GetCurrentThreadId())return;
 if(!valid||fault.load()||!HudGameplayInputAllowed()||
    !FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs)||
    frame.referenceGeneration!=HeadTrackingReferenceGeneration()) {status.Clear();request={};return;}
 const auto now=MonotonicNanoseconds();
 int slot=-1;
 if(request.slot>=0){
  if(HolstersEnabled()&&FreshSample(now,request.stamp)&&request.epoch==frame.tracking.epoch&&
    request.reference==frame.referenceGeneration&&!frame.twoHand.held)slot=request.slot;
  request={};
 }
 // Wrist status now uses native HUD pixels; do not poll duplicate vitals or
 // make a health-read failure disable otherwise independent holster controls.
 status.Clear();
 if(!HolstersEnabled())return;
 Status sample{};bool stored=false;const auto result=Native(frame,slot,false,&sample,&stored);
 if(slot>=0){
  if(!result)++dispatched;else ++refused;
  using N=equipment::SlotNotice;
  PublishSlotFeedback(result?(slot==0?N::HipDenied:N::ChestDenied):stored?(slot==0?N::HipStored:N::ChestStored):(slot==0?N::HipDraw:N::ChestDraw),frame);
 }
 if(result!=0&&result!=ERROR_NOT_READY){fault=true;status.Clear();}
}
bool ReadWristVitals(equipment::Vitals& out,std::uint64_t epoch){
 Status s;if(!WristDisplayEnabled()||fault.load()||!status.TryRead(s)||s.epoch!=epoch||
    !FreshSample(MonotonicNanoseconds(),s.stamp,350000000ull))return false;
 out=s.values;return out.health>=0&&out.maxHealth>0;
}
std::string BodyEquipmentReport(){return " holsterSlots="+std::to_string(occupied.load())+
 " holsterDispatched="+std::to_string(dispatched.load())+" holsterRefused="+std::to_string(refused.load())+
 " equipmentFault="+std::to_string(fault.load())+" wristSamples="+std::to_string(wristSamples.load())+
 " wristLayerFrames="+std::to_string(wristFrames.load());}
void RecordWristLayerFrame(){++wristFrames;}
}
