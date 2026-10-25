#include "VrOptionsRuntime.h"
#include "BodyFollow.h"
#include "AnimIkTakeover.h"
#include "HackingLane.h"
#include "FlightLane.h"
#include "SurfaceGrabLane.h"
#include "InteractionLane.h"
#include "CarryLane.h"
#include "GrenadeLane.h"
#include "VrMode.h"
#include "SceneQuery.h"
#include "PhysicalInteractions.h"
#include "PsiMedkit.h"
#include "Haptics.h"
#include "ReticleFollow.h"
#include "AimTakeover.h"
#include "BodyEquipment.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "MoveLane.h"
#include "InputPost.h"
#include "preyvr/MenuNavigator.h"
#include "preyvr/LatestSnapshot.h"
#include "XrSessionHost.h"
#include "DebugOverlay.h"
#include "ShotRay.h"
#include "preyvr/DebugOverlayScene.h"
#include "Logger.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <mutex>
namespace preyvr::dll {
namespace {
options::Menu menu; // XR thread only
std::array<std::atomic<int>,options::Count> values{};
std::atomic<bool> ready{false},open{false},owned{false},saveFailed{false},scope{false};
std::atomic<bool> holsters{false},wrist{true}; // wrist: options::Defaults()
std::atomic<bool> beltHints{true};
std::atomic<unsigned> wristSize{100};
std::atomic<std::uint64_t> dirty{0};
std::atomic<unsigned> page{0},row{0};
std::atomic<bool> useHand{true},carryHand{false},grenadeHand{false};
bool worldActive=false; // command worker only
std::atomic<int> request{0};
// Latest explicit request wins. Busy reference locks retry for at most one
// second, within the same tracking session; a stale request never fires later.
struct ViewRequest { int action=0;std::uint64_t epoch=0,deadline=0,generation=0; };
std::mutex viewMutex;
std::optional<ViewRequest> viewPending;
std::uint64_t viewGeneration=0;
std::atomic<int> viewResult{-1}; // 1 reset, 2 posture calibration
void QueueViewRequest(int action,std::uint64_t epoch){
 std::lock_guard lock(viewMutex);
 viewPending=ViewRequest{action,epoch,MonotonicNanoseconds()+1000000000ull,++viewGeneration};
 viewResult=-2;
}
std::optional<ui::Surface> pointerSurface;
UiPointerTracker optionsPointerTracker;
UiPointerVisual optionsPointerVisual;
std::uint64_t optionsPointerSequence=0;
std::uint64_t surfaceReference=0;
long long previousTime=0;
std::filesystem::path SettingsPath(){
 wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
 if(!n||n>=32768)return {};
 return std::filesystem::path(path)/L"PreyVR"/L"vr-options.ini";
}
void Apply(unsigned id,int v){
 switch(id){
 case options::UseHand:useHand=v!=0;ApplyVrWorldOptions(worldActive);break;
 case options::CarryHand:carryHand=v!=0;ApplyVrWorldOptions(worldActive);break;
 case options::GrenadeHand:grenadeHand=v!=0;ApplyVrWorldOptions(worldActive);break;
 case options::PsiTarget:SetPsiTargetMode(v);break;
 case options::Medkit:SetMedkitSlot(v);break;
 case options::PhysicalMelee:SetPhysicalMelee(v);break;
 case options::PhysicalContacts:SetPhysicalContacts(v);break;
 case options::SwingSpeed:SetSwingSpeed(v);break;
 case options::ContactStrength:SetContactStrength(v);break;
 case options::SceneReticle:SetSceneReticle(v);break;
 case options::ReticleFallback:SetReticleConvergenceMillimetres(v);break;
 case options::Haptics:SetHapticsEnabled(v);break;
 case options::HapticStrength:SetHapticStrength(v);break;
 case options::Turn:SetSnapTurnDegrees(v);break;
 case options::HeadRelative:SetHeadRelativeMovement(v);break;
 case options::TurnSpeed:SetTurnLaneScale(v);break;
 case options::TwoHand:SetTwoHandedAim(v);break;
 case options::GripToggle:SetTwoHandGripToggle(v);break;
 case options::SupportSnap:SetTwoHandSupportSnap(v);break;
 case options::Psychoscope:scope.store(v!=0);break;
 case options::Holsters:holsters.store(v!=0);ClearHolsters();break;
 case options::Wrist:wrist.store(v!=0);break;
 case options::WristSize:wristSize=v;break;
 case options::BeltHints:beltHints=v!=0;break;
 case options::UiScale:SetUiScalePercent(v);break;
 case options::UiMargin:SetUiFitMarginPercent(v);break;
 case options::UiCurve:SetUiCurveDegrees(v);break;
 case options::UiGuide:SetUiGuideEnabled(v);break;
 case options::DebugOverlay:SetDebugOverlay(v?debugdraw::kAllLayers:0u);break;
 case options::MuzzleAim:SetShotRay(v);break;
 case options::BodyFollow:SetBodyYawFollow(v>=1);SetBodyRoomScale(v>=2);break;
 case options::Stereo:SetVrStereoMode(static_cast<unsigned>(v));break;
 case options::PhysicalCrouch:SetPhysicalCrouch(v!=0);break;
 case options::WallFade:SetHeadInWallsFade(v!=0);break;
 case options::BodyPose:SetBodyPose(v!=0);break;
 case options::UiPoke:SetUiPokeEnabled(v!=0);break;
 case options::UiPokeDistance:SetUiPokeDistanceCm(static_cast<unsigned>(v));break;
 case options::UiPokeTip:SetUiPokeTipMm(static_cast<unsigned>(v));break;
 case options::HackingControls:SetHackingControls(v!=0);break;
 case options::NativeFlight:SetNativeFlightControls(v!=0);break;
 case options::SurfaceGrab:SetSurfaceGrabEnabled(v!=0);break;
 case options::SurfaceReach:SetSurfaceGrabReach(v);break;
 case options::SurfaceStrength:SetSurfacePushStrength(v);break;
 case options::SurfaceLimit:SetSurfacePushLimit(v);break;
 }
}
void Save(){
 try{
  const auto path=SettingsPath();if(path.empty()){saveFailed=true;return;}
  std::filesystem::create_directories(path.parent_path());
  auto temp=path;temp+=L".tmp";
  {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<options::Serialize(VrOptionsValues());out.flush();if(!out){saveFailed=true;return;}}
  saveFailed=!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
 }catch(...){saveFailed=true;}
 if(saveFailed.load())lifecycle::Log("preyvr_options save=failed settings_remain_active=1");
}
}
void ApplyVrWorldOptions(bool active){
 worldActive=active;
 const auto use=SetUseHand(active&&useHand.load()?1:0);
 const auto carry=SetCarryHand(active&&use==0&&useHand.load()&&carryHand.load()?1:0);
 const auto grenade=SetGrenadeHand(active&&grenadeHand.load()?1:0);
 if(use||carry||grenade)lifecycle::Log("preyvr_options world_hooks_unavailable use="+std::to_string(use)+
    " carry="+std::to_string(carry)+" grenade="+std::to_string(grenade));
}
options::Values VrOptionsValues(){
 auto v=options::Defaults();
 v[options::PsiTarget]=PsiTargetMode();v[options::Medkit]=MedkitSlotEnabled();
 v[options::PhysicalMelee]=PhysicalMeleeEnabled();v[options::PhysicalContacts]=PhysicalContactsEnabled();
 v[options::SwingSpeed]=SwingSpeed();v[options::ContactStrength]=ContactStrength();
 v[options::SceneReticle]=SceneReticleEnabled();
 v[options::ReticleFallback]=static_cast<int>(ReticleConvergenceMillimetres());
 v[options::Haptics]=HapticsEnabled();v[options::HapticStrength]=HapticStrength();
 v[options::Turn]=SnapTurnDegrees();v[options::HeadRelative]=HeadRelativeMovementEnabled();
 v[options::TwoHand]=TwoHandedAimEnabled();v[options::GripToggle]=TwoHandGripToggle();v[options::SupportSnap]=TwoHandSupportSnap();
 v[options::TurnSpeed]=TurnLaneScalePercent();
 v[options::UiScale]=UiScalePercent();v[options::UiMargin]=UiFitMarginPercent();
 v[options::UiCurve]=UiCurveDegrees();v[options::UiGuide]=UiGuideEnabled();
 v[options::Psychoscope]=scope.load();
 v[options::Holsters]=holsters.load();v[options::Wrist]=wrist.load();v[options::WristSize]=wristSize.load();
 v[options::BeltHints]=beltHints.load();
 v[options::DebugOverlay]=DebugOverlayMask()!=0;
 v[options::MuzzleAim]=ShotRayEnabled();
 v[options::BodyFollow]=BodyYawFollowEnabled()?(BodyRoomScaleEnabled()?2:1):0;
 v[options::Stereo]=static_cast<int>(VrStereoModeChosen());
 v[options::PhysicalCrouch]=static_cast<int>(PhysicalCrouchEnabled());
 v[options::WallFade]=static_cast<int>(HeadInWallsFadeEnabled());
 v[options::BodyPose]=static_cast<int>(BodyPoseEnabled());
 v[options::UiPoke]=static_cast<int>(UiPokeEnabled());
 v[options::UiPokeDistance]=static_cast<int>(UiPokeDistanceCm());
 v[options::UiPokeTip]=static_cast<int>(UiPokeTipMm());
 v[options::HackingControls]=static_cast<int>(HackingControlsEnabled());
 v[options::NativeFlight]=static_cast<int>(NativeFlightControlsEnabled());
 v[options::SurfaceGrab]=static_cast<int>(SurfaceGrabEnabled());
 v[options::SurfaceReach]=static_cast<int>(SurfaceGrabReach());
 v[options::SurfaceStrength]=static_cast<int>(SurfacePushStrength());
 v[options::SurfaceLimit]=static_cast<int>(SurfacePushLimit());
 v[options::UseHand]=useHand.load();v[options::CarryHand]=carryHand.load();v[options::GrenadeHand]=grenadeHand.load();
 // Read the actual lane settings, including changes made through the command
 // channel. Pending menu edits override only their own row until applied.
 const auto pending=dirty.load();
 for(unsigned i=0;i<options::Count;++i)if(pending&(1ull<<i))v[i]=values[i].load();
 return v;
}
void LoadVrOptions(){
 if(ready.load())return;
 auto v=VrOptionsValues();
 try{
  std::ifstream in(SettingsPath(),std::ios::binary);
  if(in){std::string data;char chunk[8193]{};in.read(chunk,sizeof(chunk));data.assign(chunk,static_cast<size_t>(in.gcount()));
   options::Values parsed{};if(options::Parse(data,parsed))v=parsed;else lifecycle::Log("preyvr_options load=invalid defaults_retained=1");
  }
 }catch(...){lifecycle::Log("preyvr_options load=failed defaults_retained=1");}
 for(unsigned i=0;i<options::Count;++i){
  values[i]=v[i];
  Apply(i,v[i]);
 }
 // Explicit process-scoped launch overrides are distinct from a stale lane
 // value. A saved OFF otherwise wins, including after a previous debug run.
 wchar_t launch[16]{};
 const auto debugLength=GetEnvironmentVariableW(L"PREYVR_DEBUG_OVERLAY",launch,16);
 if(debugLength>0&&debugLength<16){wchar_t* end=nullptr;const auto mask=wcstoul(launch,&end,10);
  if(end!=launch&&!*end&&mask<=255)SetDebugOverlay(static_cast<unsigned>(mask));}
 const auto muzzleLength=GetEnvironmentVariableW(L"PREYVR_MUZZLE_AIM",launch,16);
 if(muzzleLength==1&&(launch[0]==L'0'||launch[0]==L'1'))SetShotRay(launch[0]==L'1'?1:0);
 ready.store(true);
}
void ServiceVrOptions(){
 if(!ready.load())return;
 std::optional<ViewRequest> view;
 {std::lock_guard lock(viewMutex);view=viewPending;viewPending.reset();}
 if(view) {
  TrackingFrame f{};
  const bool current=MonotonicNanoseconds()<view->deadline&&TryGetTrackingFrame(f)&&f.epoch==view->epoch;
  const auto result=current?RecenterHeadTracking(view->action==2):DWORD{1};
  std::lock_guard lock(viewMutex);
  // A newer click or teardown must not inherit the old action's retry/status.
  if(viewGeneration==view->generation){
   if(result==3&&MonotonicNanoseconds()<view->deadline)viewPending=view;
   else {
    viewResult=static_cast<int>(result)+(view->action==2?10:0);
    lifecycle::Log("preyvr_options view_action="+std::to_string(view->action)+" result="+std::to_string(result));
   }
  }
 }
 const auto mask=dirty.exchange(0);
 if(mask){for(unsigned i=0;i<options::Count;++i)if(mask&(1ull<<i))Apply(i,values[i].load());Save();}
}
bool ProcessVrOptionsInput(const TrackingFrame& f){
 options::Input i;const auto& l=f.hands[0];const auto& r=f.hands[1];
 i.valid=ready.load()&&FreshSample(MonotonicNanoseconds(),f.publishedNs)&&IsPoseUsable(f.head,f.headValidity,200000000);
 // Hacking is active gameplay inside a screen: request a native pause before
 // opening settings, so the puzzle cannot keep running behind this panel.
 i.modal=HudMenuStateKnown()&&HudMenuIsOpen()&&!HudHackingIsOpen();i.epoch=f.epoch;
 i.start=l.menuStart||r.menuStart;i.accept=r.menuAccept;i.cancel=r.menuCancel;
 i.previous=l.gripPressed;i.next=r.gripPressed;i.x=r.thumbstickX;i.y=r.thumbstickY;
 const auto hand=UiPointerHand();
 i.dt=previousTime&&f.displayTime>previousTime?static_cast<float>(f.displayTime-previousTime)*1e-9f:0;
 previousTime=f.displayTime;
 // Intersect the current controller sample with the anchored panel; never use
 // a previous frame's hover location to decide a new trigger press.
 optionsPointerVisual=EvaluateUiPointer(pointerSurface?&*pointerSurface:nullptr,f,hand,
     HeadTrackingReferenceGeneration(),surfaceReference,optionsPointerTracker);
 optionsPointerSequence=f.sequence;
 i.trigger=optionsPointerVisual.active&&optionsPointerVisual.pressed;
 if(optionsPointerVisual.hit&&optionsPointerVisual.hit->inside){
  const auto& hit=*optionsPointerVisual.hit;
  i.hover=options::Hit(hit.u,hit.v);i.decrease=hit.u<.70f;
 }
 const auto wanted=request.exchange(0);
 if(wanted>0&&i.modal)menu.RequestOpen();else if(wanted<0)menu.Close();
 const bool wasOwned=menu.OwnsInput();
 const auto change=menu.Update(i);
 if(menu.PauseTap())PostMenuAction(static_cast<unsigned>(input::MenuAction::Start),0);
 if(change.recenter||change.calibrate){
  QueueViewRequest(change.calibrate?2:1,f.epoch);
 }
 if(change.clearHolsters)ClearHolsters();
 if(change.setting>=0){
  const auto id=static_cast<unsigned>(change.setting);
  const auto before=VrOptionsValues()[id],after=options::Adjust(id,before,change.direction);
  values[id]=after;dirty.fetch_or(1ull<<id);
  if(before!=after)QueueHaptic(i.trigger&&hand<2?static_cast<Hand>(hand):Hand::right,haptics::Event::MenuChange,f);
 }
 open=menu.Open();owned=menu.OwnsInput();page=menu.page;row=menu.row;
 if(wasOwned!=menu.OwnsInput())ClearUiPointer();
 return wasOwned||menu.OwnsInput(); // closing frame belongs to this menu too
}
UiPointerVisual OptionsPointer(const ui::Surface& surface){
 (void)surface;TrackingFrame f{};
 if(!open.load()||!TryGetTrackingFrame(f)||!FreshSample(MonotonicNanoseconds(),f.publishedNs)||
    f.sequence!=optionsPointerSequence||surfaceReference!=HeadTrackingReferenceGeneration())return {};
 return optionsPointerVisual;
}
void SetVrOptionsSurface(const ui::Surface* surface,std::uint64_t reference){
 if(surface)pointerSurface=*surface;else {pointerSurface.reset();optionsPointerTracker={};optionsPointerVisual={};}
 surfaceReference=reference;
}
bool VrOptionsOpen(){return open.load();}
bool VrOptionsReady(){return ready.load();}
bool VrOptionsInputOwned(){return owned.load();}
void RequestVrOptions(bool on){request=on?1:-1;}
void ResetVrOptionsSession(){menu={};open=false;owned=false;request=0;{std::lock_guard lock(viewMutex);viewPending.reset();++viewGeneration;viewResult=-1;}pointerSurface.reset();previousTime=0;ClearUiPointer();}
unsigned VrOptionsPage(){return page.load();}
unsigned VrOptionsRow(){return row.load();}
bool VrOptionsSaveFailed(){return saveFailed.load();}
std::wstring VrSetupMessage(){
 const auto result=viewResult.load();
 if(result==-1)return L"";
 if(result==-2)return L"WAITING FOR CURRENT HEADSET POSE";
 if(result==0)return L"VR VIEW RESET / HEIGHT BASELINE PRESERVED";
 if(result==10)return L"CURRENT POSTURE CALIBRATED / VR VIEW RESET";
 if(result%10==2)return L"LOOK STRAIGHT AHEAD AND TRY AGAIN";
 if(result%10==3)return L"REFERENCE BUSY / TRY AGAIN";
 return L"NO CURRENT HEADSET POSE / TRY AGAIN";
}
bool PsychoscopeGestureEnabled(){return scope.load();}
bool HolstersEnabled(){return holsters.load();}
bool WristDisplayEnabled(){return wrist.load();}
unsigned WristSizePercent(){return wristSize.load();}
bool BeltHintsEnabled(){return beltHints.load();}
std::string VrOptionsReport(){return " open="+std::to_string(open.load())+" inputOwned="+std::to_string(owned.load())+
 " saved="+std::to_string(!saveFailed.load())+" psychoscope="+std::to_string(scope.load())+
 " holsters="+std::to_string(holsters.load())+" wrist="+std::to_string(wrist.load())+
 " wristSize="+std::to_string(wristSize.load());}
}
