// Actual native-input adapter; the engine call boundary is observable/stubbed.
#include "../src/dll/HackingLane.cpp"
#include <iostream>
#include <vector>
#include <stdexcept>
namespace {
using namespace preyvr;using namespace preyvr::dll;
struct Event {int key;unsigned state;int value;};
std::vector<Event> events;
TrackingFrame frame{};
bool tracking=true,hackingVisible=true,optionsOwned=false,failZeroX=false;
bool gameplayReleased=true;
bool closeOnFirstFacePress=false;
std::uint64_t refGen=2,menuGen=1;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Step(){
 // The production source publishes once per XR frame. This fixture can run
 // several steps within one QPC tick; wait for a new real timestamp rather
 // than accidentally modelling a corrupt new-sequence/equal-stamp frame.
 auto stamp=MonotonicNanoseconds();
 while(stamp<=frame.publishedNs)stamp=MonotonicNanoseconds();
 ++frame.sequence;frame.publishedNs=stamp;UpdateHackingInput();
}
void Reset(){
 inputState={};enabled=0;active=false;generation=1;cleanupOwned=false;
 boundGeneration=boundMenu=inGeneration=0;axes=presses=releases=refused=0;xMilli=yMilli=0;
 events.clear();tracking=hackingVisible=gameplayReleased=true;optionsOwned=failZeroX=false;refGen=2;menuGen=1;
 closeOnFirstFacePress=false;
 frame={};frame.epoch=1;frame.headValidity={true,true,true,true,0};
}
void Ready(){SetHackingControls(1);SetHackingInputActive(true);Step();Step();events.clear();}
unsigned Count(int key,unsigned state){unsigned n=0;for(const auto& e:events)if(e.key==key&&e.state==state)++n;return n;}
}
namespace preyvr::dll {
unsigned long long HeadTrackingReferenceGeneration(){return refGen;}
unsigned long long HudMenuEpoch(){return menuGen;}
bool HudHackingIsOpen(){return hackingVisible;}
DWORD HudRefreshMenuOpenState(){return 0;}
bool GameplayInputReleasedForHacking(){return gameplayReleased;}
bool VrOptionsInputOwned(){return optionsOwned;}
bool TryGetTrackingFrame(TrackingFrame& f){f=frame;return tracking;}
DWORD PostRawInputImmediate(int key,unsigned state,int value){
 Check(input::KeyNameFor(key)!=nullptr,"only proven native pad keys");
 events.push_back({key,state,value});
 if(failZeroX&&key==input::kThumbLX&&value==0)return ERROR_BUSY;
 if(closeOnFirstFacePress&&state==input::kStatePressed&&
    (key==input::kButtonA||key==input::kButtonB||key==input::kButtonX||key==input::kButtonY)) {
  closeOnFirstFacePress=false;hackingVisible=false;++menuGen;
 }
 return 0;
}
}
int main(){try{
 using namespace preyvr;using namespace preyvr::dll;
 Reset();Step();Check(events.empty(),"disabled adapter posts nothing");Ready();
 frame.hands[0].thumbstickX=.65f;Step();
 Check(xMilli>0&&xMilli<650&&HackingInputOwnsControls(),"left stick preserves shaped analog travel");
 frame.hands[0].menuAccept=true;Step();Step();
 Check(Count(input::kButtonX,input::kStatePressed)==1,"left X sends one native press");
 frame.hands[0].menuAccept=false;Step();
 Check(Count(input::kButtonX,input::kStateReleased)==1,"left X native release does not become another press");
 frame.hands[1].menuAccept=true;Step();
 Check(Count(input::kButtonA,input::kStatePressed)==1,"right A uses its own native key");
 events.clear();hackingVisible=false;++menuGen;failZeroX=true;Step();
 Check(HackingInputOwnsControls()&&cleanupOwned,"failed cleanup keeps gameplay handoff interlocked");
 Check(Count(input::kButtonA,input::kStateReleased)==1,"button cleanup independent of failed axis cleanup");
 events.clear();failZeroX=false;Step();
 Check(!HackingInputOwnsControls()&&!cleanupOwned&&xMilli==0,"successful cleanup releases gameplay handoff");
 Check(Count(input::kButtonA,input::kStateReleased)==0,"acknowledged release never repeats");

 Reset();Ready();frame.hands[0].thumbstickY=.7f;frame.hands[1].menuCancel=true;Step();
 ++frame.epoch;refGen+=2;events.clear();Step();
 Check(yMilli==0&&Count(input::kButtonB,input::kStateReleased)==1,"reference/epoch change cleans old input");
 events.clear();Step();Check(Count(input::kButtonB,input::kStatePressed)==0,"held re-entry cannot answer a puzzle");
 frame.hands[0].thumbstickY=0;frame.hands[1].menuCancel=false;Step();
 frame.hands[1].menuCancel=true;Step();Check(Count(input::kButtonB,input::kStatePressed)==1,"neutral rearms native B");
 optionsOwned=true;events.clear();Step();Check(Count(input::kButtonB,input::kStateReleased)==1,"VR options take input ownership");
 optionsOwned=false;frame.hands[1].menuCancel=false;Step();Step();
 frame.hands[0].thumbstickX=.8f;Step();SetHackingInputActive(false);events.clear();Step();
 Check(xMilli==0&&!cleanupOwned,"VR shutdown releases held axis");

 Reset();Ready();frame.hands[0].thumbstickX=.6f;Step();tracking=false;events.clear();Step();
 Check(xMilli==0&&!cleanupOwned,"tracking loss releases on the native drain thread");
 Reset();SetHackingControls(1);SetHackingInputActive(true);gameplayReleased=false;
 frame.hands[0].thumbstickX=.8f;frame.hands[1].menuAccept=true;Step();Step();
 Check(events.empty(),"refused gameplay cleanup blocks hacking acquisition");
 gameplayReleased=true;Step();
 Check(xMilli==0&&Count(input::kButtonA,input::kStatePressed)==0,"handoff still requires neutral controls");
 frame.hands[0].thumbstickX=0;frame.hands[1].menuAccept=false;Step();Step();
 frame.hands[0].thumbstickX=.8f;frame.hands[1].menuAccept=true;Step();
 if(!(xMilli>0&&Count(input::kButtonA,input::kStatePressed)==1)) {
  std::cerr<<"handoff diagnostics "<<HackingInputReport()<<" seq="<<frame.sequence
      <<" stamp="<<frame.publishedNs<<" now="<<MonotonicNanoseconds()<<'\n';
  for(const auto& e:events)std::cerr<<" event key="<<e.key<<" state="<<e.state<<" value="<<e.value<<'\n';
 }
 Check(xMilli>0&&Count(input::kButtonA,input::kStatePressed)==1,"acknowledged handoff and neutral permit hacking");
 Reset();Ready();events.clear();closeOnFirstFacePress=true;
 frame.hands[1].menuAccept=true;frame.hands[1].menuCancel=true;Step();
 Check(Count(input::kButtonA,input::kStatePressed)==1&&Count(input::kButtonB,input::kStatePressed)==0,
       "modal closed by first face press refuses a simultaneous second press");
 Check(cleanupOwned,"delivered closing press retains cleanup ownership");
 Step();Check(Count(input::kButtonA,input::kStateReleased)==1&&!cleanupOwned,
       "closing press is released once after modal exit");
 std::cout<<"hacking_runtime ok: native bindings, analog/button state, cleanup, ownership and lifecycle\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
