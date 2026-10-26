// Production command adapter, with external publications and native/query
// boundaries replaced. This is not a game or XR runtime acceptance test.
#include "../src/dll/SurfaceGrabLane.cpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace preyvr;using namespace preyvr::dll;
TrackingFrame fFrame{};GameplayPoseFrame fPose{};SurfaceOwner fOwner{{0x1000,0x2000,0x18E0,9},0x3000};
bool fTracking=true,fGameplay=true,fOptions=false,fHacking=false,fBase=true,fActive=true;
bool fUse=false,fCarry=false,fMedkit=false,fScope=false,fSupport=false,fTwoHand=false;
bool fFixed=true,fGeometry=true,fCleanup=false,fFaces=true,fGripFree=true;
DWORD fOwnerError=0;std::uint64_t fReference=2,fMenu=1,fGeneration=1;unsigned fHaptics=0;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Step(bool wait=false){
    if(wait)Sleep(10);
    auto stamp=MonotonicNanoseconds();while(stamp<=fFrame.publishedNs)stamp=MonotonicNanoseconds();
    fFrame.publishedNs=stamp;++fFrame.sequence;fPose.publishedNs=stamp;fPose.tracking=fFrame;
    PrepareSurfaceGrab(fOwner.flight,fPose,fFrame,fTracking&&fGameplay&&!fOptions&&!fHacking);
}
void Reset(){
    enabled=0;reach=10;strength=100;limit=150;settings=1;currentSerial=reserved=deliveredRelease=0;owns=fault=false;lastError=0;
    nativeReads=queries=rejectedCollider=captures=pushes=cancelled=submitted=refused=0;ownerError=ERROR_NOT_READY;
    command.Clear();grab={};pending={};nextSerial=context=boundSettings=boundFlight=boundMenu=0;
    fFrame={};fFrame.epoch=1;fFrame.headValidity=fFrame.hands[0].gripValidity={true,true,true,true,0};
    fFrame.hands[0].gripPose.position={.05f,0,0};fOwner={{0x1000,0x2000,0x18E0,9},0x3000};
    fPose={};fPose.player=fOwner.flight.player;fPose.weaponGeneration=1;fPose.referenceGeneration=2;fPose.cameraCentreValid=true;
    fTracking=fGameplay=fBase=fActive=fFixed=fGeometry=fFaces=fGripFree=true;
    fOptions=fHacking=fUse=fCarry=fMedkit=fScope=fSupport=fTwoHand=fCleanup=false;
    fOwnerError=0;fReference=2;fMenu=fGeneration=1;fHaptics=0;
    Check(SetSurfaceGrabEnabled(1)==0,"surface enable");Step();
}
void Capture(){fFrame.hands[0].gripPressed=true;Step();Check(SurfaceGrabOwnsMotion(),"fixed nearby wall capture");}
std::uint64_t Claim(Vec3& v){return ClaimSurfaceGrabVelocity(fOwner.animated,surfacegrab::native::Caller,4,{},v);}
void Pull(){for(int i=0;i<8;++i){fFrame.hands[0].gripPose.position.x-=.01f;Step(true);}}
}
namespace preyvr::dll {
DWORD EnsureSurfaceGrabHook(){return 0;}
DWORD ReadSurfaceGrabOwner(SurfaceOwner& o){o=fOwner;return fOwnerError;}
bool IsFixedSurfaceCollider(std::uintptr_t c){return c==0x4000&&fFixed;}
unsigned NativeFlightControlsEnabled(){return fBase;}
bool NativeFlightSessionActive(){return fActive;}
std::uint64_t NativeFlightContextGeneration(){return fGeneration;}
bool NativeFlightCleanupPending(){return fCleanup;}
bool GameplayFacesReleasedForFlight(){return fFaces;}
bool GameplayGripReleasedForSurface(){return fGripFree;}
bool TryGetTrackingFrame(TrackingFrame& o){o=fFrame;return fTracking;}
bool TryGetGameplayPoseFrame(GameplayPoseFrame& o,bool){o=fPose;return true;}
unsigned long long HeadTrackingReferenceGeneration(){return fReference;}
unsigned long long HudMenuEpoch(){return fMenu;}
bool HudGameplayInputAllowed(){return fGameplay;}
bool VrOptionsInputOwned(){return fOptions;}
bool HackingInputOwnsControls(){return fHacking;}
bool UseTargetPresent(){return fUse;}bool UseCarrying(){return fCarry;}
bool MedkitZoneHasLeftHand(){return fMedkit;}bool PsychoscopeZoneHasLeftHand(){return fScope;}
bool SupportGripNear(){return fSupport;}bool TwoHandedAimHeld(){return fTwoHand;}
void QueueHaptic(Hand,haptics::Event,const TrackingFrame&){++fHaptics;}
bool QueryPhysicalSegment(const GameplayPoseFrame&,Vec3 origin,Vec3 delta,scene::Hit& hit){
    hit={};hit.distance=-1;if(!fGeometry||std::fabs(delta.x)<1e-6f)return true;
    const float t=-origin.x/delta.x;if(t<0||t>1)return true;
    hit.distance=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z)*t;
    hit.collider=reinterpret_cast<void*>(0x4000);hit.partId=3;hit.normal={1,0,0};
    hit.point={0,origin.y+t*delta.y,origin.z+t*delta.z};return true;
}
}
int main(){try{
    Reset();Capture();Vec3 desired{};auto token=Claim(desired);Check(token&&desired.x==0,"held command reaches exact native seam");FinishSurfaceGrabVelocity(token,true);
    Check(!ClaimSurfaceGrabVelocity(fOwner.animated,surfacegrab::native::Caller+1,4,{},desired),"unrelated caller untouched");
    Check(!ClaimSurfaceGrabVelocity(fOwner.animated+8,surfacegrab::native::Caller,4,{},desired),"other animated character untouched");
    Check(!ClaimSurfaceGrabVelocity(fOwner.animated,surfacegrab::native::Caller,3,{},desired),"other movement type untouched");
    fCleanup=true;Check(!Claim(desired),"native input cleanup debt blocks override");fCleanup=false;
    fFaces=false;Check(!Claim(desired),"previous altitude owner must release");fFaces=true;
    fOwnerError=ERROR_ACCESS_DISABLED_BY_POLICY;Check(!Claim(desired),"native status/parent restriction refuses override");fOwnerError=0;
    Pull();fFrame.hands[0].gripPressed=false;Step(true);Check(pending.serial&&SurfaceGrabOwnsMotion(),"physical release publishes short pending command");
    const auto serial=pending.serial;token=Claim(desired);Check(token==serial,"physical push reaches native command");
    Check(desired.x>=.15f&&desired.x<=1.5f,"physical push respects launch threshold and limit");
    Check(!Claim(desired),"push reserved once before acknowledgement");Step();Check(pending.serial==serial,"unacknowledged push not refreshed or reserialised");
    Check(ReadSurfaceGrabCounters().releaseSerial!=token,"reservation alone is not a delivered-release receipt");
    FinishSurfaceGrabVelocity(token,true);Check(ReadSurfaceGrabCounters().releaseSerial==token,"delivered release serial appears only after native acknowledgement");
    Check(!Claim(desired),"successful push cannot be replayed");Step();Check(!SurfaceGrabOwnsMotion()&&!pending.serial,"acknowledged push releases ownership");
    for(int gate=0;gate<12;++gate){Reset();Capture();
        switch(gate){case 0:fTracking=false;break;case 1:fGameplay=false;break;case 2:fOptions=true;break;case 3:fHacking=true;break;
        case 4:fReference+=2;break;case 5:++fMenu;break;case 6:++fPose.weaponGeneration;break;case 7:fBase=false;break;
        case 8:fActive=false;break;case 9:++fGeneration;break;case 10:fCarry=true;break;case 11:fSupport=true;break;}
        Check(!Claim(desired),"published native lease revalidates modal, reference, equipment and ownership immediately");
        Step();Check(!SurfaceGrabOwnsMotion()&&!pending.serial,"context cancellation cannot leave a pending launch");
    }
    for(int busy=0;busy<7;++busy){Reset();
        switch(busy){case 0:fUse=true;break;case 1:fCarry=true;break;case 2:fMedkit=true;break;case 3:fScope=true;break;
        case 4:fSupport=true;break;case 5:fTwoHand=true;break;case 6:fGripFree=false;break;}
        fFrame.hands[0].gripPressed=true;Step();Check(!SurfaceGrabOwnsMotion(),"existing use/equipment/foregrip owner has capture priority");
    }
    Reset();fFixed=false;fFrame.hands[0].gripPressed=true;Step();Check(!SurfaceGrabOwnsMotion()&&rejectedCollider.load(),"movable nearest object cannot anchor or see through to wall");
    Reset();Capture();fGeometry=false;Step();Check(!SurfaceGrabOwnsMotion(),"removed/moved surface cancels held anchor");
    Reset();Capture();fFrame.hands[0].gripPose.position.x=.5f;Step();Check(!SurfaceGrabOwnsMotion(),"tracking jump cancels without push");
    Reset();Capture();Pull();fFrame.hands[0].gripPressed=false;Step(true);Check(pending.serial,"pending push positive control");
    fFrame.hands[0].gripPressed=true;Check(!Claim(desired),"new squeeze immediately revokes pending release");
    Step();Check(!SurfaceGrabOwnsMotion()&&!pending.serial,"pending release cannot become a new held anchor");
    Reset();Capture();Pull();fFrame.hands[0].gripPressed=false;Step(true);Check(pending.serial,"expiry positive control");
    Sleep(110);Check(!Claim(desired),"expired release cannot be delivered late");Step();Check(!pending.serial,"expired pending push cleared without refreshing lease");
    Reset();Check(SetSurfaceGrabEnabled(2)==ERROR_INVALID_PARAMETER&&SetSurfaceGrabReach(19)==ERROR_INVALID_PARAMETER&&
        SetSurfacePushStrength(151)==ERROR_INVALID_PARAMETER&&SetSurfacePushLimit(301)==ERROR_INVALID_PARAMETER,"invalid surface settings refused");
    Reset();Capture();FinishSurfaceGrabVelocity(Claim(desired),false);Check(!Claim(desired)&&SetSurfaceGrabEnabled(1)!=0,"native transaction fault disarms instead of retry loop");
    std::cout<<"surface_grab_runtime: production ownership, native lease, one-shot acknowledgement and safety boundaries passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
