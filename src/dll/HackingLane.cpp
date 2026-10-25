#include "HackingLane.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "MoveLane.h"
#include "VrOptionsRuntime.h"
#include "XrInput.h"
#include "preyvr/HackingInput.h"
#include "preyvr/InputEvent.h"
#include "preyvr/LatestSnapshot.h"
#include <atomic>
#include <cmath>
#include <sstream>

namespace preyvr::dll {
namespace {
std::atomic<unsigned> enabled{0};
std::atomic<bool> active{false};
std::atomic<std::uint64_t> generation{1};
std::atomic<unsigned long long> axes{0},presses{0},releases{0},refused{0};
std::atomic<int> xMilli{0},yMilli{0};
std::atomic<bool> cleanupOwned{false};
std::atomic<bool> ready{false},handoffBlocked{false};
hacking::InputState inputState; // engine input-drain thread only
std::uint64_t boundGeneration=0,boundMenu=0;
std::uint64_t inGeneration=0;
}
DWORD SetHackingControls(unsigned value) {
    if(value>1)return ERROR_INVALID_PARAMETER;
    if(enabled.exchange(value)!=value)InvalidateHackingInput();return 0;
}
unsigned HackingControlsEnabled(){return enabled.load();}
bool HackingInputOwnsControls(){return cleanupOwned.load()||(active.load()&&enabled.load()&&HudHackingIsOpen());}
void InvalidateHackingInput(){generation.fetch_add(1);}
void SetHackingInputActive(bool value){if(active.exchange(value)!=value)InvalidateHackingInput();}
void UpdateHackingInput() {
    // Exact installed defaultprofile bindings, verified from base and patch:
    // hack_moveprompt_x/y <- xi_thumblx/y; finals <- xi_x/y/a/b (onPress).
    // Go through the existing engine action map. Calling ArkHackingUI's final
    // handler directly on release would bypass its onPress filter.
    TrackingFrame frame{};
    hacking::Input in{};in.nowNs=MonotonicNanoseconds();
    const auto reference=HeadTrackingReferenceGeneration(),menu=HudMenuEpoch(),settings=generation.load();
    const bool read=TryGetTrackingFrame(frame);
    in.valid=read&&FreshSample(in.nowNs,frame.publishedNs)&&
        IsPoseUsable(frame.head,frame.headValidity,200000000ull)&&!(reference&1);
    const bool visible=HudHackingIsOpen();
    const bool currentModal=!visible||HudRefreshMenuOpenState()==0;
    const bool gameplayReleased=GameplayInputReleasedForHacking();
    handoffBlocked.store(!gameplayReleased);
    in.active=active.load()&&enabled.load()&&currentModal&&HudHackingIsOpen()&&!VrOptionsInputOwned()&&gameplayReleased;
    in.stampNs=frame.publishedNs;in.sequence=frame.sequence;in.epoch=frame.epoch;in.reference=reference;
    // A serial identifies the whole menu/settings context; do not hash IDs.
    if(settings!=boundGeneration||menu!=boundMenu) {
        boundGeneration=settings;boundMenu=menu;++inGeneration;
    }
    in.generation=inGeneration;
    in.stickX=frame.hands[0].thumbstickX;in.stickY=frame.hands[0].thumbstickY;
    in.a=frame.hands[1].menuAccept;in.b=frame.hands[1].menuCancel;
    in.x=frame.hands[0].menuAccept;in.y=frame.hands[0].menuCancel;
    auto out=inputState.Update(in);
    const auto owns=[&]{
        TrackingFrame latest{};
        return active.load()&&enabled.load()&&settings==generation.load()&&
            menu==HudMenuEpoch()&&reference==HeadTrackingReferenceGeneration()&&HudHackingIsOpen()&&!VrOptionsInputOwned()&&
            TryGetTrackingFrame(latest)&&latest.epoch==frame.epoch&&
            FreshSample(MonotonicNanoseconds(),latest.publishedNs)&&
            IsPoseUsable(latest.head,latest.headValidity,200000000ull);
    };
    const auto post=[&](int key,unsigned state,int value){
        if(PostRawInputImmediate(key,state,value)==0)return true;
        refused.fetch_add(1);return false;
    };
    for(unsigned axis=0;axis<2;++axis) {
        const auto bit=static_cast<std::uint8_t>(1u<<axis);
        if(!(out.axisMask&bit))continue;
        const int value=static_cast<int>(std::lround((axis?out.y:out.x)*1000.f));
        if((value==0||owns())&&post(axis?input::kThumbLY:input::kThumbLX,input::kStateChanged,value)) {
            axes.fetch_add(1);(axis?yMilli:xMilli).store(value);
        } else out.axisMask=static_cast<std::uint8_t>(out.axisMask&~bit);
    }
    constexpr int keys[]={input::kButtonA,input::kButtonB,input::kButtonX,input::kButtonY};
    for(unsigned button=0;button<4;++button) {
        const auto bit=static_cast<std::uint8_t>(1u<<button);
        if(out.releaseMask&bit) {
            if(post(keys[button],input::kStateReleased,0))releases.fetch_add(1);
            else out.releaseMask=static_cast<std::uint8_t>(out.releaseMask&~bit);
        }
        if(out.pressMask&bit) {
            // Recheck volatile ownership before every new press. Cleanup releases
            // remain legal after the modal disappears, just like a real pad.
            if(in.active&&in.valid&&HudRefreshMenuOpenState()==0&&owns()&&
               post(keys[button],input::kStatePressed,1000))presses.fetch_add(1);
            else out.pressMask=static_cast<std::uint8_t>(out.pressMask&~bit);
        }
    }
    inputState.Commit(out);
    ready.store(out.ready);
    cleanupOwned.store(inputState.OwnsControls());
}
std::string HackingInputReport(){std::ostringstream s;s<<"enabled="<<enabled.load()<<" active="<<active.load()
    <<" ready="<<ready.load()<<" cleanup="<<cleanupOwned.load()<<" handoffBlocked="<<handoffBlocked.load()
    <<" visible="<<HudHackingIsOpen()<<" axes="<<axes.load()<<" presses="<<presses.load()
    <<" releases="<<releases.load()<<" refused="<<refused.load()<<" stick="<<xMilli.load()<<','<<yMilli.load();return s.str();}
}
