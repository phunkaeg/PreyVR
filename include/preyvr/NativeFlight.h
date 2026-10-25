#pragma once
#include "preyvr/VrMath.h"
#include <cstdint>
#include <optional>

namespace preyvr::flight {
enum : std::uint8_t { Up=1, Down=2, Boost=4, Brake=8 };
struct Input {
    bool valid=false,active=false,up=false,down=false,boost=false;
    float x=0,y=0;
    std::uint64_t now=0,stamp=0,sequence=0,player=0,epoch=0,reference=0,generation=0;
};
struct Output {
    std::uint8_t press=0,release=0;
    bool ready=false,move=false;
    std::uint64_t proposal=0;
};
// One input-drain owner. Only delivered edges become held. Context changes
// release the old owner and require neutral controls before another acquisition.
class Controls {
public:
    Output Update(const Input&);
    void Commit(const Output& accepted);
    std::uint8_t Held() const {return held_;}
private:
    Input previous_{};
    Output pending_{};
    std::uint64_t serial_=0;
    std::uint8_t held_=0;
    bool have_=false,ready_=false;
};
// Steam native quaternion output is w,x,y,z, unlike VrMath's x,y,z,w.
struct NativeQuat {float w=1,x=0,y=0,z=0;};
static_assert(sizeof(NativeQuat)==16);
std::optional<Quaternion> Steering(float playYaw,const Pose& head,const PoseValidity& validity);
bool OverrideRotation(NativeQuat& output,const Quaternion& desired);
}
