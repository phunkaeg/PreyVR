// Production main-thread dispatcher, with observable native consumers.
#include "../src/dll/UiPointer.cpp"
#include <cstdlib>
#include <iostream>
#include <vector>
namespace {
struct Event {int kind,x,y;};
std::vector<Event> events;
bool menuKnown=true,menuOpen=true;
DWORD modeError=0,cursorError=0,cancelError=0;
void Require(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
}
namespace preyvr::lifecycle {void Log(std::string_view){}}
namespace preyvr::dll {
bool HudMenuStateKnown(){return menuKnown;}
bool HudMenuIsOpen(){return menuOpen;}
bool TryGetTrackingFrame(TrackingFrame&){return false;}
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
    menuKnown=menuOpen=true;modeError=cursorError=cancelError=0;events.clear();
}
Sample Neutral(){return {true,true,false,330,555,now,4,1,0};}
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
}
void TestFault(){
    Reset();auto s=Press();s.x=350;modeError=ERROR_INVALID_ADDRESS;DrainPointerSample(&s,now);
    Require(gFault&&!gNativeDown,"fault cancels and releases outstanding press");
    Require(events.size()==3&&events[0].kind==-2&&events[1].kind==-3&&events[2].kind==2,"fault cleanup order");
    events.clear();DrainPointerSample(&s,now);Require(events.empty(),"fault does not retry native calls");
}
}
int main(){TestContentionAndExpiry();TestClearAndReference();TestExitAndNormalRelease();TestFault();std::cout<<"ui_pointer_dispatch passed\n";}
