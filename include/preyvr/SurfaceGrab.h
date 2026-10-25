#pragma once
#include "preyvr/VrMath.h"
#include <cstdint>

namespace preyvr::surfacegrab {
struct Contact {
    bool valid=false;
    std::uintptr_t collider=0;
    int part=0;
    Vec3 point{},normal{};
};
struct Settings {
    float reach=.10f,pushScale=1.f,maxPush=1.5f,maxPull=1.25f;
    float correction=6.f,maxStretch=.45f,maxHandJump=.20f,maxHandSpeed=5.f,minLaunch=.15f;
    std::uint64_t maxAge=100000000ull,maxGap=60000000ull,minHold=60000000ull;
};
struct Input {
    bool valid=false,allowed=false,grip=false,busy=false;
    Vec3 localHand{},worldHand{}; // localHand is raw tracking-space metres, never world position
    Quaternion worldFromReference{};
    Contact contact{};
    std::uint64_t now=0,stamp=0,sequence=0,player=0,epoch=0,reference=0,weapon=0,generation=0;
};
struct Output {
    bool held=false,acquired=false,released=false,cancelled=false,launch=false,command=false;
    Vec3 velocity{};
};
// Single input-drain owner. Only an armed squeeze captures; invalidation never
// counts as a physical release. Motion is measured in tracking space, so a
// moving player/camera cannot create launch velocity.
class Grab {
public:
    Output Update(const Input&,const Settings& settings={});
    void Cancel();
    bool Held() const {return held_;}
    bool WantsContact(bool grip) const {return held_||(armed_&&grip);}
    Contact Anchor() const {return anchor_;}
private:
    Input previous_{};
    Contact anchor_{};
    Vec3 target_{},velocity_{};
    Quaternion capturedRotation_{};
    std::uint64_t start_=0;
    bool have_=false,held_=false,armed_=false;
};
bool Finite(Vec3);
bool ValidContact(const Contact&);
bool SameContact(const Contact&,const Contact&);
Vec3 Limit(Vec3,float maximum);
}
