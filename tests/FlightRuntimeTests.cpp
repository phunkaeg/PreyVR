// Production input/basis adapter. Only native calls and external publications
// are substituted; no game, injection or fake native object layout is involved.
#include "../src/dll/FlightLane.cpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace preyvr;using namespace preyvr::dll;
TrackingFrame frame{};
FlightOwner owner{0x1000,0x2000,0x18e0,9};
bool tracking=true,poseValid=true,gameplay=true,optionsOwned=false,hacking=false,oldFacesReleased=true;
bool headRelative=true,nativeBoost=false,nativeBrake=false,failBoostPress=false,refuseBoostCleanup=false;
DWORD ownerError=0;
bool refuseAltitudeRelease=false;
bool surfaceOwned=false;
std::uintptr_t posePlayerOverride=0;
std::uint64_t reference=2,menu=1;
struct Event{int key;unsigned state;};std::vector<Event> events;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Step(){auto stamp=MonotonicNanoseconds();while(stamp<=frame.publishedNs)stamp=MonotonicNanoseconds();
    frame.publishedNs=stamp;++frame.sequence;PrepareNativeFlightInput();DrainNativeFlightInput();}
void Reset(){
    flightEnabled=movementMode=0;flightActive=facesOwned=flightCleanup=false;flightGeneration=1;
    flightError=0;nativeState=0;flightPresses=flightReleases=flightRefused=rotationReads=0;
    flightOwnerError=ERROR_NOT_READY;flightPoseValid=flightReady=false;
    rotationSample.Clear();prepared={};flightControls={};flightOutput={};contextSerial=boundSettings=boundMenu=0;
    boostOwner=brakeOwner=0;setterDebt=0;flightQuarantine=boundHeadRelative=boundSurface=surfaceOwned=false;
    frame={};frame.epoch=1;frame.headValidity={true,true,true,true,0};owner={0x1000,0x2000,0x18e0,9};
    tracking=poseValid=gameplay=oldFacesReleased=headRelative=true;optionsOwned=hacking=nativeBoost=nativeBrake=false;
    failBoostPress=refuseBoostCleanup=refuseAltitudeRelease=false;posePlayerOverride=0;ownerError=0;reference=2;menu=1;events.clear();
}
void Enable(){Check(SetNativeFlightControls(1)==0,"enable native flight");SetNativeFlightActive(true);Step();}
unsigned Count(int key,unsigned state){unsigned count=0;for(auto e:events)if(e.key==key&&e.state==state)++count;return count;}
}
namespace preyvr::dll {
void PrepareSurfaceGrab(const FlightOwner&,const GameplayPoseFrame&,const TrackingFrame&,bool){}
bool SurfaceGrabOwnsMotion(){return surfaceOwned;}
DWORD EnsureNativeFlightHook(){return 0;}
DWORD ReadNativeFlightOwner(FlightOwner& out){out=owner;return ownerError;}
DWORD SetNativeFlightBoost(std::uintptr_t player,bool pressed){
    if(player!=owner.player)return pressed?ERROR_NOT_READY:0;
    if(!pressed&&refuseBoostCleanup)return ERROR_BUSY;
    nativeBoost=pressed;return pressed&&failBoostPress?ERROR_BUSY:0;
}
DWORD SetNativeFlightBrake(std::uintptr_t player,bool pressed){
    if(player!=owner.player)return pressed?ERROR_NOT_READY:0;nativeBrake=pressed;return 0;
}
bool TryGetTrackingFrame(TrackingFrame& out){out=frame;return tracking;}
bool TryGetGameplayPoseFrame(GameplayPoseFrame& out,bool){out={};out.player=posePlayerOverride?posePlayerOverride:owner.player;out.tracking=frame;
    out.referenceGeneration=reference;out.yaw=.3f;out.publishedNs=frame.publishedNs;return poseValid;}
unsigned long long HeadTrackingReferenceGeneration(){return reference;}
unsigned long long HudMenuEpoch(){return menu;}
bool HudGameplayInputAllowed(){return gameplay;}
bool VrOptionsInputOwned(){return optionsOwned;}
bool HackingInputOwnsControls(){return hacking;}
bool HeadRelativeMovementEnabled(){return headRelative;}
int MoveLaneCinematicGate(){return 0;}
bool GameplayFacesReleasedForFlight(){return oldFacesReleased;}
DWORD PostRawInputImmediate(int key,unsigned state,int){
    Check(key==input::kButtonA||key==input::kButtonB,"only proven altitude keys are posted; brake has no half shoulder events");
    events.push_back({key,state});return refuseAltitudeRelease&&state==input::kStateReleased?ERROR_BUSY:0;
}
}
int main(){try{
    using namespace preyvr;using namespace preyvr::dll;
    Reset();Step();Check(events.empty()&&!NativeFlightOwnsFaces(),"off leaves normal controls alone");
    frame.hands[1].menuAccept=true;Enable();Check(NativeFlightMovementMode()==1&&events.empty(),"held entry quarantined");
    frame.hands[1].menuAccept=false;Step();Check(NativeFlightMovementMode()==2,"neutral flight owns coherent movement");
    frame.hands[1].menuAccept=true;Step();Check(Count(input::kButtonA,input::kStatePressed)==1,"one native A for ascent");
    Step();Check(Count(input::kButtonA,input::kStatePressed)==1,"held ascent does not refire");
    frame.hands[0].menuCancel=true;Step();Check(nativeBoost,"Y requests boost through native setter");
    frame.hands[0].menuCancel=false;Step();Check(!nativeBoost,"Y release clears native boost even with gamepad prompt mode");
    frame.hands[1].menuCancel=true;Step();Check(nativeBrake&&NativeFlightMovementMode()==1,"A+B brakes and zeros stick");
    Check(Count(input::kButtonA,input::kStateReleased)==1,"ascent released before braking");
    frame.hands[1].menuCancel=false;Step();Check(!nativeBrake,"brake clears before returning to altitude input");
    frame.head.orientation={std::sin(.3f),0,0,std::cos(.3f)};Step();
    flight::NativeQuat rotation{};Check(ApplyNativeFlightRotation(owner.entity,0x1270651,rotation),"only native thrust read can steer");
    Check(Rotate(Quaternion{rotation.x,rotation.y,rotation.z,rotation.w},{0,1,0}).z>.5f,"native basis includes headset pitch");
    const auto before=rotation;
    Check(!ApplyNativeFlightRotation(owner.entity,0x1270652,rotation)&&rotation.x==before.x,"unrelated getter callers untouched");
    Check(!ApplyNativeFlightRotation(owner.entity+8,0x1270651,rotation),"other entity untouched");
    reference+=2;Check(!ApplyNativeFlightRotation(owner.entity,0x1270651,rotation),"recenter invalidates basis immediately");
    Step();Check(Count(input::kButtonA,input::kStateReleased)==2,"recenter releases owned ascent");
    gameplay=false;Step();Check(!NativeFlightCleanupPending(),"modal completes cleanup");
    Check(!ApplyNativeFlightRotation(owner.entity,0x1270651,rotation),"modal cannot steer physics");

    Reset();oldFacesReleased=false;Enable();Check(NativeFlightMovementMode()==1,"old gameplay face debt blocks acquisition");
    oldFacesReleased=true;Step();Check(NativeFlightMovementMode()==2,"old owner cleanup permits neutral handoff");
    frame.hands[0].menuCancel=true;failBoostPress=refuseBoostCleanup=true;Step();
    Check(nativeBoost&&NativeFlightCleanupPending()&&NativeFlightMovementMode()==1,"setter fault after write retains forced cleanup debt");
    failBoostPress=refuseBoostCleanup=false;Step();Check(!nativeBoost&&!NativeFlightCleanupPending(),"cleanup debt retries and clears");
    Step();Check(!nativeBoost,"held faulty source cannot immediately reacquire");
    frame.hands[0].menuCancel=false;Step();frame.hands[0].menuCancel=true;Step();Check(nativeBoost,"neutral rearms after fault");
    optionsOwned=true;Step();Check(!nativeBoost,"VR options clear native boost");

    Reset();Enable();headRelative=false;Step();rotation={};
    Check(!ApplyNativeFlightRotation(owner.entity,0x1270651,rotation),"body-relative setting retains native entity basis");
    frame.hands[1].menuCancel=true;Step();tracking=false;Step();Check(!NativeFlightCleanupPending(),"tracking loss releases altitude");
    Reset();Enable();owner.state=15;Step();Check(NativeFlightMovementMode()==0,"gravity shaft is not ordinary zero-g flight");
    ownerError=ERROR_NOT_READY;Step();Check(NativeFlightMovementMode()==1,"unknown owner refuses flight/ground crossover");
    Reset();Enable();frame.hands[0].menuCancel=true;Step();SetNativeFlightActive(false);Step();Check(!nativeBoost&&!NativeFlightCleanupPending(),"VR stop clears native request");
    Reset();Enable();frame.hands[1].menuAccept=true;Step();gameplay=false;refuseAltitudeRelease=true;Step();
    Check(NativeFlightCleanupPending()&&NativeFlightOwnsFaces(),"refused altitude release retains modal/hacking interlock");
    refuseAltitudeRelease=false;Step();Check(!NativeFlightCleanupPending(),"accepted altitude cleanup frees handoff");
    Reset();Enable();posePlayerOverride=owner.player+8;Step();Check(NativeFlightMovementMode()==1,"old/mismatched gameplay pose cannot steer new owner");
    Reset();Enable();TrackingFrame readback{};tracking=false;
    Check(!ReadNativeFlightTracking(readback),"focus loss invalidates prepared movement immediately");
    Check(!ApplyNativeFlightRotation(owner.entity,0x1270651,rotation),"focus loss invalidates prepared thrust basis immediately");
    Reset();Enable();++frame.epoch;
    Check(!ApplyNativeFlightRotation(owner.entity,0x1270651,rotation),"new XR epoch invalidates prepared thrust basis immediately");
    Reset();Enable();frame.hands[0].menuCancel=true;Step();frame.hands[0].gripPressed=frame.hands[1].gripPressed=true;Step();
    Check(!nativeBoost,"recenter chord cancels boost rather than combining actions");
    Reset();Enable();frame.hands[0].menuCancel=true;Step();owner.player+=0x100;owner.entity+=0x100;owner.input+=0x100;nativeBoost=false;Step();
    Check(!nativeBoost&&!NativeFlightCleanupPending(),"replacement player does not inherit boost or old receiver write");
    Step();Check(!nativeBoost,"held boost source is quarantined across player replacement");
    Reset();Enable();frame.hands[0].menuCancel=true;Step();owner.cinematic=1;Step();
    Check(!nativeBoost&&NativeFlightMovementMode()==1,"live native cinematic gate overrides a stale analog observation");
    Check(!ApplyNativeFlightRotation(owner.entity,0x1270651,rotation),"cinematics retain original native rotation result");
    Reset();Enable();frame.hands[0].menuCancel=true;Step();Check(nativeBoost,"boost positive control before surface handoff");
    surfaceOwned=true;Step();Check(!nativeBoost&&NativeFlightMovementMode()==1,"surface anchor releases boost and suppresses stick thrust");
    surfaceOwned=false;Step();Check(!nativeBoost&&NativeFlightMovementMode()==1,"held flight source cannot reacquire on push release");
    frame.hands[0].menuCancel=false;Step();Check(NativeFlightMovementMode()==2,"neutral rearms flight after anchor handoff");
    std::cout<<"flight_runtime: native boundaries, scoped basis, modal/context handoff and setter debt passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
