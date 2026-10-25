#include "SurfaceGrabLane.h"
#include "SurfaceGrabNative.h"
#include "FlightLane.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "HackingLane.h"
#include "VrOptionsRuntime.h"
#include "SceneQuery.h"
#include "InteractionLane.h"
#include "PsiMedkit.h"
#include "Psychoscope.h"
#include "Haptics.h"
#include "MoveLane.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/SurfaceGrabNative.h"
#include <atomic>
#include <cmath>
#include <sstream>
namespace preyvr::dll {
namespace {
namespace sg=surfacegrab;
std::atomic<unsigned> enabled{0},reach{10},strength{100},limit{150};
std::atomic<std::uint64_t> settings{1},currentSerial{0},reserved{0},deliveredRelease{0};
std::atomic<bool> owns{false},fault{false};
std::atomic<DWORD> lastError{0};
std::atomic<DWORD> ownerError{ERROR_NOT_READY};
std::atomic<unsigned long long> nativeReads{0};
std::atomic<unsigned long long> queries{0},rejectedCollider{0},captures{0},pushes{0},cancelled{0},submitted{0},refused{0};
struct Command {
    Vec3 velocity{};
    std::uintptr_t player=0,entity=0,animated=0;
    std::uint64_t serial=0,stamp=0,basisStamp=0,epoch=0,reference=0,weapon=0,settings=0,flight=0,menu=0;
    bool release=false;
};
LatestSnapshot<Command> command;
sg::Grab grab; // input-drain thread only
Command pending{};
std::uint64_t nextSerial=0,context=0,boundSettings=0,boundFlight=0,boundMenu=0;
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Scale(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Length(Vec3 a){return std::sqrt(Dot(a,a));}
void Clear(){command.Clear();currentSerial=0;owns=false;pending={};}
bool Current(const Command& c){
    TrackingFrame latest{};GameplayPoseFrame pose{};
    return c.serial&&c.serial==currentSerial.load()&&enabled.load()&&!fault.load()&&
        NativeFlightControlsEnabled()&&NativeFlightSessionActive()&&c.flight==NativeFlightContextGeneration()&&
        c.settings==settings.load()&&c.menu==HudMenuEpoch()&&HudGameplayInputAllowed()&&!VrOptionsInputOwned()&&!HackingInputOwnsControls()&&
        !UseTargetPresent()&&!UseCarrying()&&!MedkitZoneHasLeftHand()&&!PsychoscopeZoneHasLeftHand()&&!SupportGripNear()&&!TwoHandedAimHeld()&&
        c.reference==HeadTrackingReferenceGeneration()&&!(c.reference&1)&&
        FreshSample(MonotonicNanoseconds(),c.stamp,100000000ull)&&FreshSample(MonotonicNanoseconds(),c.basisStamp,100000000ull)&&
        TryGetTrackingFrame(latest)&&latest.epoch==c.epoch&&FreshSample(MonotonicNanoseconds(),latest.publishedNs,100000000ull)&&
        IsPoseUsable(latest.head,latest.headValidity,100000000ull)&&IsPoseUsable(latest.hands[0].gripPose,latest.hands[0].gripValidity,100000000ull)&&
        latest.hands[0].gripPressed!=c.release&&!(latest.hands[0].gripPressed&&latest.hands[1].gripPressed&&latest.hands[0].menuCancel)&&
        TryGetGameplayPoseFrame(pose)&&pose.player==c.player&&pose.weaponGeneration==c.weapon&&pose.referenceGeneration==c.reference;
}
sg::Contact Cast(const GameplayPoseFrame& pose,Vec3 origin,Vec3 delta){
    scene::Hit hit{};++queries;
    if(!QueryPhysicalSegment(pose,origin,delta,hit)||hit.distance<0)return {};
    const auto collider=reinterpret_cast<std::uintptr_t>(hit.collider);
    if(!IsFixedSurfaceCollider(collider)){++rejectedCollider;return {};}
    sg::Contact out{true,collider,hit.partId,hit.point,hit.normal};
    const float n=Length(out.normal);if(!std::isfinite(n)||n<.98f||n>1.02f)return {};
    out.normal=Scale(out.normal,1/n);
    if(!sg::ValidContact(out)||Dot(out.normal,delta)>=-.001f)return {};
    return out;
}
sg::Contact Capture(const GameplayPoseFrame& pose,const Pose& hand,float distance){
    constexpr Vec3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    sg::Contact nearest{};float best=distance+.001f;
    for(auto dir:directions){
        const auto hit=Cast(pose,hand.position,Scale(Rotate(hand.orientation,dir),distance));
        const float length=Length(Sub(hand.position,hit.point));
        if(sg::ValidContact(hit)&&length<best&&Dot(Sub(hand.position,hit.point),hit.normal)>=-.002f){nearest=hit;best=length;}
    }
    return nearest;
}
}
DWORD SetSurfaceGrabEnabled(unsigned value){
    if(value>1)return ERROR_INVALID_PARAMETER;if(value){if(fault.load())return ERROR_INVALID_FUNCTION;const auto error=EnsureSurfaceGrabHook();lastError=error;if(error)return error;}
    if(enabled.exchange(value)!=value)settings.fetch_add(1);return 0;
}
unsigned SurfaceGrabEnabled(){return enabled.load();}
DWORD SetSurfaceGrabReach(unsigned cm){if(cm<5||cm>18)return ERROR_INVALID_PARAMETER;if(reach.exchange(cm)!=cm)++settings;return 0;}
unsigned SurfaceGrabReach(){return reach.load();}
DWORD SetSurfacePushStrength(unsigned percent){if(percent>150)return ERROR_INVALID_PARAMETER;if(strength.exchange(percent)!=percent)++settings;return 0;}
unsigned SurfacePushStrength(){return strength.load();}
DWORD SetSurfacePushLimit(unsigned cm){if(cm<50||cm>300)return ERROR_INVALID_PARAMETER;if(limit.exchange(cm)!=cm)++settings;return 0;}
unsigned SurfacePushLimit(){return limit.load();}
bool SurfaceGrabOwnsMotion(){return owns.load();}
bool SurfaceGrabOwnsLeftGrip(){return owns.load();}
void PrepareSurfaceGrab(const FlightOwner& flight,const GameplayPoseFrame& pose,const TrackingFrame& tracking,bool allowed){
    const auto setting=settings.load(),flightGeneration=NativeFlightContextGeneration(),menu=HudMenuEpoch();
    if(setting!=boundSettings||flightGeneration!=boundFlight||menu!=boundMenu){boundSettings=setting;boundFlight=flightGeneration;boundMenu=menu;++context;}
    const bool leftBusy=UseTargetPresent()||UseCarrying()||MedkitZoneHasLeftHand()||PsychoscopeZoneHasLeftHand()||SupportGripNear()||TwoHandedAimHeld();
    const bool recenter=tracking.hands[0].gripPressed&&tracking.hands[1].gripPressed&&tracking.hands[0].menuCancel;
    SurfaceOwner owner{};
    const auto read=(enabled.load()&&allowed)?ReadSurfaceGrabOwner(owner):ERROR_NOT_READY;
    ownerError=read;
    const bool valid=enabled.load()&&!fault.load()&&allowed&&NativeFlightControlsEnabled()&&NativeFlightSessionActive()&&!leftBusy&&!recenter&&
        !read&&owner.flight.player==flight.player&&owner.flight.entity==flight.entity&&owner.flight.state==9&&!owner.flight.cinematic&&
        pose.cameraCentreValid&&sg::Finite(pose.cameraCentre)&&std::isfinite(pose.yaw)&&pose.player==flight.player&&pose.referenceGeneration==HeadTrackingReferenceGeneration()&&pose.tracking.epoch==tracking.epoch&&
        FreshSample(MonotonicNanoseconds(),pose.publishedNs,100000000ull)&&FreshSample(MonotonicNanoseconds(),tracking.publishedNs,100000000ull)&&
        IsPoseUsable(tracking.hands[0].gripPose,tracking.hands[0].gripValidity,100000000ull)&&IsPoseUsable(tracking.head,tracking.headValidity,100000000ull);
    if(pending.serial){
        if(deliveredRelease.load()==pending.serial){if(valid&&Current(pending))QueueHaptic(Hand::left,haptics::Event::ObjectThrown,tracking);Clear();grab.Cancel();}
        else if(valid&&Current(pending))return;
        else {++cancelled;pending={};Clear();grab.Cancel();}
    }
    sg::Input input{};input.valid=valid;input.allowed=valid;input.grip=tracking.hands[0].gripPressed;
    input.now=MonotonicNanoseconds();input.stamp=tracking.publishedNs;input.sequence=tracking.sequence;input.player=flight.player;
    input.epoch=tracking.epoch;input.reference=HeadTrackingReferenceGeneration();input.weapon=pose.weaponGeneration;input.generation=context;
    Pose hand{};
    if(valid){
        input.worldFromReference=stereo::YawQuaternion(pose.yaw);
        const Pose worldFromTracking{input.worldFromReference,Sub(pose.cameraCentre,Rotate(input.worldFromReference,stereo::ToEngineSpace(pose.tracking.head.position)))};
        hand=Compose(worldFromTracking,stereo::ToEngineSpace(tracking.hands[0].gripPose));
        input.localHand=stereo::ToEngineSpace(tracking.hands[0].gripPose.position);input.worldHand=hand.position;
        if(grab.Held()){
            const auto a=grab.Anchor();input.contact=Cast(pose,Add(a.point,Scale(a.normal,.025f)),Scale(a.normal,-.05f));
        }else if(grab.WantsContact(input.grip)&&GameplayGripReleasedForSurface())input.contact=Capture(pose,hand,reach.load()*.01f);
    }
    sg::Settings policy{};policy.reach=reach.load()*.01f;policy.pushScale=strength.load()*.01f;policy.maxPush=limit.load()*.01f;
    const auto result=grab.Update(input,policy);
    if(result.acquired){++captures;QueueHaptic(Hand::left,haptics::Event::ObjectGrabbed,tracking);}
    if(result.cancelled)++cancelled;
    if(!result.command){Clear();return;}
    Command c{};c.velocity=result.velocity;c.player=flight.player;c.entity=flight.entity;c.animated=owner.animated;
    c.serial=++nextSerial;if(!c.serial)c.serial=++nextSerial;c.stamp=tracking.publishedNs;c.basisStamp=pose.publishedNs;
    c.epoch=tracking.epoch;c.reference=input.reference;c.weapon=pose.weaponGeneration;c.settings=setting;c.flight=flightGeneration;c.menu=menu;c.release=result.launch;
    if(c.release){pending=c;++pushes;}
    currentSerial=c.serial;command.Publish(c);owns=true;
}
std::uint64_t ClaimSurfaceGrabVelocity(std::uintptr_t animated,std::uintptr_t caller,unsigned type,Vec3 nativeVelocity,Vec3& desired){
    if(caller!=surfacegrab::native::Caller||type!=4||!sg::Finite(nativeVelocity)||Length(nativeVelocity)>100)return 0;
    ++nativeReads;
    Command c{};SurfaceOwner owner{};
    if(!command.TryRead(c)||!Current(c)||c.animated!=animated||NativeFlightCleanupPending()||!GameplayFacesReleasedForFlight()||
       ReadSurfaceGrabOwner(owner)||owner.animated!=animated||owner.flight.player!=c.player||owner.flight.entity!=c.entity||owner.flight.state!=9||owner.flight.cinematic)return 0;
    if(c.release){auto previous=reserved.load();if(previous>=c.serial||!reserved.compare_exchange_strong(previous,c.serial))return 0;}
    if(c.serial!=currentSerial.load()||!Current(c))return 0;
    desired=c.velocity;return c.serial;
}
void FinishSurfaceGrabVelocity(std::uint64_t token,bool success){
    if(!success){fault=true;++refused;lastError=ERROR_FUNCTION_FAILED;return;}
    ++submitted;if(reserved.load()==token)deliveredRelease=token;
}
std::string SurfaceGrabReport(){std::ostringstream s;s<<"enabled="<<enabled.load()<<" owns="<<owns.load()<<" fault="<<fault.load()<<" error="<<lastError.load()
    <<" reachCm="<<reach.load()<<" strength="<<strength.load()<<" limitCm="<<limit.load()<<" captures="<<captures.load()<<" pushes="<<pushes.load()
    <<" submitted="<<submitted.load()<<" nativeReads="<<nativeReads.load()<<" ownerError="<<ownerError.load()<<" queries="<<queries.load()<<" colliderRejected="<<rejectedCollider.load()<<" cancelled="<<cancelled.load()<<" refused="<<refused.load();return s.str();}
}
