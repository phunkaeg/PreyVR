#pragma once
#include "preyvr/VrMath.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace preyvr::scene {
// Steam native layout, proved by parameter producers and concrete consumers.
// Never use the native weapon wrapper: it owns a shared global hit buffer.
struct Hit {
    float distance = 0;
    std::uint32_t padding = 0;
    void* collider = nullptr;
    int part = 0, partId = 0;
    short surface = 0, material = 0;
    int foreignIndex = 0, node = 0;
    Vec3 point{}, normal{};
    int terrain = 0, primitive = 0;
    std::uint32_t padding2 = 0;
    Hit* next = nullptr;
};
struct Params {
    void* foreignData = nullptr;
    int foreignId = 0;
    std::uint32_t padding = 0;
    void* callback = nullptr;
    Vec3 origin{}, delta{};
    int objectTypes = 0;
    unsigned flags = 0;
    Hit* hits = nullptr;
    int capacity = 0;
    std::uint32_t padding2 = 0;
    void* cached = nullptr;
    int skipCount = 0;
    std::uint32_t padding3 = 0;
    void** skip = nullptr;
    unsigned collisionType = 0, collisionIgnore = 0;
    std::uint64_t trailing = 0;
};
static_assert(sizeof(Hit)==0x50 && offsetof(Hit,point)==0x24 && offsetof(Hit,terrain)==0x3c);
static_assert(sizeof(Params)==0x70 && offsetof(Params,origin)==0x18 && offsetof(Params,delta)==0x24);
static_assert(offsetof(Params,hits)==0x38 && offsetof(Params,capacity)==0x40);
static_assert(offsetof(Params,skipCount)==0x50 && offsetof(Params,skip)==0x58);
static_assert(offsetof(Params,collisionIgnore)==0x64);
constexpr float Range = 200.f;
inline bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool Build(Params& p,Vec3 origin,Vec3 direction,Hit& hit,void** skip,int count){
    p={};
    const float norm=direction.x*direction.x+direction.y*direction.y+direction.z*direction.z;
    if(!Finite(origin)||!Finite(direction)||std::fabs(norm-1.f)>.002f||!skip||count<1||count>2)return false;
    for(int i=0;i<count;++i)if(!skip[i])return false;
    hit={};hit.distance=-1;
    p.origin=origin;p.delta={direction.x*Range,direction.y*Range,direction.z*Range};
    p.objectTypes=0x11f;p.flags=0xf;p.hits=&hit;p.capacity=1;p.skipCount=count;p.skip=skip;
    p.collisionIgnore=0x400000;
    return true;
}
// A zero-distance contact is valid. Negative means no usable hit; never retain
// yesterday's target after a miss, refusal, equip change or tracking loss.
inline float Distance(int count,const Hit& hit){
    return count==1&&std::isfinite(hit.distance)&&hit.distance>=0&&hit.distance<=Range?hit.distance:-1.f;
}
inline float ReticleDistance(bool enabled,float hit,float fallback){
    return enabled&&std::isfinite(hit)&&hit>=0&&hit<=Range?hit:fallback;
}
}
