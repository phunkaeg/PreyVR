// Production main-thread dispatcher, with observable native consumers.
#include "../src/dll/UiPointer.cpp"
#include <cstdlib>
#include <iostream>
#include <vector>
namespace {
struct Event {int kind,x,y;};
std::vector<Event> events;
bool menuKnown=true,menuOpen=true;
bool optionsOwned=false;
bool hackingOpen=false,trackingRead=false;
std::uint64_t trackingReference=2,menuEpoch=0;
preyvr::dll::TrackingFrame trackingFrame{};
DWORD modeError=0,cursorError=0,cancelError=0;
void Require(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
}
namespace preyvr::lifecycle {void Log(std::string_view){}}
namespace preyvr::dll {
bool HudMenuStateKnown(){return menuKnown;}
bool HudMenuIsOpen(){return menuOpen;}
bool HudHackingIsOpen(){return hackingOpen;}
unsigned long long HudMenuEpoch(){return menuEpoch;}
unsigned long long HeadTrackingReferenceGeneration(){return trackingReference;}
bool VrOptionsInputOwned(){return optionsOwned;}
bool TryGetTrackingFrame(TrackingFrame& f){f=trackingFrame;return trackingRead;}
DWORD PostRawInputImmediate(int key,unsigned state,int value){
    Require(key==input::kMouseX&&state==input::kStateChanged&&value==0,"mode event contract");
    events.push_back({-2,0,0});return modeError;
}
DWORD HudDispatchPointer(int event,int x,int y){events.push_back({event,x,y});return cursorError;}
DWORD CancelInventoryPointerCapture(bool* cancelled){events.push_back({-3,0,0});*cancelled=true;return cancelError;}
}
namespace {
using namespace preyvr::dll;
constexpr std::uint64_t now=1000000000;
void Reset(){
    gSample.Clear();gClearGeneration=0;gRetainedSample={};gHand=1;gFault=false;
    gButtons={};gEpoch=gReference=0;gLastX=gLastY=-100;gWasActive=gNativeDown=false;
    gSurface=0;gNativeTracker={};gPokeEnabled=0;gPokeSettings=1;gPokeTipMm=80;
    hackingOpen=trackingRead=false;trackingReference=2;menuEpoch=0;trackingFrame={};
    menuKnown=menuOpen=true;optionsOwned=false;modeError=cursorError=cancelError=0;events.clear();
}
Sample Neutral(){return {true,true,false,330,555,now,4,2,0};}
Sample Press(){auto s=Neutral();DrainPointerSample(&s,now);s.pressed=true;DrainPointerSample(&s,now);Require(gNativeDown,"native button pressed");events.clear();return s;}
void CheckCancelOrder(){
    Require(events.size()==2&&events[0].kind==-3&&events[1].kind==2,"cancel before release, no move while held");
    Require(events[1].x==330&&events[1].y==555,"release uses last valid coordinates");
    Require(!gNativeDown,"release clears native button");
}
void TestContentionAndExpiry(){
    Reset();auto s=Press();
    DrainPointerSample(nullptr,now+1000000);
    Require(events.empty()&&gNativeDown,"read contention retains fresh held sample");
    DrainPointerSample(nullptr,now+200000001);CheckCancelOrder();
    events.clear();s.stamp=now+200000002;
    DrainPointerSample(&s,s.stamp);
    Require(!gNativeDown,"held reacquisition cannot click");
    s.pressed=false;DrainPointerSample(&s,s.stamp);s.pressed=true;DrainPointerSample(&s,s.stamp);
    Require(gNativeDown,"neutral rearms recovered pointer");
}
void TestClearAndReference(){
    Reset();auto s=Press();ClearUiPointer();DrainPointerSample(nullptr,now);CheckCancelOrder();
    events.clear();DrainPointerSample(&s,now);Require(!gNativeDown,"old publication cannot revive after explicit clear");
    Reset();s=Press();s.reference++;DrainPointerSample(&s,now);CheckCancelOrder();
    events.clear();DrainPointerSample(&s,now);Require(!gNativeDown,"recenter while held does not re-click");
    Reset();s=Press();SetUiPointerHand(0);DrainPointerSample(&s,now);CheckCancelOrder();
}
void TestExitAndNormalRelease(){
    Reset();auto s=Press();s.inside=false;s.x=-300;DrainPointerSample(&s,now);CheckCancelOrder();
    events.clear();DrainPointerSample(&s,now);
    Require(!events.empty()&&events.back().kind==0&&events.back().x==-300,"outside hover follows release on next drain");
    s.inside=true;s.x=330;DrainPointerSample(&s,now);Require(!gNativeDown,"outside cancellation requires neutral");
    Reset();s=Press();s.pressed=false;DrainPointerSample(&s,now);
    Require(events.size()==1&&events[0].kind==2,"ordinary release does not cancel inventory placement");
    Reset();s=Press();menuKnown=false;DrainPointerSample(&s,now);CheckCancelOrder();
    Reset();s=Press();optionsOwned=true;DrainPointerSample(&s,now);CheckCancelOrder();
    events.clear();DrainPointerSample(&s,now);Require(!gNativeDown,"options block native pointer press");
}
void TestFault(){
    Reset();auto s=Press();s.x=350;modeError=ERROR_INVALID_ADDRESS;DrainPointerSample(&s,now);
    Require(gFault&&!gNativeDown,"fault cancels and releases outstanding press");
    Require(events.size()==3&&events[0].kind==-2&&events[1].kind==-3&&events[2].kind==2,"fault cleanup order");
    events.clear();DrainPointerSample(&s,now);Require(events.empty(),"fault does not retry native calls");
}
void TestPokeProducer(){
    Reset();SetUiPokeEnabled(1);trackingRead=true;
    auto& f=trackingFrame;f.epoch=1;f.sequence=1;f.publishedNs=preyvr::MonotonicNanoseconds();
    f.hands[1].aimValidity={true,true,true,true,0};
    const preyvr::ui::Surface surface{{preyvr::Pose{{},{0,0,-.65f}},.6f,.4f},0};
    const auto step=[&](float aimZ){
        f.hands[1].aimPose.position={0,0,aimZ};++f.sequence;f.publishedNs=preyvr::MonotonicNanoseconds();
        auto visual=PublishUiPointer(&surface,1000,600,7,200);DrainUiPointer();return visual;
    };
    auto hover=step(-.50f);Require(hover.touch&&!hover.pressed,"front hover claims touch instead of trigger ray");
    auto press=step(-.576f);Require(press.touch&&press.pressed&&gNativeDown,"crossing posts native press without trigger");
    Require(gLastX==500&&gLastY==500,"touch keeps cropped native movie pixel mapping");
    events.clear();auto release=step(-.54f);
    Require(release.touch&&!release.pressed&&!gNativeDown,"withdrawal posts native release");
    Require(events.size()==1&&events[0].kind==2,"normal poke release does not cancel placement");
    step(-.576f);Require(gNativeDown,"fresh second crossing presses");
    ++menuEpoch;events.clear();DrainUiPointer();
    Require(!gNativeDown&&events.size()==2&&events[0].kind==-3,"new modal contents cancel old capture");
    hackingOpen=true;auto hidden=step(-.54f);
    Require(!hidden.active,"hacking refuses mouse pointer mode changes");
    Reset();SetUiPokeEnabled(1);trackingRead=true;
    f.epoch=1;f.hands[1].aimValidity={true,true,true,true,0};step(-.50f);step(-.576f);
    f.hands[1].triggerPressed=true;step(-.54f);step(-.40f);
    auto ray=step(-.40f);Require(ray.active&&!ray.touch&&!ray.pressed,"held trigger cannot click after poke handoff");
    f.hands[1].triggerPressed=false;step(-.40f);f.hands[1].triggerPressed=true;ray=step(-.40f);
    Require(ray.pressed,"neutral rearms distant trigger ray");
}
}
int main(){TestContentionAndExpiry();TestClearAndReference();TestExitAndNormalRelease();TestFault();TestPokeProducer();std::cout<<"ui_pointer_dispatch passed\n";}
