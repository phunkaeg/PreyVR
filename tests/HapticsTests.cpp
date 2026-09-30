#include "../src/dll/HapticsXr.h"
#include <cstdlib>
#include <iostream>
using namespace preyvr;
namespace {unsigned applications=0,stops=0;XrPath lastHand=0;float amplitude=0;XrDuration duration=0;std::uint64_t reference=2;XrResult result=XR_SUCCESS;}
namespace preyvr::dll {std::uint64_t HeadTrackingReferenceGeneration(){return reference;}}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession,const XrHapticActionInfo* info,const XrHapticBaseHeader* base){
    ++applications;lastHand=info->subactionPath;const auto& pulse=*reinterpret_cast<const XrHapticVibration*>(base);
    amplitude=pulse.amplitude;duration=pulse.duration;return result;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(XrSession,const XrHapticActionInfo*){++stops;return XR_SUCCESS;}
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    using namespace dll;
    haptics::Policy policy;haptics::Request request{haptics::Event::MenuChange,1,2,4,1000000000,1};
    Check(policy.Consume(request,1000000000,2,4,1,70,true).duration>0,"fresh event accepted");
    Check(!policy.Consume(request,1000000000,2,4,1,70,true).duration,"event consumed once");
    request.serial=2;request.stamp+=10000000;
    Check(!policy.Consume(request,request.stamp,2,4,1,70,true).duration,"bounded repeat rate");
    request.serial=3;request.stamp+=100000000;
    Check(!policy.Consume(request,request.stamp,3,4,1,70,true).duration,"old tracking epoch refused");
    request.serial=4;Check(!policy.Consume(request,request.stamp,2,6,1,70,true).duration,"old reference refused");
    request.serial=5;Check(!policy.Consume(request,request.stamp,2,4,2,70,true).duration,"old settings generation refused");
    request.serial=6;Check(!policy.Consume(request,request.stamp+100000001,2,4,1,70,true).duration,"stale event refused");
    request.serial=7;Check(!policy.Consume(request,request.stamp,2,4,1,0,true).duration,"zero strength silent");
    request.serial=8;Check(!policy.Consume(request,request.stamp,2,4,1,70,false).duration,"unfocused silent");
    const auto session=reinterpret_cast<XrSession>(1);
    const auto action=reinterpret_cast<XrAction>(2);const std::array<XrPath,2> paths{11,12};
    TrackingFrame frame;frame.epoch=1;frame.publishedNs=MonotonicNanoseconds();
    frame.headValidity={true,true,true,true,0};
    for(auto& hand:frame.hands)hand.gripValidity={true,true,true,true,0};
    QueueHaptic(Hand::left,haptics::Event::ForegripAttached,frame);
    ServiceHaptics(session,action,paths,frame,true);
    Check(applications==1&&lastHand==11&&duration==35000000&&amplitude>.31f&&amplitude<.32f,"real adapter routes bounded pulse to support hand");
    ServiceHaptics(session,action,paths,frame,true);Check(applications==1,"no repeated XR submissions");
    SetHapticsEnabled(0);ServiceHaptics(session,action,paths,frame,true);Check(stops==1,"disable stops active pulse");
    SetHapticsEnabled(1);ResetHaptics(session,action,paths);
    QueueHaptic(Hand::right,haptics::Event::MenuChange,frame);
    ServiceHaptics(session,action,paths,frame,false);ServiceHaptics(session,action,paths,frame,true);
    Check(applications==1,"unfocused event cannot replay on focus recovery");
    ResetHaptics(session,action,paths);QueueHaptic(Hand::right,haptics::Event::MenuChange,frame);
    ++frame.epoch;ServiceHaptics(session,action,paths,frame,true);Check(applications==1,"session epoch rejects queued event");
    ResetHaptics(session,action,paths);QueueHaptic(Hand::right,haptics::Event::MenuChange,frame);
    reference+=2;ServiceHaptics(session,action,paths,frame,true);Check(applications==1,"recenter rejects queued event");
    ResetHaptics(session,action,paths);QueueHaptic(Hand::right,haptics::Event::MenuChange,frame);
    result=XR_ERROR_RUNTIME_FAILURE;ServiceHaptics(session,action,paths,frame,true);ServiceHaptics(session,action,paths,frame,true);
    Check(applications==2,"failed XR request is not retried every frame");
    ResetHaptics(session,action,paths);std::cout<<"Haptic freshness, focus, routing, cancellation and failure policies passed\n";
}
