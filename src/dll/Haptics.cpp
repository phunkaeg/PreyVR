#include "HapticsXr.h"
#include "HeadTrackingHook.h"
#include <atomic>
namespace preyvr::dll {
namespace {
std::atomic<bool> enabled{true};
std::atomic<unsigned> strength{70};
std::atomic<std::uint64_t> serial{0},generation{1},submitted{0},failed{0};
std::array<LatestSnapshot<haptics::Request>,2> pending;
std::array<haptics::Policy,2> policies; // XR frame owner only
std::array<bool,2> active{};
std::array<std::uint64_t,2> ends{};
std::uint64_t previousEpoch=0,previousReference=0,previousGeneration=0;
void Stop(XrSession session,XrAction action,const std::array<XrPath,2>& paths){
    for(unsigned h=0;h<2;++h){
        if(active[h]&&session&&action){
            XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=action;info.subactionPath=paths[h];
            xrStopHapticFeedback(session,&info);
        }
        active[h]=false;ends[h]=0;
    }
}
}
void QueueHaptic(Hand hand,haptics::Event event,const TrackingFrame& frame){
    const auto h=static_cast<unsigned>(hand);
    if(h>=2||!enabled.load()||!strength.load()||!FreshSample(MonotonicNanoseconds(),frame.publishedNs))return;
    const auto reference=HeadTrackingReferenceGeneration();
    if(reference&1)return;
    pending[h].Publish({event,++serial,frame.epoch,reference,frame.publishedNs,generation.load()});
}
void SetHapticsEnabled(unsigned on){if(enabled.exchange(on!=0)!=(on!=0))++generation;}
unsigned HapticsEnabled(){return enabled.load()?1u:0u;}
void SetHapticStrength(unsigned value){strength=std::min(value,100u);++generation;}
unsigned HapticStrength(){return strength.load();}
void ServiceHaptics(XrSession session,XrAction action,const std::array<XrPath,2>& paths,const TrackingFrame& frame,bool focused){
    const auto now=MonotonicNanoseconds(),reference=HeadTrackingReferenceGeneration(),gen=generation.load();
    const bool allowed=focused&&session&&action&&enabled.load()&&strength.load()&&!(reference&1)&&
        FreshSample(now,frame.publishedNs)&&IsPoseUsable(frame.head,frame.headValidity,200000000);
    if(!allowed||frame.epoch!=previousEpoch||reference!=previousReference||gen!=previousGeneration)
        Stop(session,action,paths);
    previousEpoch=frame.epoch;previousReference=reference;previousGeneration=gen;
    for(unsigned h=0;h<2;++h){
        if(now>=ends[h])active[h]=false;
        haptics::Request request{};
        if(!pending[h].TryRead(request))continue;
        const auto pulse=policies[h].Consume(request,now,frame.epoch,reference,gen,strength.load(),allowed&&
            IsPoseUsable(frame.hands[h].gripPose,frame.hands[h].gripValidity,200000000));
        if(!pulse.duration)continue;
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=action;info.subactionPath=paths[h];
        XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};vibration.duration=pulse.duration;
        vibration.amplitude=pulse.amplitude;vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
        const auto result=xrApplyHapticFeedback(session,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
        if(result==XR_SUCCESS){++submitted;active[h]=true;ends[h]=now+pulse.duration;}else ++failed;
    }
}
void ResetHaptics(XrSession session,XrAction action,const std::array<XrPath,2>& paths){
    Stop(session,action,paths);++generation;
    for(auto& queue:pending)queue.Clear();
    for(auto& policy:policies)policy.Reset();
    previousEpoch=previousReference=previousGeneration=0;
}
std::string HapticsReport(){return " enabled="+std::to_string(enabled.load())+" strength="+std::to_string(strength.load())+
    " submitted="+std::to_string(submitted.load())+" failed="+std::to_string(failed.load());}
}
