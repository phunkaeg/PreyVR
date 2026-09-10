#include "UiPointer.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "InventoryPointerCapture.h"
#include "preyvr/InputEvent.h"
#include "XrInput.h"
#include "Logger.h"
#include "preyvr/LatestSnapshot.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <sstream>
namespace preyvr::dll {
namespace {
struct Sample {
    bool active=false,inside=false,pressed=false;
    int x=-100,y=-100;
    std::uint64_t stamp=0,epoch=0,reference=0,generation=0;
};
LatestSnapshot<Sample> gSample;
std::atomic<std::uint64_t> gClearGeneration{0};
Sample gRetainedSample; // main-thread fallback only while the last sample is fresh
std::atomic<unsigned> gHand{1};
std::atomic<bool> gFault{false};
std::atomic<unsigned long long> gMoves{0},gPresses{0},gReleases{0},gRefused{0};
std::atomic<int> gX{-100},gY{-100};
ui::PointerButtons gButtons; // main thread only
std::uint64_t gEpoch=0,gReference=0;
int gLastX=-100,gLastY=-100;
bool gWasActive=false;
bool gNativeDown=false;
bool Refuse(DWORD result,const char* stage) {
    // A failed move or mode switch must not strand a previous mouse press.
    // The guarded release is attempted once, then this lane stays disarmed.
    DWORD release=0,cancel=0;
    if(gNativeDown){
        bool cancelled=false;
        if(std::string_view(stage)!="inventory_cancel")cancel=CancelInventoryPointerCapture(&cancelled);
        release=HudDispatchPointer(2,gLastX,gLastY);gNativeDown=false;
    }
    gRefused.fetch_add(1);gFault.store(true);
    lifecycle::Log("preyvr_pointer refused="+std::to_string(result)+" stage="+stage+
        " cancel="+std::to_string(cancel)+" release="+std::to_string(release)+" native_lane_disarmed=1");
    return false;
}
bool Send(int event,int x,int y) {
    // HardwareMouse delivers Flash coordinates, but the independent input
    // listener 0x182D3A0 selects keyboard/mouse versus gamepad UI behaviour.
    // A zero mouse axis selects that native mode without moving the camera or
    // introducing a held gameplay button when a click closes the menu.
    if(event==0&&HudMenuStateKnown()&&HudMenuIsOpen()) {
        const auto modeResult=PostRawInputImmediate(input::kMouseX,input::kStateChanged,0);
        if(modeResult)return Refuse(modeResult,"input_mode");
    }
    const auto result=HudDispatchPointer(event,x,y);
    if(result) {
        return Refuse(result,"cursor_event");
    }
    if(event==0)gMoves.fetch_add(1);
    if(event==1){gPresses.fetch_add(1);gNativeDown=true;}
    if(event==2){gReleases.fetch_add(1);gNativeDown=false;}
    if(event!=0)lifecycle::Log("preyvr_pointer event="+std::to_string(event)+" x="+std::to_string(x)+" y="+std::to_string(y));
    return true;
}
}
unsigned UiPointerHand(){return gFault.load()?2:gHand.load();}
DWORD SetUiPointerHand(unsigned hand){if(hand>2)return ERROR_INVALID_PARAMETER;gHand.store(hand);ClearUiPointer();return 0;}
void ClearUiPointer(){gClearGeneration.fetch_add(1);gSample.Clear();}
UiPointerVisual PublishUiPointer(const ui::Surface* surface,unsigned width,unsigned height,unsigned long long reference) {
    UiPointerVisual visual;
    Sample sample;sample.stamp=MonotonicNanoseconds();sample.reference=reference;sample.generation=gClearGeneration.load();
    TrackingFrame frame;
    const auto hand=UiPointerHand();
    if(surface&&hand<2&&width>0&&width<=8192&&height>0&&height<=8192&&TryGetTrackingFrame(frame)) {
        const auto& state=frame.hands[hand];
        if(IsPoseUsable(state.aimPose,state.aimValidity,200000000)) {
            sample.active=true;sample.pressed=state.triggerPressed;sample.epoch=frame.epoch*4+hand;
            visual.active=true;
            visual.aim=state.aimPose;visual.pressed=sample.pressed;
            visual.hit=ui::Intersect(*surface,state.aimPose);
            if(visual.hit) {
                sample.inside=visual.hit->inside;
                // Off-screen drag coordinates remain off-screen. Bound integer conversion.
                sample.x=static_cast<int>(std::clamp(visual.hit->u,-2.f,3.f)*static_cast<float>(width));
                sample.y=static_cast<int>(std::clamp(visual.hit->v,-2.f,3.f)*static_cast<float>(height));
            }
        }
    }
    gSample.Publish(sample);
    return visual;
}
namespace {
bool CancelInventory(bool& cancelled) {
    bool one=false;
    const auto result=CancelInventoryPointerCapture(&one);
    cancelled=cancelled||one;
    return result?Refuse(result,"inventory_cancel"):true;
}
void DrainPointerSample(const Sample* incoming,std::uint64_t now) {
    if(incoming)gRetainedSample=*incoming;
    // A try-lock miss is not a tracking-loss publication. Keep a still-fresh
    // sample through contention, while explicit clears invalidate it immediately.
    const auto s=gRetainedSample;
    bool active=s.generation==gClearGeneration.load()&&FreshSample(now,s.stamp)&&s.active&&
        UiPointerHand()<2&&HudMenuStateKnown()&&HudMenuIsOpen();
    if(gFault.load())return;
    bool cancelled=false;
    if(active&&(gEpoch!=s.epoch||gReference!=s.reference)) {
        if(gButtons.Update(false,false,false)==-1) {
            if(!CancelInventory(cancelled)||!Send(2,gLastX,gLastY))return;
            gWasActive=false;
            gEpoch=s.epoch;gReference=s.reference;
            return; // allow queued release to advance before changing hover
        }
        gWasActive=false;
    }
    gEpoch=s.epoch;gReference=s.reference;
    int edge=gButtons.Update(active,s.inside,s.pressed);
    const int x=active?s.x:-100,y=active?s.y:-100;
    const bool outsideCapture=gNativeDown&&(!active||!s.inside);
    if(outsideCapture) {
        if(!CancelInventory(cancelled))return;
        gButtons.Update(false,false,false);
        // Never feed an off-panel coordinate while the native button is down:
        // OnDrag clamps it to inventory cell (1,1). If Pick is still queued,
        // release at the last on-panel point rather than manufacturing a move.
        if(!Send(2,gLastX,gLastY))return;
        return; // clear hover on the next drain, after the native release
    }
    // Clear native hover when leaving the surface, and send position before a click.
    if((active||gWasActive)&&(x!=gLastX||y!=gLastY||edge==1||!gWasActive)) {
        if(!Send(0,x,y))return;
        gLastX=x;gLastY=y;gX.store(x);gY.store(y);
    }
    if(edge&&!Send(edge>0?1:2,x,y))return;
    gWasActive=active;
}
}
void DrainUiPointer() {
    Sample s;
    const bool read=gSample.TryRead(s);
    DrainPointerSample(read?&s:nullptr,MonotonicNanoseconds());
}
std::string UiPointerReport(){std::ostringstream s;s<<"hand="<<UiPointerHand()<<" moves="<<gMoves.load()
    <<" presses="<<gPresses.load()<<" releases="<<gReleases.load()<<" refused="<<gRefused.load()
    <<" pixel="<<gX.load()<<','<<gY.load();return s.str();}
}
