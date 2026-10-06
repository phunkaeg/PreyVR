#include "preyvr/TwoHandedAim.h"
#include "preyvr/MotionController.h"
#include "preyvr/WeaponRigAlignment.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
using namespace preyvr::twohand;
void Check(bool value,const char* message) { if(!value) { std::cerr<<message<<'\n';std::exit(1); } }
bool Near(Vec3 a,Vec3 b,float epsilon=.002f) { return std::fabs(a.x-b.x)<epsilon&&std::fabs(a.y-b.y)<epsilon&&std::fabs(a.z-b.z)<epsilon; }
Input Base() { Input i{};i.owner=1;i.epoch=1;i.reference=2;i.usable=true;i.dt=.01f;
    i.region.start={0,.25f,0};i.region.end={0,.45f,0};i.support.position={0,.35f,0};return i; }
Output Hold(Solver& s,Input& i) { s.Update(i);i.squeeze=1; Output out{};for(int n=0;n<60;++n)out=s.Update(i);return out; }
int main() {
    {
        auto toggle=Base();toggle.toggleGrip=true;Solver solver;
        Check(Hold(solver,toggle).held,"toggle grip acquires");
        toggle.squeeze=0;Check(solver.Update(toggle).held,"toggle remains attached after release");
        toggle.squeeze=1;Check(!solver.Update(toggle).held,"second squeeze detaches");
        Check(!solver.Update(toggle).held,"held detach cannot immediately reacquire");
        toggle.squeeze=0;solver.Update(toggle);toggle.squeeze=1;Check(solver.Update(toggle).held,"fresh squeeze reacquires");
        toggle.owner++;Check(!solver.Update(toggle).held,"weapon switch clears toggle");
        Check(!solver.Update(toggle).held,"switch with held grip cannot latch");
        toggle.squeeze=0;solver.Update(toggle);toggle.squeeze=1;Check(solver.Update(toggle).held,"new weapon fresh grip");
        toggle.toggleGrip=false;Check(!solver.Update(toggle).held,"switching grip preference releases");
        Check(!solver.Update(toggle).held,"mode switch needs release");
        toggle.usable=false;Check(!solver.Update(toggle).held,"disabled resets");
    }
    auto i=Base();Solver s;auto out=Hold(s,i);
    Check(out.held&&out.blend>.99f,"grip enters in region");
    Check(std::fabs(out.socket.y-.35f)<.001f,"nearest point locks within capsule");
    i.support.position={.35f,0,0};out=s.Update(i);
    Check(out.held,"leaving acquisition region does not release latch");
    Check(Near(Rotate(out.orientation,{0,1,0}),{1,0,0}),"raw offhand steers barrel");
    // Feed solved orientation through the real wrist alignment contract.
    weaponrig::Basis basis{};basis.source=weaponrig::Source::jointBind;
    basis.weaponInWrist=Normalize({.2f,.1f,.3f,.9f});basis.barrelInWeapon=Normalize({.1f,.3f,.1f,.8f});
    const Quaternion character=Normalize({.1f,.2f,.4f,.9f});Quaternion wrist{};
    Check(weaponrig::SolveWrist(character,out.orientation,basis,wrist),"wrist solve accepts two-hand aim");
    Check(Near(Rotate(Multiply(Multiply(Multiply(character,wrist),basis.weaponInWrist),basis.barrelInWeapon),{0,1,0}),
               Rotate(out.orientation,{0,1,0})),"model barrel and firing ray share solved direction");
    i.squeeze=0;out=s.Update(i);Check(!out.held&&out.blend>0,"release blends instead of snapping");
    for(int n=0;n<60;++n)out=s.Update(i);
    Check(Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"release restores one-hand aim");
    i=Base();s={};i.support.position={1,.35f,0};out=Hold(s,i);Check(!out.held,"empty-air squeeze cannot acquire");
    i.support.position={0,.35f,0};Check(!s.Update(i).held,"held failed squeeze cannot acquire by drifting in");
    i.squeeze=0;s.Update(i);i.squeeze=1;Check(s.Update(i).held,"fresh squeeze can acquire");
    i.owner=2;Check(!s.Update(i).held,"weapon switch clears hold");Check(!s.Update(i).held,"new weapon requires release");
    i.squeeze=0;s.Update(i);i.squeeze=.7f;Check(s.Update(i).held,"new owner reacquires on squeeze");
    i.squeeze=.5f;Check(s.Update(i).held,"squeeze hysteresis holds at half travel");
    i.reference+=2;Check(!s.Update(i).held,"recenter invalidates hold");
    i=Base();s={};Hold(s,i);i.usable=false;out=s.Update(i);Check(!out.held&&out.blend==0,"tracking/modal loss resets immediately");
    i.usable=true;Check(!s.Update(i).held,"held reacquired controller cannot auto-grab");
    i=Base();s={};Hold(s,i);i.epoch++;Check(!s.Update(i).held,"session epoch invalidates hold");
    i=Base();s={};Hold(s,i);i.support.position={0,0,.35f};out=s.Update(i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,0,1}),"vertical aiming has no up-vector singularity");
    i.support.position={0,-.35f,0};out=s.Update(i);Check(Near(Rotate(out.orientation,{0,1,0}),{0,-1,0}),"180 degree turn is finite");
    i.support.position={0,.01f,0};out=s.Update(i);Check(!out.held&&out.blend==0,"near-coincident hands cannot spin weapon");
    i=Base();s={};i.region.start=i.region.end={0,.05f,0};i.support.position={0,.05f,0};Check(!Hold(s,i).held,"short adjacent grips are excluded");
    i=Base();s={};i.visualPrimaryOffset={.1f,0,0};i.support.position={.1f,.35f,0};out=Hold(s,i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"acquisition and steering account for visible wrist reach compression");
    i=Base();s={};i.region.start=i.region.end={.06f,.35f,-.03f};i.support.position=i.region.start;out=Hold(s,i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"authored off-axis socket does not introduce aim offset on grab");
    i=Base();s={};Hold(s,i);i.squeeze=std::numeric_limits<float>::quiet_NaN();Check(!s.Update(i).held,"NaN squeeze refuses");
    i=Base();s={};Hold(s,i);i.support.position.x=std::numeric_limits<float>::infinity();Check(!s.Update(i).held,"nonfinite tracking refuses");
    i=Base();s={};Hold(s,i);i.dt=1;Check(!s.Update(i).held,"long stall resets smoothing");
    {
        // Q-Beam samples, metres in the barrel frame (2026-10-07).
        RegionLatch latch;Vec3 socket{};const Vec3 rest{.375f,.616f,.198f};
        Check(!latch.Update(5,true,rest,socket),"one sample does not latch");
        for(int n=0;n<RegionLatch::kSettleSamples-2;++n) Check(!latch.Update(5,true,{rest.x+(n%2)*.002f,rest.y,rest.z},socket),"settling");
        Check(latch.Update(5,true,rest,socket)&&Near(socket,rest),"a resting palm latches");
        Check(latch.Update(5,false,{},socket)&&Near(socket,rest),"out-of-bounds palm (firing animation) keeps the region");
        for(int n=0;n<200;++n) latch.Update(5,true,{.129f,.473f,.096f},socket);
        Check(Near(socket,rest),"an animated palm, even resting elsewhere, never moves a latched region");
        Check(!latch.Update(6,true,{.1f,.3f,0},socket),"re-equip starts over");
        Check(!latch.Update(0,true,rest,socket),"no owner, no region");
        RegionLatch moving;
        for(int n=0;n<100;++n) Check(!moving.Update(5,true,{.3f+n*.02f,.6f,.2f},socket),"an equip animation never settles");
        moving.Update(5,true,{std::numeric_limits<float>::quiet_NaN(),0,0},socket);
        for(int n=0;n<RegionLatch::kSettleSamples;++n) moving.Update(5,true,rest,socket);
        Check(moving.Latched()&&Near(socket,rest),"then latches once it rests");
    }
    {
        LongReach reach{};
        const Vec3 qbeam{-.355f,.576f,-.233f},gloo{-.103f,.424f,.013f};   // measured 2026-10-07
        Check(std::fabs(HoldBack(qbeam,reach)-.126f)<.001f,"Q-Beam foregrip comes back to 0.45 m ahead");
        Check(HoldBack(gloo,reach)==0,"a foregrip within reach is untouched");
        Check(HoldBack({0,1.5f,0},reach)==reach.maxHoldBack,"hold-back is bounded");
        Check(HoldBack({0,std::numeric_limits<float>::quiet_NaN(),0},reach)==0,"nonfinite socket refuses");
        const Region far=SupportRegion(qbeam,.126f,reach),near=SupportRegion(gloo,0,reach);
        Check(Near(far.start,{qbeam.x,qbeam.y-.14f,qbeam.z})&&Near(far.end,{qbeam.x,qbeam.y+.04f,qbeam.z}),"long weapon grabs behind its foregrip");
        Check(Near(near.start,{gloo.x,gloo.y-.04f,gloo.z})&&Near(near.end,{gloo.x,gloo.y+.04f,gloo.z}),"other weapons keep the native region");
        // A grab 12 cm behind the foregrip, inside the radius, latches there: steering starts without a jump.
        Input g=Base();g.region=far;g.support.position={qbeam.x,qbeam.y-.12f,qbeam.z};Solver s2;auto o=Hold(s2,g);
        Check(o.held&&Near(o.socket,g.support.position)&&Near(Rotate(o.orientation,{0,1,0}),{0,1,0}),"behind-foregrip grab holds without an aim jump");
    }
    std::cout<<"Two-handed aim geometry, lifecycle and barrel integration passed\n";
}
