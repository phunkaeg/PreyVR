#include "RegressionProbe.h"
#include "AimTakeover.h"
#include "BodyFollow.h"
#include "FlightLane.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "HudLayer.h"
#include "SurfaceGrabLane.h"
#include "VrOptionsRuntime.h"
#include "XrSessionHost.h"
#include "preyvr/LatestSnapshot.h"
#include <atomic>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
namespace preyvr::dll {
namespace {
int moduleAnchor=0;
std::atomic<std::uint64_t> samples{0};
std::string ModulePath(){
    HMODULE module=nullptr;wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
       reinterpret_cast<LPCWSTR>(&moduleAnchor),&module))return {};
    const auto n=GetModuleFileNameW(module,path,32768);if(!n||n>=32768)return {};
    const auto bytes=WideCharToMultiByte(CP_UTF8,0,path,static_cast<int>(n),nullptr,0,nullptr,nullptr);
    std::string out(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,path,static_cast<int>(n),out.data(),bytes,nullptr,nullptr);return out;
}
std::string Quoted(std::string_view v){std::string s="\"";for(const unsigned char c:v){if(c=='"'||c=='\\')s+='\\';if(c<32){s+="?";}else s+=static_cast<char>(c);}return s+'"';}
void Number(std::ostream& s,float v){if(std::isfinite(v))s<<v;else s<<"null";}
void Vector(std::ostream& s,Vec3 v){s<<'[';Number(s,v.x);s<<',';Number(s,v.y);s<<',';Number(s,v.z);s<<']';}
void Rotation(std::ostream& s,Quaternion q){s<<'[';Number(s,q.x);s<<',';Number(s,q.y);s<<',';Number(s,q.z);s<<',';Number(s,q.w);s<<']';}
}
std::string RegressionStateJson(std::string_view request){
    if(request.empty()||request.size()>64||request.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string_view::npos)
        return "{\"schema\":1,\"result\":87,\"error\":\"invalid_request\"}";
    const auto now=MonotonicNanoseconds(),ref=HeadTrackingReferenceGeneration();
    TrackingFrame t{};GameplayPoseFrame p{};Vec3 support{};
    const bool tracked=TryGetTrackingFrame(t)&&FreshSample(now,t.publishedNs,100000000ull)&&
        IsPoseUsable(t.head,t.headValidity,100000000ull)&&
        IsPoseUsable(t.hands[0].gripPose,t.hands[0].gripValidity,100000000ull)&&IsPoseUsable(t.hands[1].gripPose,t.hands[1].gripValidity,100000000ull);
    const bool pose=tracked&&TryGetGameplayPoseFrame(p)&&FreshSample(now,p.publishedNs,100000000ull)&&p.cameraCentreValid&&
        p.referenceGeneration==ref&&!(ref&1)&&p.tracking.epoch==t.epoch&&std::isfinite(p.yaw)&&
        std::isfinite(p.cameraCentre.x)&&std::isfinite(p.cameraCentre.y)&&std::isfinite(p.cameraCentre.z);
    const bool hint=pose&&TryGetSupportGripTargetXr(support);
    const auto g=ReadSurfaceGrabCounters();
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(9)<<std::boolalpha;
    s<<"{\"schema\":1,\"result\":0,\"request\":"<<Quoted(request)<<",\"pid\":"<<GetCurrentProcessId()
     <<",\"module\":"<<Quoted(ModulePath())<<",\"sample\":"<<++samples<<",\"nowNs\":"<<now<<",\"reference\":"<<ref
     <<",\"menu\":{\"known\":"<<HudMenuStateKnown()<<",\"mask\":"<<HudVisibleModalMask()<<",\"epoch\":"<<HudMenuEpoch()
     <<",\"samples\":"<<HudMenuStateSampleCount()<<",\"inventory\":"<<HudInventoryIsOpen()<<",\"gameplay\":"<<HudGameplayInputAllowed()
     <<",\"options\":"<<VrOptionsInputOwned()<<",\"navigationEnabled\":"<<MenuNavigationEnabled()<<",\"actions\":"<<MenuNavigationActionCount()<<'}'
     <<",\"tracking\":{\"valid\":"<<tracked<<",\"space\":"<<Quoted(XrTrackingSpaceName())<<",\"sequence\":"<<t.sequence<<",\"epoch\":"<<t.epoch<<",\"stampNs\":"<<t.publishedNs<<",\"displayTimeNs\":"<<t.displayTime<<",\"head\":";
    Vector(s,t.head.position);s<<",\"rotation\":";Rotation(s,t.head.orientation);
    s<<",\"left\":";Vector(s,t.hands[0].gripPose.position);s<<",\"right\":";Vector(s,t.hands[1].gripPose.position);
    s<<",\"leftGrip\":"<<t.hands[0].gripPressed<<",\"rightGrip\":"<<t.hands[1].gripPressed<<'}'
     <<",\"pose\":{\"valid\":"<<pose<<",\"stampNs\":"<<p.publishedNs<<",\"weapon\":"<<p.weaponGeneration<<",\"player\":"<<p.player<<",\"camera\":";
    Vector(s,p.cameraCentre);s<<",\"yaw\":";Number(s,p.yaw);s<<'}'
     <<",\"twoHand\":{\"held\":"<<TwoHandedAimHeld()<<",\"near\":"<<SupportGripNear()<<",\"blend\":";
    Number(s,p.twoHand.blend);s<<",\"target\":";
    if(hint)Vector(s,support);else s<<"null";s<<'}'
     <<",\"flight\":{\"active\":"<<NativeFlightSessionActive()<<",\"state\":"<<NativeFlightObservedState()<<",\"ownerError\":"<<NativeFlightObservedOwnerError()<<'}'
     <<",\"grab\":{\"owns\":"<<g.owns<<",\"fault\":"<<g.fault<<",\"ownerError\":"<<g.ownerError
     <<",\"captures\":"<<g.captures<<",\"pushes\":"<<g.pushes<<",\"submitted\":"<<g.submitted<<",\"releaseSerial\":"<<g.releaseSerial<<",\"cancelled\":"<<g.cancelled<<",\"refused\":"<<g.refused<<'}'
     <<",\"inventoryFrames\":"<<InventoryLayerFrameCount()
     <<",\"settings\":{\"twoHand\":"<<TwoHandedAimEnabled()<<",\"toggle\":"<<TwoHandGripToggle()<<",\"flight\":"<<NativeFlightControlsEnabled()
     <<",\"grab\":"<<SurfaceGrabEnabled()<<",\"reach\":"<<SurfaceGrabReach()<<",\"strength\":"<<SurfacePushStrength()<<",\"limit\":"<<SurfacePushLimit()
     <<",\"inventory\":"<<InventoryCaptureEnabled()<<",\"bodyYaw\":"<<BodyYawFollowEnabled()<<",\"bodyRoom\":"<<BodyRoomScaleEnabled()<<"}}";
    return s.str();
}
}
