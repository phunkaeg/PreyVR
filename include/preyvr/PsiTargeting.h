#pragma once
#include "preyvr/AnimIk.h"
#include "preyvr/MotionController.h"
#include "preyvr/LatestSnapshot.h"
#include <cstdint>
#include <optional>

namespace preyvr::psi {
enum class Mode : unsigned { Native, Head, LeftController };
struct Ray { Vec3 origin{},direction{}; };
static_assert(sizeof(Ray)==24);
inline std::optional<Ray> BuildRay(Mode mode,float yaw,Vec3 cameraCentre,
                                 Pose head,Pose hand,PoseValidity validity) {
    if((mode!=Mode::Head&&mode!=Mode::LeftController)||!std::isfinite(yaw)||!std::isfinite(cameraCentre.x)||
       !std::isfinite(cameraCentre.y)||!std::isfinite(cameraCentre.z))return {};
    stereo::ReferenceFrame ref{};ref.yawRadians=yaw;
    const auto source=mode==Mode::Head?head:hand;
    auto ray=controller::AimFromController(ref,source,validity,100000000);
    if(!ray)return {};
    const auto origin=mode==Mode::Head?cameraCentre:
        animik::ControllerWorldFromHead(yaw,cameraCentre,head,hand).position;
    if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))return {};
    return Ray{origin,ray->direction};
}
struct Sample {
    Ray ray{};
    std::uintptr_t player=0,component=0,power=0;
    int selected=-1;
    std::uint64_t epoch=0,reference=0,generation=0,stamp=0,menu=0;
};
inline bool SameOwner(const Sample& a,const Sample& b){
    return a.player&&a.player==b.player&&a.component==b.component&&a.power&&a.power==b.power&&
        a.selected==b.selected&&a.epoch==b.epoch&&a.reference==b.reference&&
        a.generation==b.generation&&a.menu==b.menu;
}
inline bool Castable(const Sample& preview,const Sample& current,std::uint64_t now){
    return SameOwner(preview,current)&&!(current.reference&1)&&FreshSample(now,preview.stamp,100000000);
}
// A scope changes only the output of this player's getter, never cached fields.
struct Context { Sample sample{}; bool active=false; unsigned reads=0; };
inline bool Override(Context* ctx,std::uintptr_t receiver,Ray* result){
    if(!ctx||!ctx->active||!result||!ctx->sample.player||receiver!=ctx->sample.player+0x40)return false;
    *result=ctx->sample.ray;++ctx->reads;return true;
}
enum class TriggerEvent { None, Begin, Cast, Cancel };
class Trigger {
public:
    TriggerEvent Update(bool down,bool valid){
        if(!valid){const bool was=held_;Reset();return was?TriggerEvent::Cancel:TriggerEvent::None;}
        if(!down){armed_=true;if(held_){held_=false;return TriggerEvent::Cast;}return TriggerEvent::None;}
        if(armed_&&!held_){armed_=false;held_=true;return TriggerEvent::Begin;}
        return TriggerEvent::None;
    }
    void Reset(){armed_=held_=false;}
private: bool armed_=false,held_=false;
};
}
