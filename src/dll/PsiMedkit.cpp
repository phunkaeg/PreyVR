#include "PsiMedkit.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "Haptics.h"
#include "SlotFeedback.h"
#include "MinHookInit.h"
#include "preyvr/PsiTargeting.h"
#include "preyvr/MedkitSlot.h"
#include "preyvr/PsiMedkitNative.h"
#include "preyvr/PsychoscopeNative.h"
#include <MinHook.h>
#include <atomic>
#include <mutex>

namespace preyvr::dll {
namespace {
namespace native=abilities::native;
std::atomic<unsigned> mode{0},medkit{0};
std::atomic<std::uint64_t> generation{1};
std::atomic<bool> installed{false},fault{false},medkitGrip{false};
std::atomic<unsigned long long> previews{0},rayReads{0},casts{0},rejectedCasts{0},
    medkitUsed{0},medkitAbsent{0},medkitDenied{0};
std::atomic<DWORD> lastError{0};
std::mutex installMutex;
using UpdateFn=void(__fastcall*)(std::uintptr_t,float);
using ActivateFn=bool(__fastcall*)(std::uintptr_t);
using SelectFn=bool(__fastcall*)(std::uintptr_t,int);
using RayFn=psi::Ray*(__fastcall*)(std::uintptr_t,psi::Ray*);
using CandidateFn=bool(__fastcall*)(std::uintptr_t,std::uintptr_t,bool);
UpdateFn originalUpdate=nullptr;
ActivateFn originalActivate=nullptr;
SelectFn originalSelect=nullptr;
RayFn originalRay=nullptr;
CandidateFn originalCandidate=nullptr;
thread_local psi::Context* context=nullptr;
thread_local psi::Sample preview{};

struct Owner {std::uintptr_t player=0,power=0,focus=0,inventory=0;int selected=-1;std::uintptr_t selectedPower=0;};
DWORD ReadOwner(Owner* out){
 __try {
    if(!InputDrainThreadId()||InputDrainThreadId()!=GetCurrentThreadId())return ERROR_INVALID_THREAD_ID;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!base)return ERROR_NOT_READY;
    out->player=reinterpret_cast<std::uintptr_t(__fastcall*)()>(base+native::GetPlayer)();
    if(!out->player)return ERROR_NOT_READY;
    const auto psiOwner=*reinterpret_cast<const std::uintptr_t*>(out->player+0x678);
    if(!psiOwner)return ERROR_NOT_READY;
    out->power=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(base+native::GetPower)(out->player);
    if(out->power!=psiOwner+8)return ERROR_INVALID_ADDRESS;
    out->focus=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(base+native::GetFocus)(out->player+0x678);
    out->inventory=*reinterpret_cast<const std::uintptr_t*>(out->player+0x1810);
    out->selected=*reinterpret_cast<const int*>(out->power+0x230);
    if(out->selected>=0&&out->selected<16)
        out->selectedPower=*reinterpret_cast<const std::uintptr_t*>(out->power+0x40+8*out->selected);
    return ERROR_SUCCESS;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
bool MakeContext(std::uintptr_t component,psi::Context& ctx){
    GameplayPoseFrame f{};Owner owner{};
    const auto m=mode.load();
    if(!m||fault.load()||!HudGameplayInputAllowed()||ReadOwner(&owner)||owner.power!=component||
       !TryGetGameplayPoseFrame(f)||f.player!=owner.player||!f.cameraCentreValid||!f.headYawUsable||
       !FreshSample(MonotonicNanoseconds(),f.tracking.publishedNs,100000000)||
       !IsPoseUsable(f.tracking.head,f.tracking.headValidity,100000000)||
       f.referenceGeneration!=HeadTrackingReferenceGeneration()||(f.referenceGeneration&1))return false;
    const auto& left=f.tracking.hands[0];
    const auto ray=psi::BuildRay(static_cast<psi::Mode>(m),f.yaw,f.cameraCentre,
        f.tracking.head,left.aimPose,m==1?f.tracking.headValidity:left.aimValidity);
    if(!ray)return false;
    ctx.sample={*ray,f.player,component,owner.selectedPower,owner.selected,f.tracking.epoch,
        f.referenceGeneration,generation.load(),f.tracking.publishedNs,HudMenuEpoch()};
    ctx.active=true;return true;
}
// finally restores even when native code raises an exception. No native state
// is written, and no native simulation/targeting callback is repeated.
void InvokeUpdate(std::uintptr_t self,float dt,psi::Context* ctx){
    auto* saved=context;context=ctx;
    __try {originalUpdate(self,dt);} __finally {context=saved;}
}
bool InvokeActivate(std::uintptr_t self,psi::Context* ctx){
    auto* saved=context;context=ctx;bool result=false;
    __try {result=originalActivate(self);} __finally {context=saved;}
    return result;
}
bool InvokeSelect(std::uintptr_t self,int id,psi::Context* ctx){
    auto* saved=context;context=ctx;bool result=false;
    __try {result=originalSelect(self,id);} __finally {context=saved;}
    return result;
}
psi::Ray* __fastcall RayHook(std::uintptr_t receiver,psi::Ray* result){
    auto* out=originalRay(receiver,result);
    if(context&&!fault.load()&&mode.load()&&context->sample.generation==generation.load()&&
       context->sample.reference==HeadTrackingReferenceGeneration()&&
       context->sample.menu==HudMenuEpoch()&&
       FreshSample(MonotonicNanoseconds(),context->sample.stamp,100000000)&&
       psi::Override(context,receiver,out))++rayReads;
    return out;
}
bool __fastcall CandidateHook(std::uintptr_t parameters,std::uintptr_t entity,bool hudPreferred){
    // The native HUD candidate normally bypasses the angle check. It still
    // has to pass the native VR ray cone, distance, target type and LOS checks.
    if(context&&context->active&&context->reads&&mode.load()&&!fault.load()&&
       context->sample.generation==generation.load()&&
       context->sample.reference==HeadTrackingReferenceGeneration()&&
       context->sample.menu==HudMenuEpoch()&&
       FreshSample(MonotonicNanoseconds(),context->sample.stamp,100000000))hudPreferred=false;
    return originalCandidate(parameters,entity,hudPreferred);
}
void __fastcall UpdateHook(std::uintptr_t self,float dt){
    if(!mode.load()||fault.load()){originalUpdate(self,dt);return;}
    psi::Context ctx{};const bool valid=MakeContext(self,ctx);
    if(valid||preview.component==self)preview={};
    InvokeUpdate(self,dt,valid?&ctx:nullptr);
    if(valid&&ctx.sample.power&&ctx.sample.generation==generation.load()){
        preview=ctx.sample;++previews;
    }
}
bool __fastcall SelectHook(std::uintptr_t self,int id){
    if(!mode.load()||fault.load())return originalSelect(self,id);
    psi::Context ctx{};const bool valid=MakeContext(self,ctx);
    if(valid||preview.component==self)preview={};
    return InvokeSelect(self,id,valid?&ctx:nullptr);
}
bool __fastcall ActivateHook(std::uintptr_t self){
    if(!mode.load()||fault.load())return originalActivate(self);
    Owner owner{};const auto ownerError=ReadOwner(&owner);
    if(ownerError==ERROR_INVALID_THREAD_ID||(!ownerError&&owner.power!=self))return originalActivate(self);
    psi::Context ctx{};
    if(!MakeContext(self,ctx)||!psi::Castable(preview,ctx.sample,MonotonicNanoseconds())){
        ++rejectedCasts;return false;
    }
    // Native powers have already cached the preview target. Preserve its ray
    // for any activation-time readers rather than substituting a newer ray.
    ctx.sample=preview;
    const bool result=InvokeActivate(self,&ctx);preview={};
    if(result)++casts;
    return result;
}
DWORD Gate(){
 __try {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!base)return ERROR_NOT_READY;
    for(const auto& c:native::Contracts){
        std::uint64_t h=14695981039346656037ull;
        const auto* b=reinterpret_cast<const unsigned char*>(base+c.rva);
        for(std::size_t i=0;i<c.size;++i)h=(h^b[i])*1099511628211ull;
        if(h!=c.hash)return ERROR_BAD_EXE_FORMAT;
    }
    return ERROR_SUCCESS;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
DWORD Install(){
    std::lock_guard lock(installMutex);
    if(installed.load())return 0;
    const auto gated=Gate();if(gated)return gated;
    if(!EnsureGameplayPoseObservation())return ERROR_NOT_READY;
    EnsureMinHook();
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    struct Hook {std::uintptr_t rva;void* detour;void** original;};
    const Hook hooks[]={{native::Reticle,reinterpret_cast<void*>(&RayHook),reinterpret_cast<void**>(&originalRay)},
      {native::Update,reinterpret_cast<void*>(&UpdateHook),reinterpret_cast<void**>(&originalUpdate)},
      {native::Activate,reinterpret_cast<void*>(&ActivateHook),reinterpret_cast<void**>(&originalActivate)},
      {native::Select,reinterpret_cast<void*>(&SelectHook),reinterpret_cast<void**>(&originalSelect)},
      {native::Candidate,reinterpret_cast<void*>(&CandidateHook),reinterpret_cast<void**>(&originalCandidate)}};
    unsigned made=0;bool queued=true;
    for(const auto& h:hooks){
        auto* target=reinterpret_cast<void*>(base+h.rva);
        if(MH_CreateHook(target,h.detour,h.original)!=MH_OK)break;
        ++made;
        if(MH_QueueEnableHook(target)!=MH_OK){queued=false;break;}
    }
    if(made!=5||!queued||MH_ApplyQueued()!=MH_OK){
        for(unsigned i=0;i<made;++i){auto* target=reinterpret_cast<void*>(base+hooks[i].rva);MH_DisableHook(target);MH_RemoveHook(target);}
        return ERROR_INVALID_FUNCTION;
    }
    installed=true;return 0;
}

struct AimOwnership {Owner owner{};std::uint64_t epoch=0,reference=0,generation=0;int selected=-1;};
AimOwnership owned; // game producer thread only, never retained for dereference
psi::Trigger trigger;
equipment::MedkitSlot slot;
std::uint64_t inputOwner=0,inputEpoch=0,inputReference=0,inputGeneration=0,inputMenu=0;
long long previousTime=0;
bool NativeFocusOwned(const Owner& owner){
    return owned.owner.player==owner.player&&owned.owner.power==owner.power&&owned.owner.focus==owner.focus&&
        owned.selected==owner.selected&&owned.owner.selectedPower==owner.selectedPower&&owner.focus;
}
DWORD StopOwned(bool afterCast=false){
 __try {
    if(!owned.owner.player)return 0;
    Owner current{};const auto error=ReadOwner(&current);
    if(error)return error;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    const bool sameFocus=current.player==owned.owner.player&&current.power==owned.owner.power&&current.focus&&
        current.focus==owned.owner.focus;
    // A power may clear its selection inside its own native Start callback.
    // After our synchronous activation, release our focus even in that case.
    if(sameFocus&&(afterCast||NativeFocusOwned(current))&&!*reinterpret_cast<const unsigned char*>(current.focus+0x88))
        reinterpret_cast<void(__fastcall*)(std::uintptr_t,bool)>(base+native::FocusStop)(current.focus,true);
    owned={};return 0;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
DWORD Begin(const GameplayPoseFrame& frame){
 __try {
    Owner owner{};const auto error=ReadOwner(&owner);if(error)return error;
    if(owner.player!=frame.player||!owner.focus||*reinterpret_cast<const unsigned char*>(owner.focus+0x390))return ERROR_NOT_READY;
    const int equipped=*reinterpret_cast<const int*>(owner.power+0x234);
    if(equipped<0||equipped>=16)return ERROR_NOT_READY;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(!reinterpret_cast<bool(__fastcall*)(std::uintptr_t,bool)>(base+native::FocusStart)(owner.focus,false))return ERROR_NOT_READY;
    owned={owner,frame.tracking.epoch,frame.referenceGeneration,generation.load(),equipped};
    if(!reinterpret_cast<SelectFn>(base+native::Select)(owner.power,equipped)){
        // Selection can fail before selected ID changes; we still own the
        // newly opened focus mode and must release its time/input policy.
        reinterpret_cast<void(__fastcall*)(std::uintptr_t,bool)>(base+native::FocusStop)(owner.focus,false);
        owned={};return ERROR_NOT_READY;
    }
    Owner selected{};
    if(ReadOwner(&selected)||selected.player!=owner.player||selected.power!=owner.power||
       selected.focus!=owner.focus||selected.selected!=equipped||!selected.selectedPower){
        owned={};return ERROR_NOT_READY;
    }
    owned.owner=selected;
    return 0;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
DWORD Cast(const GameplayPoseFrame& frame){
 __try {
    Owner current{};const auto error=ReadOwner(&current);if(error)return error;
    if(!NativeFocusOwned(current)||owned.epoch!=frame.tracking.epoch||owned.reference!=frame.referenceGeneration||
       owned.generation!=generation.load()||!*reinterpret_cast<const unsigned char*>(current.focus+0x390)||
       *reinterpret_cast<const unsigned char*>(current.focus+0x88))return ERROR_NOT_READY;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    const bool accepted=reinterpret_cast<ActivateFn>(base+native::Activate)(current.power);
    const auto stop=StopOwned(true);
    return stop?stop:accepted?ERROR_SUCCESS:ERROR_NOT_READY;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
DWORD ConsumeMedkit(const GameplayPoseFrame& frame,int* result){
 __try {
    Owner current{};const auto error=ReadOwner(&current);if(error)return error;
    if(current.player!=frame.player||!current.inventory||!current.focus||
       *reinterpret_cast<const unsigned char*>(current.focus+0x390)||
       *reinterpret_cast<const unsigned char*>(current.player+0x7BC))return ERROR_NOT_READY;
    const auto input=current.player+scope::InputOffset;
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if(*reinterpret_cast<const std::uintptr_t*>(input)!=base+scope::InputVtableRva||
       *reinterpret_cast<const std::uintptr_t*>(input+0x70)!=current.player)return ERROR_INVALID_ADDRESS;
    if(*reinterpret_cast<const int*>(input+0x94))return ERROR_NOT_READY;
    const auto id=reinterpret_cast<std::uint64_t(__fastcall*)()>(base+native::MedkitArchetype)();
    if(!id)return ERROR_NOT_READY;
    *result=reinterpret_cast<int(__fastcall*)(std::uint64_t)>(base+native::Consume)(id);
    return *result>=0&&*result<=2?ERROR_SUCCESS:ERROR_INVALID_DATA;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
void Error(DWORD result){
    if(result)lastError=result;
    if(result&&result!=ERROR_NOT_READY&&result!=ERROR_INVALID_THREAD_ID)fault=true;
}
}
DWORD SetPsiTargetMode(unsigned value){
    if(value>2)return ERROR_INVALID_PARAMETER;
    if(value){const auto result=Install();Error(result);if(result)return result;}
    mode=value;generation.fetch_add(1);return 0;
}
unsigned PsiTargetMode(){return mode.load();}
DWORD SetMedkitSlot(unsigned value){
    if(value>1)return ERROR_INVALID_PARAMETER;
    if(value){const auto result=Install();Error(result);if(result)return result;}
    medkit=value;generation.fetch_add(1);return 0;
}
unsigned MedkitSlotEnabled(){return medkit.load();}
bool MedkitOwnsGrip(){return medkitGrip.load();}
void UpdatePsiMedkit(const GameplayPoseFrame& frame,bool valid){
    if(InputDrainThreadId()!=GetCurrentThreadId())return;
    const auto& f=frame.tracking;const auto ref=frame.referenceGeneration,gen=generation.load();
    valid=valid&&!fault.load()&&HudGameplayInputAllowed()&&!frame.twoHand.held&&
        FreshSample(MonotonicNanoseconds(),f.publishedNs,100000000)&&
        IsPoseUsable(f.head,f.headValidity,100000000)&&
        IsPoseUsable(f.hands[0].gripPose,f.hands[0].gripValidity,100000000)&&
        !(ref&1)&&ref==HeadTrackingReferenceGeneration();
    const auto menuEpoch=HudMenuEpoch();
    const bool changed=inputOwner!=frame.player||inputEpoch!=f.epoch||inputReference!=ref||inputGeneration!=gen||inputMenu!=menuEpoch;
    if(!valid||changed){
        Error(StopOwned());trigger.Reset();slot.Reset();medkitGrip=false;previousTime=0;preview={};
        inputOwner=frame.player;inputEpoch=f.epoch;inputReference=ref;inputGeneration=gen;inputMenu=menuEpoch;
        if(!valid)return;
    }
    if(previousTime==f.displayTime)return;
    const float dt=previousTime&&f.displayTime>previousTime?static_cast<float>(f.displayTime-previousTime)*1e-9f:0;
    previousTime=f.displayTime;
    if(!std::isfinite(dt)||dt<=0||dt>.2f){
        Error(StopOwned());trigger.Reset();slot.Reset();medkitGrip=false;preview={};return;
    }
    const auto& left=f.hands[0];
    const bool slotAllowed=valid&&medkit.load()&&!left.menuAccept&&!left.menuCancel&&!f.hands[1].gripPressed;
    const bool wasOwned=slot.OwnsGrip();
    const bool use=slot.Update(f.head,left.gripPose.position,left.gripPressed,left.triggerPressed,slotAllowed,dt);
    medkitGrip=slot.OwnsGrip();
    if(!wasOwned&&slot.OwnsGrip()){
        QueueHaptic(Hand::left,haptics::Event::ForegripAttached,f);
        PublishSlotFeedback(equipment::SlotNotice::MedkitReady,frame);
    }
    if(use){
        int result=-1;const auto error=ConsumeMedkit(frame,&result);Error(error);
        if(!error&&result==2){++medkitUsed;QueueHaptic(Hand::left,haptics::Event::WeaponContact,f);}
        else if(!error&&result==0)++medkitAbsent;else ++medkitDenied;
        PublishSlotFeedback(!error&&result==2?equipment::SlotNotice::MedkitUsed:
            !error&&result==0?equipment::SlotNotice::MedkitEmpty:equipment::SlotNotice::MedkitDenied,frame);
    }
    const auto event=trigger.Update(left.triggerPressed,valid&&mode.load()&&!slot.OwnsGrip()&&!left.gripPressed&&
        !left.menuAccept&&!left.menuCancel&&!f.hands[1].gripPressed);
    if(event==psi::TriggerEvent::Begin)Error(Begin(frame));
    else if(event==psi::TriggerEvent::Cast){Error(Cast(frame));Error(StopOwned());}
    else if(event==psi::TriggerEvent::Cancel)Error(StopOwned());
}
std::string PsiMedkitReport(){return " targetMode="+std::to_string(mode.load())+" medkitSlot="+std::to_string(medkit.load())+
    " installed="+std::to_string(installed.load())+" previews="+std::to_string(previews.load())+
    " rayReads="+std::to_string(rayReads.load())+" casts="+std::to_string(casts.load())+
    " castRefused="+std::to_string(rejectedCasts.load())+" medkitGrip="+std::to_string(medkitGrip.load())+
    " medkitUsed="+std::to_string(medkitUsed.load())+" medkitAbsent="+std::to_string(medkitAbsent.load())+
    " medkitDenied="+std::to_string(medkitDenied.load())+" fault="+std::to_string(fault.load())+" error="+std::to_string(lastError.load());}
}
