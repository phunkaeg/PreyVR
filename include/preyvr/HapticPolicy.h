#pragma once
#include "preyvr/LatestSnapshot.h"
#include <algorithm>
namespace preyvr::haptics {
enum class Event { MenuChange, ForegripAttached };
struct Request {
    Event event=Event::MenuChange;
    std::uint64_t serial=0,epoch=0,reference=0,stamp=0,generation=0;
};
struct Pulse {float amplitude=0;std::int64_t duration=0;};
class Policy {
public:
    Pulse Consume(const Request& r,std::uint64_t now,std::uint64_t epoch,std::uint64_t reference,
                  std::uint64_t generation,unsigned strength,bool allowed){
        if(!r.serial||r.serial<=seen_)return {};
        seen_=r.serial;
        if(!allowed||!strength||r.epoch!=epoch||r.reference!=reference||r.generation!=generation||
           !FreshSample(now,r.stamp,100000000)||
           (last_&&now>=last_&&now-last_<40000000))return {};
        last_=now;
        const float amplitude=r.event==Event::ForegripAttached?.45f:.25f;
        return {amplitude*std::min(strength,100u)*.01f,r.event==Event::ForegripAttached?35000000:18000000};
    }
    void Reset(){*this={};}
private:
    std::uint64_t seen_=0,last_=0;
};
}
