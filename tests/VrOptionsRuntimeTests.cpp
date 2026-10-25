// Real runtime adapter, with native/XR calls replaced at its boundary.
#include "../src/dll/VrOptionsRuntime.cpp"
#include <iostream>
#include <cstdlib>
namespace {
preyvr::options::Values applied=preyvr::options::Defaults();
bool nativeModal=true;unsigned pauseTaps=0,recenters=0,calibrations=0,clears=0,holsterClears=0;
DWORD viewResultForTest=0;
bool supersedeViewForTest=false;
preyvr::dll::TrackingFrame currentFrame{};
preyvr::dll::UiPointerVisual pointerForTest{};
bool hackingForTest=false;
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
}
namespace preyvr::lifecycle { void Log(std::string_view){} }
namespace preyvr::dll {
DWORD SetNativeFlightControls(unsigned v){applied[options::NativeFlight]=v;return 0;}
unsigned NativeFlightControlsEnabled(){return applied[options::NativeFlight];}
unsigned useEffective=0,carryEffective=0,grenadeEffective=0;
DWORD SetUseHand(unsigned v){useEffective=v;return 0;}
DWORD SetCarryHand(unsigned v){carryEffective=v;return 0;}
DWORD SetGrenadeHand(unsigned v){grenadeEffective=v;return 0;}
DWORD SetPsiTargetMode(unsigned v){applied[options::PsiTarget]=v;return 0;}unsigned PsiTargetMode(){return applied[options::PsiTarget];}
DWORD SetMedkitSlot(unsigned v){applied[options::Medkit]=v;return 0;}unsigned MedkitSlotEnabled(){return applied[options::Medkit];}
void SetPhysicalMelee(unsigned v){applied[options::PhysicalMelee]=v;}unsigned PhysicalMeleeEnabled(){return applied[options::PhysicalMelee];}
void SetPhysicalContacts(unsigned v){applied[options::PhysicalContacts]=v;}unsigned PhysicalContactsEnabled(){return applied[options::PhysicalContacts];}
void SetSwingSpeed(unsigned v){applied[options::SwingSpeed]=v;}unsigned SwingSpeed(){return applied[options::SwingSpeed];}
void SetContactStrength(unsigned v){applied[options::ContactStrength]=v;}unsigned ContactStrength(){return applied[options::ContactStrength];}
void QueueHaptic(Hand,haptics::Event,const TrackingFrame&){}
void SetSceneReticle(unsigned v){applied[options::SceneReticle]=v;}
unsigned SceneReticleEnabled(){return applied[options::SceneReticle];}
void SetHapticsEnabled(unsigned v){applied[options::Haptics]=v;}
unsigned HapticsEnabled(){return applied[options::Haptics];}
void SetHapticStrength(unsigned v){applied[options::HapticStrength]=v;}
unsigned HapticStrength(){return applied[options::HapticStrength];}
DWORD SetReticleConvergenceMillimetres(unsigned v){applied[options::ReticleFallback]=v;return 0;}
DWORD ReticleConvergenceMillimetres(){return applied[options::ReticleFallback];}
void ClearHolsters(){++holsterClears;}
DWORD SetDebugOverlay(unsigned v){applied[options::DebugOverlay]=v?1:0;return 0;}
DWORD SetShotRay(unsigned v){applied[options::MuzzleAim]=v;return 0;}
unsigned ShotRayEnabled(){return applied[options::MuzzleAim];}
unsigned bodyYaw=1,bodyRoom=1;
DWORD SetBodyYawFollow(unsigned v){bodyYaw=v;applied[options::BodyFollow]=bodyYaw+bodyRoom*bodyYaw;return 0;}
DWORD SetBodyRoomScale(unsigned v){bodyRoom=v;applied[options::BodyFollow]=bodyYaw+bodyRoom*bodyYaw;return 0;}
unsigned BodyYawFollowEnabled(){return bodyYaw;}
unsigned BodyRoomScaleEnabled(){return bodyRoom;}
DWORD SetVrStereoMode(unsigned v){applied[options::Stereo]=static_cast<int>(v);return 0;}
unsigned VrStereoModeChosen(){return static_cast<unsigned>(applied[options::Stereo]);}
DWORD SetPhysicalCrouch(unsigned v){applied[options::PhysicalCrouch]=static_cast<int>(v);return 0;}
unsigned PhysicalCrouchEnabled(){return static_cast<unsigned>(applied[options::PhysicalCrouch]);}
DWORD SetHeadInWallsFade(unsigned v){applied[options::WallFade]=static_cast<int>(v);return 0;}
unsigned HeadInWallsFadeEnabled(){return static_cast<unsigned>(applied[options::WallFade]);}
DWORD SetBodyPose(unsigned v){applied[options::BodyPose]=static_cast<int>(v);return 0;}
unsigned BodyPoseEnabled(){return static_cast<unsigned>(applied[options::BodyPose]);}
DWORD SetUiPokeEnabled(unsigned v){applied[options::UiPoke]=static_cast<int>(v);return 0;}
unsigned UiPokeEnabled(){return static_cast<unsigned>(applied[options::UiPoke]);}
DWORD SetUiPokeDistanceCm(unsigned v){applied[options::UiPokeDistance]=static_cast<int>(v);return 0;}
unsigned UiPokeDistanceCm(){return static_cast<unsigned>(applied[options::UiPokeDistance]);}
DWORD SetUiPokeTipMm(unsigned v){applied[options::UiPokeTip]=static_cast<int>(v);return 0;}
unsigned UiPokeTipMm(){return static_cast<unsigned>(applied[options::UiPokeTip]);}
DWORD SetHackingControls(unsigned v){applied[options::HackingControls]=static_cast<int>(v);return 0;}
unsigned HackingControlsEnabled(){return static_cast<unsigned>(applied[options::HackingControls]);}
unsigned DebugOverlayMask(){return applied[options::DebugOverlay];}
DWORD SetSnapTurnDegrees(unsigned v){applied[options::Turn]=v;return 0;}
unsigned SnapTurnDegrees(){return applied[options::Turn];}
DWORD SetHeadRelativeMovement(unsigned v){applied[options::HeadRelative]=v;return 0;}
bool HeadRelativeMovementEnabled(){return applied[options::HeadRelative]!=0;}
DWORD SetTurnLaneScale(unsigned v){applied[options::TurnSpeed]=v;return 0;}
unsigned TurnLaneScalePercent(){return applied[options::TurnSpeed];}
DWORD SetTwoHandedAim(unsigned v){applied[options::TwoHand]=v;return 0;}
unsigned TwoHandedAimEnabled(){return applied[options::TwoHand];}
DWORD SetTwoHandGripToggle(unsigned v){applied[options::GripToggle]=v;return 0;}
bool TwoHandGripToggle(){return applied[options::GripToggle]!=0;}
DWORD SetTwoHandSupportSnap(unsigned v){applied[options::SupportSnap]=v;return 0;}
bool TwoHandSupportSnap(){return applied[options::SupportSnap]!=0;}
DWORD SetUiScalePercent(unsigned v){applied[options::UiScale]=v;return 0;}
DWORD UiScalePercent(){return applied[options::UiScale];}
DWORD SetUiFitMarginPercent(unsigned v){applied[options::UiMargin]=v;return 0;}
DWORD UiFitMarginPercent(){return applied[options::UiMargin];}
DWORD SetUiCurveDegrees(unsigned v){applied[options::UiCurve]=v;return 0;}
unsigned UiCurveDegrees(){return applied[options::UiCurve];}
DWORD SetUiGuideEnabled(unsigned v){applied[options::UiGuide]=v;return 0;}
DWORD UiGuideEnabled(){return applied[options::UiGuide];}
bool HudMenuIsOpen(){return nativeModal;}
bool HudHackingIsOpen(){return hackingForTest;}
bool HudMenuStateKnown(){return true;}
bool TryGetTrackingFrame(TrackingFrame& f){f=currentFrame;return true;}
unsigned UiPointerHand(){return 1;}
UiPointerVisual EvaluateUiPointer(const ui::Surface* surface,const TrackingFrame& frame,unsigned hand,std::uint64_t reference,std::uint64_t,UiPointerTracker&){
 Check(&frame==&currentFrame&&hand==1&&reference==2,"options evaluates the current controller frame");
 return surface?pointerForTest:UiPointerVisual{};
}
unsigned long long HeadTrackingReferenceGeneration(){return 2;}
DWORD RecenterHeadTracking(bool height){if(height)++calibrations;else ++recenters;
 if(supersedeViewForTest){supersedeViewForTest=false;QueueViewRequest(2,currentFrame.epoch);}
 return viewResultForTest;}
void ClearUiPointer(){++clears;}
DWORD PostMenuAction(unsigned id,unsigned long long){Check(id==static_cast<unsigned>(input::MenuAction::Start),"only pause posted");++pauseTaps;return 0;}
}
int main(){
 using namespace preyvr;using namespace preyvr::dll;
 wchar_t temp[MAX_PATH]{};Check(GetTempPathW(MAX_PATH,temp)!=0,"temporary directory");
 const auto sandbox=std::filesystem::path(temp)/(L"PreyVR-options-test-"+std::to_wstring(GetCurrentProcessId()));
 Check(!std::filesystem::exists(sandbox),"test sandbox must be new");
 Check(SetEnvironmentVariableW(L"LOCALAPPDATA",sandbox.c_str())!=0,"process-only settings destination");
 SetEnvironmentVariableW(L"PREYVR_DEBUG_OVERLAY",nullptr);SetEnvironmentVariableW(L"PREYVR_MUZZLE_AIM",nullptr);
 std::filesystem::create_directories(sandbox/L"PreyVR");
 {std::ofstream initial(sandbox/L"PreyVR"/L"vr-options.ini");initial<<options::Serialize(options::Defaults());}
 SetDebugOverlay(255);SetShotRay(1);
 LoadVrOptions();Check(VrOptionsReady()&&!PsychoscopeGestureEnabled(),"startup gesture off");
 Check(DebugOverlayMask()==0&&ShotRayEnabled()==0,"saved OFF defeats old debug/muzzle lane values");
 Check(!useEffective&&!carryEffective&&!grenadeEffective,"loading options outside VR cannot enable overrides");
 ApplyVrWorldOptions(true);Check(useEffective&&!carryEffective&&!grenadeEffective,"only opted-in features activate");
 Apply(options::CarryHand,1);Apply(options::GrenadeHand,1);
 Check(carryEffective&&grenadeEffective,"world preferences reach native adapter switches");
 ApplyVrWorldOptions(false);Check(!useEffective&&!carryEffective&&!grenadeEffective,"VR stop disables all world overrides");
 Check(VrOptionsValues()[options::CarryHand]&&VrOptionsValues()[options::GrenadeHand],"shutdown preserves preferences");
 ApplyVrWorldOptions(true);Check(useEffective&&carryEffective&&grenadeEffective,"VR restart reapplies preferences");
 Apply(options::UseHand,0);Check(!useEffective&&!carryEffective,"right-hand/native mode withdraws left carry");
 Apply(options::UseHand,1);Apply(options::CarryHand,0);Apply(options::GrenadeHand,0);
 ApplyVrWorldOptions(false);
 SetSnapTurnDegrees(75);SetUiFitMarginPercent(95);
 Check(VrOptionsValues()[options::Turn]==75&&VrOptionsValues()[options::UiMargin]==95,"UI reflects live command-channel changes");
 currentFrame.epoch=1;currentFrame.headValidity={true,true,true,true,0};
 currentFrame.hands[1].aimValidity={true,true,true,true,0};
 auto tick=[&]{currentFrame.displayTime+=10000000;currentFrame.publishedNs=MonotonicNanoseconds();return ProcessVrOptionsInput(currentFrame);};
 tick();tick();RequestVrOptions(true);tick();tick();
 Check(VrOptionsOpen()&&VrOptionsInputOwned(),"adapter claims input");
 auto& r=currentFrame.hands[1];r.gripPressed=true;tick();r.gripPressed=false;tick();
 Check(VrOptionsPage()==1,"hands page");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(applied[options::GripToggle]==1,"menu applies toggle preference");
 std::ifstream saved(sandbox/L"PreyVR"/L"vr-options.ini");std::string data((std::istreambuf_iterator<char>(saved)),{});saved.close();
 auto persisted=options::Defaults();Check(options::Parse(data,persisted)&&persisted[options::GripToggle]==1,"menu choice persisted");
 // Move to scope gesture and enable it through the actual adapter.
 for(int n=0;n<2;++n){r.thumbstickY=-1;tick();r.thumbstickY=0;tick();}
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(PsychoscopeGestureEnabled(),"gesture opt-in through menu reaches consumer");
 for(int n=0;n<2;++n){r.gripPressed=true;tick();r.gripPressed=false;tick();}
 Check(VrOptionsPage()==3,"equipment page reached");
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(HolstersEnabled(),"holster opt-in reaches input consumer");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(!WristDisplayEnabled()&&WristSizePercent()==100,"wrist toggle (on by default) reaches layer consumer");
 for(int n=0;n<2;++n){r.thumbstickY=-1;tick();r.thumbstickY=0;tick();}
 const auto clearBefore=holsterClears;
 r.menuAccept=true;tick();r.menuAccept=false;tick();
 Check(holsterClears==clearBefore+1,"clear holsters invokes its own action");
 r.gripPressed=true;tick();r.gripPressed=false;tick();
 Check(VrOptionsPage()==4,"feedback page reached");
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(SceneReticleEnabled()==1,"scene reticle opt-in reaches producer");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(ReticleConvergenceMillimetres()==10500,"fallback setting retains sub-metre precision");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(!HapticsEnabled(),"haptics disable reaches XR adapter");
 r.gripPressed=true;tick();r.gripPressed=false;tick();
 Check(VrOptionsPage()==5&&!PhysicalMeleeEnabled()&&!PhysicalContactsEnabled(),"physical features independently off by default");
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(PhysicalMeleeEnabled()&&!PhysicalContactsEnabled(),"physical wrench opt-in reaches consumer independently");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(PhysicalContactsEnabled(),"object nudge opt-in reaches consumer");
 r.gripPressed=true;tick();r.gripPressed=false;tick();
 Check(VrOptionsPage()==6&&PsiTargetMode()==0&&!MedkitSlotEnabled(),"abilities start with native behavior");
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(PsiTargetMode()==1,"head-directed psi reaches adapter");
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(PsiTargetMode()==2,"controller-directed psi reaches adapter");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(MedkitSlotEnabled(),"medkit opt-in reaches adapter");
 r.gripPressed=true;tick();r.gripPressed=false;tick();
 Check(VrOptionsPage()==7,"setup page reached");
 r.menuAccept=true;tick();r.menuAccept=false;tick();
 viewResultForTest=3;ServiceVrOptions();
 Check(calibrations==1&&recenters==0&&VrSetupMessage()==L"WAITING FOR CURRENT HEADSET POSE","height calibration retries a busy reference");
 viewResultForTest=0;ServiceVrOptions();ServiceVrOptions();
 Check(calibrations==2&&recenters==0&&VrSetupMessage()==L"CURRENT POSTURE CALIBRATED / VR VIEW RESET","successful calibration reports completion and stops retrying");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(recenters==1&&calibrations==2&&VrSetupMessage()==L"VR VIEW RESET / HEIGHT BASELINE PRESERVED","ordinary reset preserves the posture baseline");
 r.menuAccept=true;tick();r.menuAccept=false;tick();viewResultForTest=2;ServiceVrOptions();ServiceVrOptions();
 Check(recenters==2&&VrSetupMessage()==L"LOOK STRAIGHT AHEAD AND TRY AGAIN","vertical view refusal never automatically retries");
 r.menuAccept=true;tick();r.menuAccept=false;tick();++currentFrame.epoch;ServiceVrOptions();
 Check(recenters==2&&VrSetupMessage()==L"NO CURRENT HEADSET POSE / TRY AGAIN","request from an old tracking session never executes");
 // A new request arriving while the worker calls the native boundary must keep
 // its own action/epoch/deadline and must not receive the older result.
 QueueViewRequest(1,currentFrame.epoch);supersedeViewForTest=true;viewResultForTest=3;
 ServiceVrOptions();Check(VrSetupMessage()==L"WAITING FOR CURRENT HEADSET POSE","superseded reset cannot publish a result");
 viewResultForTest=0;ServiceVrOptions();
 Check(recenters==3&&calibrations==3&&VrSetupMessage()==L"CURRENT POSTURE CALIBRATED / VR VIEW RESET","new calibration never inherits older reset retry");
 QueueViewRequest(1,currentFrame.epoch);
 {std::lock_guard lock(viewMutex);viewPending->deadline=0;}
 ServiceVrOptions();Check(recenters==3,"expired request never reaches native backend");
 // Native menu changes behind us: held confirm cannot leak to the game.
 r.menuAccept=true;nativeModal=false;Check(tick()&&!VrOptionsOpen(),"native menu closes options");
 Check(tick(),"held input quarantined");r.menuAccept=false;tick();Check(!tick(),"neutral releases ownership");
 ResetVrOptionsSession();Check(!VrOptionsInputOwned(),"teardown clears ownership");
 Check(pauseTaps==0,"opening from native menu posts no redundant pause");
 // The 3D VIEW row, alone on the last tab (one tab back from the first): alternate eyes by default, the stick
 // raises it to full stereo and lowers it back, then to mono.
 nativeModal=true;for(int n=0;n<4&&!VrOptionsOpen();++n){RequestVrOptions(true);tick();tick();}
 Check(VrOptionsOpen(),"options reopen");
 for(unsigned n=0;n<options::Pages&&VrOptionsPage()!=8;++n){r.gripPressed=true;tick();r.gripPressed=false;tick();}
 Check(VrOptionsPage()==8&&applied[options::Stereo]==1,"view tab reached with alternate eyes by default");
 r.thumbstickX=1;tick();r.thumbstickX=0;tick();ServiceVrOptions();
 Check(applied[options::Stereo]==2,"3D view raised to full stereo reaches VR mode");
 r.thumbstickX=-1;tick();r.thumbstickX=0;tick();ServiceVrOptions();
 Check(applied[options::Stereo]==1,"3D view lowered to alternate eyes reaches VR mode");
 r.thumbstickX=-1;tick();r.thumbstickX=0;tick();ServiceVrOptions();
 Check(applied[options::Stereo]==0&&VrOptionsValues()[options::Stereo]==0,"3D view lowered to mono reaches VR mode");
 for(int rowDown=0;rowDown<3;++rowDown){r.thumbstickY=-1;tick();r.thumbstickY=0;tick();}
 r.menuAccept=true;tick();r.menuAccept=false;tick();
 Check(VrOptionsValues()[options::BodyPose]==0,"body posing pending edit survives bit 32 boundary");
 ServiceVrOptions();Check(!BodyPoseEnabled(),"body posing opt-out reaches adapter");
 r.thumbstickY=1;tick();r.thumbstickY=0;tick();r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(!HeadInWallsFadeEnabled(),"wall fade high-bit edit reaches adapter");
 r.thumbstickY=1;tick();r.thumbstickY=0;tick();r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(!PhysicalCrouchEnabled(),"physical crouch high-bit edit reaches adapter");
 r.gripPressed=true;tick();r.gripPressed=false;tick();Check(VrOptionsPage()==9,"world tab reached");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(VrOptionsValues()[options::CarryHand]&&!carryEffective,"last-bit setting persists while VR is off");
 ApplyVrWorldOptions(true);Check(carryEffective,"saved menu carry preference activates with VR");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(grenadeEffective,"high-bit grenade edit reaches runtime");
 r.thumbstickY=-1;tick();r.thumbstickY=0;tick();r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(NativeFlightControlsEnabled()==1,"flight opt-in reaches native adapter through high-bit mailbox");
 ApplyVrWorldOptions(false);
 r.gripPressed=true;tick();r.gripPressed=false;tick();Check(VrOptionsPage()==10,"input tab reached");
 r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();
 Check(UiPokeEnabled()==1,"poke opt-in reaches adapter");
 for(int edit=0;edit<3;++edit){r.thumbstickY=-1;tick();r.thumbstickY=0;tick();r.menuAccept=true;tick();r.menuAccept=false;tick();ServiceVrOptions();}
 Check(UiPokeDistanceCm()==70&&UiPokeTipMm()==90&&HackingControlsEnabled()==1,"new input rows persist through high-bit mailbox");
 const ui::Surface touchSurface{{Pose{{},{0,0,-.65f}},.6f,.4f},0};SetVrOptionsSurface(&touchSurface,2);
 pointerForTest.active=pointerForTest.touch=true;pointerForTest.hit=ui::RayHit{.8f,.31f,0,{},true};
 tick();pointerForTest.pressed=true;tick();pointerForTest.pressed=false;tick();ServiceVrOptions();
 Check(UiPokeEnabled()==0,"poke contact selects settings through shared menu path");
 pointerForTest={};
 ResetVrOptionsSession();
 // A launch override is explicit, and disappears on an ordinary subsequent load.
 SetEnvironmentVariableW(L"PREYVR_DEBUG_OVERLAY",L"3");SetEnvironmentVariableW(L"PREYVR_MUZZLE_AIM",L"1");
 ready=false;LoadVrOptions();Check(DebugOverlayMask()==1&&ShotRayEnabled()==1,"explicit launch overrides saved OFF");
 SetEnvironmentVariableW(L"PREYVR_DEBUG_OVERLAY",nullptr);SetEnvironmentVariableW(L"PREYVR_MUZZLE_AIM",nullptr);
 ready=false;LoadVrOptions();Check(DebugOverlayMask()==0&&ShotRayEnabled()==0,"normal load restores saved OFF");
 Check(NativeFlightControlsEnabled()==1,"flight preference roundtrips to disk");
 std::filesystem::remove(sandbox/L"PreyVR"/L"vr-options.ini");
 std::filesystem::remove(sandbox/L"PreyVR");std::filesystem::remove(sandbox);
 std::cout<<"Runtime menu, native-input ownership, settings application and disk roundtrip passed\n";
}
