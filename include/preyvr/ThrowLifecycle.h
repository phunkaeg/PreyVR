#pragma once
#include "preyvr/HandCarry.h"
#include <array>
#include <cstdint>
#include <limits>

namespace preyvr::carry {
// XrTime and the monotonic publication clock have different origins. Only add
// an elapsed publication duration to XrTime; never compare their absolute values.
struct TrackingStamp {
    std::uint64_t epoch=0, reference=0, sequence=0, publishedNs=0;
    std::int64_t displayTime=0;
};
inline bool SameSpace(const TrackingStamp& a,const TrackingStamp& b) {
    return !(a.reference&1) && a.epoch==b.epoch && a.reference==b.reference;
}
class TrackedMotionHistory {
public:
    void Reset(){for(auto& h:history_)h.Reset();stamps_={};}
    void Push(unsigned hand,const TrackingStamp& stamp,const MotionSample& sample,bool valid) {
        if(hand>=2)return;
        auto& h=history_[hand];
        if(!valid || (stamp.reference&1) || !stamp.sequence || !stamp.publishedNs ||
           stamp.displayTime<=0 || sample.time!=stamp.displayTime || !Finite(sample.position) || !Finite(sample.orientation)) {
            h.Reset();stamps_[hand]={};return;
        }
        if(SameSpace(stamps_[hand],stamp) && stamps_[hand].sequence &&
           (stamp.sequence<=stamps_[hand].sequence || sample.time<=stamps_[hand].displayTime)) {
            h.Reset();stamps_[hand]={};return;
        }
        if(!SameSpace(stamps_[hand],stamp))h.Reset();
        h.Push(sample);stamps_[hand]=stamp;
    }
    Motion Estimate(unsigned hand,const TrackingStamp& current,std::uint64_t now) const {
        if(hand>=2)return {};
        const auto& stored=stamps_[hand];
        constexpr std::uint64_t maxAge=60000000;
        if(!SameSpace(stored,current) || !stored.sequence || current.sequence<stored.sequence ||
           !stored.publishedNs || now<stored.publishedNs || now-stored.publishedNs>maxAge ||
           !current.publishedNs || now<current.publishedNs || now-current.publishedNs>maxAge ||
           current.displayTime<stored.displayTime || current.displayTime<=0)return {};
        const auto age=static_cast<std::int64_t>(now-current.publishedNs);
        if(current.displayTime>std::numeric_limits<std::int64_t>::max()-age)return {};
        return history_[hand].Estimate(current.displayTime+age);
    }
private:
    std::array<MotionHistory,2> history_{};
    std::array<TrackingStamp,2> stamps_{};
};

// A safety release still goes to the engine, but cannot authorize a VR throw.
// After lost tracking/modal ownership/recenter, require a neutral trigger first.
class FireGate {
public:
    struct Result {bool pressed=false, physical=false;};
    Result Update(bool tracked,bool allowed,bool forced,bool chord,bool pressed,bool neutral,
                  std::uint64_t epoch,std::uint64_t reference) {
        const bool changed=!haveSpace_ || epoch!=epoch_ || reference!=reference_;
        haveSpace_=true;epoch_=epoch;reference_=reference;
        const bool usable=tracked&&allowed&&!forced&&!chord&&!(reference&1)&&!changed;
        if(!usable)blocked_=true;
        else if(neutral)blocked_=false;
        return {usable&&!blocked_&&pressed,usable&&!blocked_};
    }
private:
    bool blocked_=true,haveSpace_=false;
    std::uint64_t epoch_=0,reference_=0;
};
}
