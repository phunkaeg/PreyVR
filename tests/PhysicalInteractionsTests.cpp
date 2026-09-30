#include "preyvr/PhysicalInteractions.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;using namespace preyvr::physical;
void Check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
struct Driver {
    Swing swing;Sample s{};
    Driver(){s.owner=1;s.reference=2;s.epoch=1;s.sequence=1;s.stamp=1000000000;s.points=Bounds({-.05f,0,-.05f},{.05f,.6f,.05f});swing.Update(s,1.2f);}
    Motion Tick(float dx=0){++s.sequence;s.stamp+=10000000;s.grip.position.x+=dx;for(auto& p:s.points)p.x+=dx;return swing.Update(s,1.2f);}
    void Rest(int n=40){for(int i=0;i<n;++i)Tick();}
};
int main(){
    Driver d;Check(!d.Tick(.03f).query,"equipping during a swing cannot strike");d.Rest();
    Check(!d.Tick(.025f).strike,"short twitch below minimum travel");d.Tick(.025f);d.Tick(.025f);
    auto m=d.Tick(.025f);Check(m.strike&&m.query,"qualified physical swing");
    Check(!d.swing.Update(d.s,1.2f).query,"same tracking publication cannot be consumed twice");
    d.swing.Contact();for(int i=0;i<20;++i)Check(!d.Tick(.025f).query,"one contact per swing");
    d.Rest();for(int i=0;i<4;++i)m=d.Tick(.025f);Check(m.strike,"rest rearms next swing");
    d.s.owner++;Check(!d.Tick(.025f).query,"equip generation resets");d.Rest();
    d.s.reference+=2;Check(!d.Tick(.025f).query,"recenter resets");d.Rest();
    d.s.epoch++;Check(!d.Tick(.025f).query,"tracking epoch resets");d.Rest();
    Check(!d.Tick(1).query,"controller teleport rejected");Check(!d.Tick(.03f).query,"teleport requires fresh rest");
    d.Rest();d.s.stamp+=200000000;Check(!d.Tick(.03f).query,"stale sequence gap rejected");
    d.Rest();d.s.worldFromReference={{0,0,.70710678f,.70710678f},{100,25,4}};
    Check(!d.Tick().query,"walking and snap turn alone produce no contact");
    m=d.Tick(.03f);Check(m.query,"physical movement after turn still works");
    Check(std::fabs(m.to[0].x-m.from[0].x)<.0001f&&std::fabs(m.to[0].y-m.from[0].y-.03f)<.0001f,"both segment endpoints use current world transform");
    Driver idle;idle.Rest();for(auto& p:idle.s.points)p.x+=.04f;Check(!idle.Tick().query,"idle animation cannot supply physical intent");
    Driver arc;arc.Rest();const auto original=arc.s.points;
    for(int i=1;i<=5;++i){const float angle=i*.04f;arc.s.grip.orientation={0,0,std::sin(angle/2),std::cos(angle/2)};
        for(unsigned j=0;j<original.size();++j)arc.s.points[j]=Rotate(arc.s.grip.orientation,original[j]);m=arc.Tick();}
    Check(m.strike,"rotation-only wrist swing moves weapon tip");
    Contact contact{};Motion sweep{};sweep.dt=.01f;sweep.speed=2;sweep.from.fill({0,0,0});sweep.to.fill({.02f,0,0});
    int rays=0;Check(Sweep(sweep,[&](Vec3,Vec3 delta,scene::Hit& h){++rays;h.distance=rays==2?.005f:.01f;h.collider=reinterpret_cast<void*>(std::uintptr_t(rays));h.point={h.distance,0,0};h.normal={-1,0,0};return delta.x==.02f;},contact),"complete sweep");
    Check(rays==9&&contact.found&&contact.hit.collider==reinterpret_cast<void*>(2)&&std::fabs(contact.fraction-.25f)<1e-6f,"earliest contact across all bounds samples wins");
    rays=0;Check(!Sweep(sweep,[&](Vec3,Vec3,scene::Hit& h){h=contact.hit;return ++rays<3;},contact)&&!contact.found,"partial query refuses all hits");
    Check(Sweep(sweep,[](Vec3,Vec3,scene::Hit& h){h.distance=-1;return true;},contact)&&!contact.found,"miss clears previous hit");
    contact.direction={1,0,0};contact.speed=100;contact.hit.point={2,3,4};contact.hit.partId=7;
    auto action=Nudge(contact,100);Check(action.type==2&&action.impulse.x==1.5f&&action.angular.x==0&&action.point.z==4&&action.partId==7&&action.applyTime==2&&action.source==2,"bounded native impulse, exact offsets and native wake source");
    Check(Nudge(contact,0).impulse.x==0&&Nudge(contact,50).impulse.x==.75f,"strength and off scaling");
    RigidResponse tiny{};tiny.inverseMass=100;tiny.centre=contact.hit.point;tiny.inverseInertia={1,0,0,0,1,0,0,0,1};
    Check(LimitNudge(action,tiny)&&std::fabs(action.impulse.x-.015f)<1e-6f,"tiny object's induced linear speed capped at 1.5 m/s");
    action=Nudge(contact,100);action.point={0,1,0};tiny.centre={};tiny.inverseMass=1;tiny.inverseInertia[8]=100;
    Check(LimitNudge(action,tiny)&&std::fabs(action.impulse.x-.03f)<1e-6f,"off-centre contact induced spin capped at 3 rad/s");
    tiny.inverseMass=0;Check(!LimitNudge(action,tiny),"infinite-mass rigid object receives no nudge");
    HitOwnership hit;hit.Claim(42);Check(hit.Suppress(42)&&!hit.Suppress(43),"only owned weapon suppressed");
    Check(hit.BlockAttack(42,1,1)&&!hit.BlockAttack(42,1,2),"physical animation blocks extra attack but allows release");
    Check(hit.Suppress(42)&&hit.Suppress(42),"all duplicate delayed events suppressed");
    Check(!hit.BlockAttack(42,0,1)&&!hit.Suppress(42),"native idle attack restores button path");
    Check(hit.Claim(42)&&hit.Claim(43)&&hit.Suppress(42)&&hit.Suppress(43),"new weapon cannot release old delayed hit ownership");
    for(std::uintptr_t w=44;w<58;++w)Check(hit.Claim(w),"bounded ownership slots");
    Check(!hit.Claim(58)&&hit.Suppress(42),"full ownership registry refuses rather than evicts delayed hits");
    hit.Release(43);Check(hit.Claim(58)&&hit.Suppress(42),"released idle slot is reusable");
    scene::Params params{};scene::Hit h{};void* skip[]={reinterpret_cast<void*>(1)};
    Check(scene::Build(params,{}, {1,0,0},h,skip,1,.125f)&&params.delta.x==.125f,"short query is not the reticle's 200m ray");
    Check(!scene::Build(params,{}, {1,0,0},h,skip,1,std::numeric_limits<float>::quiet_NaN()),"invalid range rejected");
    std::cout<<"Physical motion, ownership, short sweeps and impulse contracts passed\n";
}
