// Real runtime adapter, with native/XR calls replaced at its boundary.
#include "../src/dll/VrOptionsRuntime.cpp"
#include <iostream>
#include <cstdlib>
namespace {
preyvr::options::Values applied=preyvr::options::Defaults();
bool nativeModal=true;unsigned pauseTaps=0,recenters=0,clears=0;
preyvr::dll::TrackingFrame currentFrame{};
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
}
namespace preyvr::lifecycle { void Log(std::string_view){} }
namespace preyvr::dll {
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
bool HudMenuStateKnown(){return true;}
bool TryGetTrackingFrame(TrackingFrame& f){f=currentFrame;return true;}
unsigned UiPointerHand(){return 1;}
unsigned long long HeadTrackingReferenceGeneration(){return 2;}
DWORD RecenterHeadTracking(bool){++recenters;return 0;}
void ClearUiPointer(){++clears;}
DWORD PostMenuAction(unsigned id,unsigned long long){Check(id==static_cast<unsigned>(input::MenuAction::Start),"only pause posted");++pauseTaps;return 0;}
}
int main(){
 using namespace preyvr;using namespace preyvr::dll;
 wchar_t temp[MAX_PATH]{};Check(GetTempPathW(MAX_PATH,temp)!=0,"temporary directory");
 const auto sandbox=std::filesystem::path(temp)/(L"PreyVR-options-test-"+std::to_wstring(GetCurrentProcessId()));
 Check(!std::filesystem::exists(sandbox),"test sandbox must be new");
 Check(SetEnvironmentVariableW(L"LOCALAPPDATA",sandbox.c_str())!=0,"process-only settings destination");
 LoadVrOptions();Check(VrOptionsReady()&&!PsychoscopeGestureEnabled(),"startup gesture off");
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
 // Native menu changes behind us: held confirm cannot leak to the game.
 r.menuAccept=true;nativeModal=false;Check(tick()&&!VrOptionsOpen(),"native menu closes options");
 Check(tick(),"held input quarantined");r.menuAccept=false;tick();Check(!tick(),"neutral releases ownership");
 ResetVrOptionsSession();Check(!VrOptionsInputOwned(),"teardown clears ownership");
 Check(pauseTaps==0,"opening from native menu posts no redundant pause");
 std::filesystem::remove(sandbox/L"PreyVR"/L"vr-options.ini");
 std::filesystem::remove(sandbox/L"PreyVR");std::filesystem::remove(sandbox);
 std::cout<<"Runtime menu, native-input ownership, settings application and disk roundtrip passed\n";
}
