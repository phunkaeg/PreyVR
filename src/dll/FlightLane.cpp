#include "FlightLane.h"
#include "FlightNative.h"
#include "SurfaceGrabLane.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "HackingLane.h"
#include "InputPost.h"
#include "MoveLane.h"
#include "VrOptionsRuntime.h"
#include "preyvr/InputEvent.h"
#include "preyvr/LatestSnapshot.h"
#include <atomic>
#include <cmath>
#include <sstream>

namespace preyvr::dll {
namespace {
std::atomic<unsigned> flightEnabled{0},movementMode{0};
std::atomic<bool> flightActive{false},facesOwned{false};
std::atomic<bool> flightCleanup{false};
std::atomic<std::uint64_t> flightGeneration{1};
std::atomic<DWORD> flightError{0};
std::atomic<DWORD> flightOwnerError{ERROR_NOT_READY};
std::atomic<bool> flightPoseValid{false},flightReady{false};
std::atomic<int> nativeState{0};
std::atomic<unsigned long long> flightPresses{0},flightReleases{0},flightRefused{0},rotationReads{0};
struct Prepared {
    TrackingFrame tracking{};
    FlightOwner owner{};
    Quaternion direction{};
    std::uint64_t reference=0,generation=0,menu=0;
    bool valid=false,ready=false,steer=false;
};
LatestSnapshot<Prepared> rotationSample;
Prepared prepared; // input-drain thread only
flight::Controls flightControls;
flight::Output flightOutput{};
std::uint64_t contextSerial=0,boundSettings=0,boundMenu=0;
std::uintptr_t boostOwner=0,brakeOwner=0;
// A native setter may fault after changing its flag. Cleanup debt is separate
// from acknowledged edges; it blocks new acquisition until cleared.
std::uint8_t setterDebt=0;
bool flightQuarantine=false;
bool boundHeadRelative=false;
bool boundSurface=false;
bool Current(const Prepared& p) {
    TrackingFrame latest{};
    return flightEnabled.load()&&flightActive.load()&&p.valid&&
        p.generation==flightGeneration.load()&&p.reference==HeadTrackingReferenceGeneration()&&
        p.menu==HudMenuEpoch()&&HudGameplayInputAllowed()&&!VrOptionsInputOwned()&&!HackingInputOwnsControls()&&
        p.steer==HeadRelativeMovementEnabled()&&TryGetTrackingFrame(latest)&&latest.epoch==p.tracking.epoch&&
        FreshSample(MonotonicNanoseconds(),latest.publishedNs,100000000ull)&&
        IsPoseUsable(latest.head,latest.headValidity,100000000ull)&&
        FreshSample(MonotonicNanoseconds(),p.tracking.publishedNs,100000000ull);
}
DWORD Deliver(std::uint8_t bit,bool pressed,std::uintptr_t player) {
    if(bit==flight::Boost)return SetNativeFlightBoost(player,pressed);
    if(bit==flight::Brake)return SetNativeFlightBrake(player,pressed);
    return PostRawInputImmediate(bit==flight::Up?input::kButtonA:input::kButtonB,
        pressed?input::kStatePressed:input::kStateReleased,pressed?1000:0);
}
void Note(DWORD result) {flightError=result;if(result)flightRefused.fetch_add(1);}
}
DWORD SetNativeFlightControls(unsigned value) {
    if(value>1)return ERROR_INVALID_PARAMETER;
    if(value){const auto error=EnsureNativeFlightHook();flightError=error;if(error)return error;}
    if(flightEnabled.exchange(value)!=value)flightGeneration.fetch_add(1);return 0;
}
unsigned NativeFlightControlsEnabled(){return flightEnabled.load();}
bool NativeFlightSessionActive(){return flightActive.load();}
std::uint64_t NativeFlightContextGeneration(){return flightGeneration.load();}
int NativeFlightObservedState(){return nativeState.load();}
DWORD NativeFlightObservedOwnerError(){return flightOwnerError.load();}
void SetNativeFlightActive(bool value){if(flightActive.exchange(value)!=value)flightGeneration.fetch_add(1);}
bool NativeFlightOwnsFaces(){return facesOwned.load();}
bool NativeFlightCleanupPending(){return flightCleanup.load();}
unsigned NativeFlightMovementMode(){return movementMode.load();}
bool ReadNativeFlightTracking(TrackingFrame& out){out=prepared.tracking;return Current(prepared)&&prepared.ready;}
void PrepareNativeFlightInput() {
    const auto settings=flightGeneration.load(),menu=HudMenuEpoch();
    const bool headRelative=HeadRelativeMovementEnabled();
    if(settings!=boundSettings||menu!=boundMenu||headRelative!=boundHeadRelative){
        boundSettings=settings;boundMenu=menu;boundHeadRelative=headRelative;++contextSerial;
    }
    prepared={};prepared.generation=settings;prepared.menu=menu;
    const bool request=flightActive.load()&&flightEnabled.load();
    const auto readOwner=(request||flightControls.Held()||setterDebt)?ReadNativeFlightOwner(prepared.owner):ERROR_NOT_READY;
    flightOwnerError=readOwner;
    if(!readOwner)nativeState=prepared.owner.state;
    const bool inFlight=request&&!readOwner&&prepared.owner.state==9;
    const bool claim=request&&(readOwner||prepared.owner.state==9);
    TrackingFrame f{};GameplayPoseFrame pose{};
    const auto reference=HeadTrackingReferenceGeneration();prepared.reference=reference;
    const bool read=TryGetTrackingFrame(f);prepared.tracking=f;
    const bool neutral=read&&!f.hands[1].menuAccept&&!f.hands[1].menuCancel&&!f.hands[0].menuCancel&&
        std::hypot(f.hands[0].thumbstickX,f.hands[0].thumbstickY)<=.15f;
    if(neutral&&!setterDebt&&!flightControls.Held())flightQuarantine=false;
    const bool recenter=read&&f.hands[0].gripPressed&&f.hands[1].gripPressed&&f.hands[0].menuCancel;
    const bool poseGood=inFlight&&read&&TryGetGameplayPoseFrame(pose)&&pose.player==prepared.owner.player&&
        pose.referenceGeneration==reference&&pose.tracking.epoch==f.epoch&&!(reference&1)&&
        FreshSample(MonotonicNanoseconds(),pose.publishedNs,100000000ull)&&
        FreshSample(MonotonicNanoseconds(),f.publishedNs,100000000ull)&&
        IsPoseUsable(f.head,f.headValidity,100000000ull);
    const auto direction=poseGood?flight::Steering(pose.yaw,f.head,f.headValidity):std::nullopt;
    prepared.valid=direction.has_value();if(direction)prepared.direction=*direction;
    prepared.steer=headRelative;
    const bool baseAllowed=prepared.valid&&Current(prepared)&&!prepared.owner.cinematic&&!recenter;
    PrepareSurfaceGrab(prepared.owner,pose,f,baseAllowed);
    const bool surface=SurfaceGrabOwnsMotion();
    if(surface!=boundSurface){boundSurface=surface;++contextSerial;}
    const bool allowed=baseAllowed&&!surface&&!setterDebt&&!flightQuarantine&&
        MoveLaneCinematicGate()==0&&GameplayFacesReleasedForFlight();
    flight::Input in{};in.valid=prepared.valid;in.active=allowed;
    in.up=f.hands[1].menuAccept;in.down=f.hands[1].menuCancel;in.boost=f.hands[0].menuCancel;
    in.x=f.hands[0].thumbstickX;in.y=f.hands[0].thumbstickY;
    in.now=MonotonicNanoseconds();in.stamp=f.publishedNs;in.sequence=f.sequence;
    in.player=prepared.owner.player;in.epoch=f.epoch;in.reference=reference;in.generation=contextSerial;
    flightOutput=flightControls.Update(in);prepared.ready=flightOutput.ready;
    flightPoseValid=prepared.valid;flightReady=prepared.ready;
    flightCleanup=flightControls.Held()||setterDebt;
    facesOwned=claim||flightCleanup.load();
    movementMode=(claim||flightCleanup.load())?(allowed&&flightOutput.move?2u:1u):0u;
    rotationSample.Publish(prepared);
}
void DrainNativeFlightInput() {
    auto out=flightOutput;
    constexpr std::uint8_t bits[]={flight::Up,flight::Down,flight::Boost,flight::Brake};
    for(const auto bit:{flight::Boost,flight::Brake})if(setterDebt&bit) {
        const auto error=Deliver(bit,false,bit==flight::Boost?boostOwner:brakeOwner);Note(error);
        if(!error)setterDebt=static_cast<std::uint8_t>(setterDebt&~bit);
    }
    for(const auto bit:bits)if(out.release&bit) {
        const auto player=bit==flight::Boost?boostOwner:bit==flight::Brake?brakeOwner:prepared.owner.player;
        const auto error=Deliver(bit,false,player);Note(error);
        if(error)out.release=static_cast<std::uint8_t>(out.release&~bit);else ++flightReleases;
    }
    // Do not replace a refused release with a new owner/direction.
    const bool clean=out.release==flightOutput.release&&!setterDebt;
    for(const auto bit:bits)if(out.press&bit) {
        FlightOwner owner{};
        const bool owns=clean&&Current(prepared)&&GameplayFacesReleasedForFlight()&&
            !ReadNativeFlightOwner(owner)&&owner.player==prepared.owner.player&&owner.entity==prepared.owner.entity&&owner.state==9&&!owner.cinematic;
        const auto error=owns?Deliver(bit,true,owner.player):ERROR_NOT_READY;Note(error);
        if(error) {
            out.press=static_cast<std::uint8_t>(out.press&~bit);
            if(owns&&(bit==flight::Boost||bit==flight::Brake)) {
                (bit==flight::Boost?boostOwner:brakeOwner)=owner.player;
                setterDebt=static_cast<std::uint8_t>(setterDebt|bit);flightQuarantine=true;
                const auto rollback=Deliver(bit,false,owner.player);Note(rollback);
                if(!rollback)setterDebt=static_cast<std::uint8_t>(setterDebt&~bit);
            }
        }else {
            if(bit==flight::Boost)boostOwner=owner.player;if(bit==flight::Brake)brakeOwner=owner.player;
            ++flightPresses;
        }
    }
    flightControls.Commit(out);
    flightCleanup=flightControls.Held()||setterDebt;
    if(setterDebt){movementMode=1;prepared.ready=false;flightReady=false;rotationSample.Publish(prepared);}
    facesOwned=NativeFlightMovementMode()!=0||flightControls.Held()||setterDebt;
}
bool ApplyNativeFlightRotation(std::uintptr_t entity,std::uintptr_t caller,flight::NativeQuat& out) {
    if(caller!=0x1270651||!flightActive.load()||!flightEnabled.load())return false;
    Prepared p{};FlightOwner owner{};
    TrackingFrame latest{};
    if(!rotationSample.TryRead(p)||!p.ready||!p.steer||!Current(p)||p.owner.entity!=entity||
       ReadNativeFlightOwner(owner)||owner.player!=p.owner.player||owner.entity!=entity||owner.state!=9||owner.cinematic||
       !TryGetTrackingFrame(latest)||latest.epoch!=p.tracking.epoch||
       !FreshSample(MonotonicNanoseconds(),latest.publishedNs,100000000ull)||
       !IsPoseUsable(latest.head,latest.headValidity,100000000ull))return false;
    if(!flight::OverrideRotation(out,p.direction))return false;
    ++rotationReads;return true;
}
std::string NativeFlightReport(){std::ostringstream s;s<<"enabled="<<flightEnabled.load()<<" active="<<flightActive.load()
    <<" state="<<nativeState.load()<<" mode="<<movementMode.load()<<" faces="<<facesOwned.load()<<" cleanup="<<flightCleanup.load()
    <<" ownerError="<<flightOwnerError.load()<<" pose="<<flightPoseValid.load()<<" ready="<<flightReady.load()
    <<" error="<<flightError.load()<<" presses="<<flightPresses.load()<<" releases="<<flightReleases.load()
    <<" refused="<<flightRefused.load()<<" steering="<<rotationReads.load();return s.str();}
}
