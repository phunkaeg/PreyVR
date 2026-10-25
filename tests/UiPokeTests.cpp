#include "preyvr/UiPoke.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace preyvr;
using namespace preyvr::ui;
namespace {
void Check(bool value,const char* why) { if (!value) { std::cerr << why << '\n'; std::exit(1); } }
bool Near(float a,float b,float tolerance=.0001f) { return std::fabs(a-b) < tolerance; }
float Dist(Vec3 a,Vec3 b) { return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)); }
const Surface flat{{Pose{{},{0,0,-.6f}},.8f,.5f},0};
struct Driver {
    Poke poke;
    PokeInput i{true,flat,{0,0,-.57f},1000000000ull,1000000000ull,1,1,2,0};
    PokeOutput At(float u,float v,float front)
    {
        const auto p = SurfacePoint(i.surface,u,v);
        const Vec3 n = Rotate(p.orientation,{0,0,1});
        i.tip = {p.position.x+n.x*front,p.position.y+n.y*front,p.position.z+n.z*front};
        i.nowNs += 10000000ull; i.stampNs = i.nowNs; ++i.sequence;
        return poke.Update(i);
    }
};
void Projection()
{
    for (float curve : {0.f,.6f,1.2f}) {
        Surface s = flat; s.angle = curve;
        for (float u : {.02f,.25f,.5f,.75f,.98f}) for (float v : {.02f,.5f,.98f}) {
            const Pose p = SurfacePoint(s,u,v);
            const Vec3 n = Rotate(p.orientation,{0,0,1});
            for (float d : {-.02f,0.f,.03f}) {
                const auto h = ProjectPoke(s,{p.position.x+n.x*d,p.position.y+n.y*d,p.position.z+n.z*d});
                Check(h&&h->inside&&Near(h->u,u)&&Near(h->v,v)&&Near(h->frontDistance,d), "surface projection agrees with independent UV/front offsets");
                Check(Dist(h->point,p.position)<.0002f&&Dist(h->normal,n)<.0001f, "surface point and normal agree");
                const Pose room{Normalize({.1f,.3f,.05f,.9f}),{1.5f,.7f,-.3f}};
                Surface placed = s; placed.panel.pose = Compose(room,s.panel.pose);
                const Pose finger = Compose(room,Pose{{},{p.position.x+n.x*d,p.position.y+n.y*d,p.position.z+n.z*d}});
                const auto shifted = ProjectPoke(placed,finger.position);
                Check(shifted&&Near(shifted->u,u)&&Near(shifted->v,v)&&Near(shifted->frontDistance,d), "projection invariant under rigid room transform");
            }
        }
    }
    auto h = ProjectPoke(flat,{.5f,0,-.6f});
    Check(h&&!h->inside&&h->u>1, "outside UV is not clamped");
    Surface invalid = flat; invalid.panel.width = 0;
    Check(!ProjectPoke(invalid,{}), "degenerate surface refused");
    invalid = flat; invalid.angle = 1.3f;
    Check(!ProjectPoke(invalid,{}), "unsupported curvature refused");
    Check(!ProjectPoke(flat,{NAN,0,0}), "nonfinite finger refused");
}
void Gesture()
{
    for (float curve : {0.f,.6f}) {
        Driver d; d.i.surface.angle = curve;
        auto o = d.At(.5f,.5f,.03f);
        Check(o.active&&o.inside&&!o.pressed, "front hover is neutral active sample");
        o = d.At(.5f,.5f,-.004f); Check(o.active&&o.pressed, "fresh front crossing presses");
        PointerButtons buttons; buttons.Update(true,true,false);
        Check(buttons.Update(o.active,o.inside,o.pressed)==1, "poke state feeds pointer press");
        const auto duplicate = d.poke.Update(d.i);
        Check(duplicate.pressed&&buttons.Update(duplicate.active,duplicate.inside,duplicate.pressed)==0, "duplicate held sample retains without repeat edge");
        o = d.At(.57f,.5f,-.005f); Check(o.active&&o.pressed&&o.contact->u>.56f, "pressed lateral motion drags");
        o = d.At(.57f,.5f,.005f); Check(o.pressed, "shallow withdrawal does not chatter");
        o = d.At(.57f,.5f,.02f); Check(o.active&&!o.pressed, "withdrawal threshold releases");
        Check(buttons.Update(o.active,o.inside,o.pressed)==-1, "poke state feeds pointer release");
        Check(d.At(.57f,.5f,-.004f).pressed, "withdrawal rearms next press");
    }
    Driver back;
    Check(!back.At(.5f,.5f,-.004f).pressed&&!back.At(.5f,.5f,-.01f).pressed, "entry from behind cannot press");
    Check(!back.At(.5f,.5f,.02f).pressed&&back.At(.5f,.5f,-.004f).pressed, "back entry needs deliberate withdrawal");
    Driver outside; outside.At(.95f,.5f,.03f); outside.At(.95f,.5f,-.004f);
    auto o = outside.At(1.05f,.5f,-.005f);
    Check(!o.active&&!o.inside&&!o.pressed&&o.contact&&o.contact->u>1, "leaving panel releases without edge clamping");
    Check(!outside.At(.95f,.5f,-.005f).pressed, "reentering while penetrated cannot press");
    Driver far; Check(!far.At(.5f,.5f,.2f).active, "far finger leaves ray available");
    far.At(.5f,.5f,.03f); far.At(.5f,.5f,-.004f);
    Check(!far.At(.5f,.5f,-.08f).active, "excessive penetration cancels");
    Check(!far.At(.5f,.5f,-.003f).pressed, "overpenetration needs withdrawal before rearm");
}
void Discontinuities()
{
    for (int change = 0; change < 3; ++change) {
        Driver d; d.At(.5f,.5f,.03f); d.At(.5f,.5f,-.004f);
        if(change==0)++d.i.epoch; if(change==1)d.i.reference+=2; if(change==2)++d.i.generation;
        Check(!d.At(.5f,.5f,-.005f).pressed, "context change releases held poke");
        Check(!d.At(.5f,.5f,-.006f).pressed, "context change requires withdrawal");
    }
    Driver lost; lost.At(.5f,.5f,.03f); lost.At(.5f,.5f,-.004f);
    lost.i.valid = false; Check(!lost.poke.Update(lost.i).active, "tracking loss cancels");
    lost.i.valid = true; Check(!lost.At(.5f,.5f,-.005f).pressed, "tracking recovery behind surface cannot press");
    Driver stale; stale.At(.5f,.5f,.03f); stale.At(.5f,.5f,-.004f);
    stale.i.nowNs += 201000000ull;
    Check(!stale.poke.Update(stale.i).active, "even duplicate publication expires");
    Driver jump; jump.At(.5f,.5f,.03f);
    Check(!jump.At(.5f,.5f,-.13f).pressed, "large finger discontinuity does not click");
    Driver gap; gap.At(.5f,.5f,.03f); gap.i.nowNs += 300000000ull;
    Check(!gap.At(.5f,.5f,-.004f).pressed, "large sample gap cannot bridge a press");
    Driver clock; clock.At(.5f,.5f,.03f); clock.At(.5f,.5f,-.004f); clock.i.nowNs = clock.i.stampNs-1;
    Check(!clock.poke.Update(clock.i).active, "future timestamp refused");
}
void MovingSurface()
{
    Driver d; d.At(.5f,.5f,.025f);
    // Move the panel over the fixed finger in small per-frame steps, so the
    // pose discontinuity threshold alone cannot explain refusal.
    const Vec3 fixed = d.i.tip;
    for (int n=0;n<5;++n) {
        d.i.surface.panel.pose.position.z += .007f;
        d.i.tip = fixed; d.i.nowNs += 10000000ull; d.i.stampNs=d.i.nowNs; ++d.i.sequence;
        Check(!d.poke.Update(d.i).pressed, "moving panel over stationary finger cannot click");
    }
    for (int change=0;change<4;++change) {
        Driver held; held.At(.5f,.5f,.03f); held.At(.5f,.5f,-.004f);
        if(change==0)held.i.surface.panel.width+=.02f;
        if(change==1)held.i.surface.panel.height+=.02f;
        if(change==2)held.i.surface.angle=.3f;
        if(change==3)held.i.surface.panel.pose.orientation=Normalize({0,.1f,0,1});
        held.i.nowNs+=10000000ull;held.i.stampNs=held.i.nowNs;++held.i.sequence;
        Check(!held.poke.Update(held.i).pressed, "presentation resize/curve/rotation cancels capture");
    }
    Driver sign; sign.At(.5f,.5f,.03f); sign.At(.5f,.5f,-.004f);
    sign.i.surface.panel.pose.orientation={0,0,0,-1};
    sign.i.nowNs+=10000000ull;sign.i.stampNs=sign.i.nowNs;++sign.i.sequence;
    Check(sign.poke.Update(sign.i).pressed, "quaternion sign equivalence does not cancel capture");
}
}
int main()
{
    Projection(); Gesture(); Discontinuities(); MovingSurface();
    std::cout << "ui_poke: geometry, crossing, capture, lifecycle and moving-surface controls passed\n";
}
