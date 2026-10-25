#include "preyvr/SurfaceGrab.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace preyvr;
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Driver {
    surfacegrab::Grab grab;surfacegrab::Input i{};surfacegrab::Settings s{};
    Driver(){i.valid=i.allowed=true;i.now=i.stamp=1000000000;i.sequence=i.player=i.epoch=i.weapon=i.generation=1;i.reference=2;
        i.localHand=i.worldHand={.05f,0,0};i.contact={true,0x2000,3,{0,0,0},{1,0,0}};}
    surfacegrab::Output Step(float dx=0){i.now=i.stamp+=10000000;++i.sequence;i.localHand.x+=dx;i.worldHand.x+=dx;return grab.Update(i,s);}
    void Capture(){Check(!Step().held,"neutral arms");i.grip=true;Check(Step().acquired,"fresh squeeze captures");}
    void Push(){for(int n=0;n<8;++n)Check(Step(-.01f).held,"held physical motion");}
};
void Acquisition(){
    Driver d;d.i.grip=true;Check(!d.Step().held,"held entry cannot acquire");d.i.grip=false;d.Step();d.i.grip=true;
    d.i.contact.valid=false;Check(!d.Step().held,"miss cannot capture");d.i.contact.valid=true;Check(!d.Step().held,"held miss cannot acquire late");
    d.i.grip=false;d.Step();d.i.grip=true;d.i.contact.normal={-1,0,0};Check(!d.Step().held,"backface cannot acquire");
    Driver far;far.i.worldHand.x=.2f;far.i.grip=false;far.Step();far.i.grip=true;Check(!far.Step().held,"reach enforced");
    Driver d2;d2.Capture();Check(d2.Step().velocity.x==0,"stationary anchor has no initial snap");
    d2.i.grip=false;auto out=d2.Step();Check(out.released&&!out.launch&&!out.command,"short hold never pushes");
}
void Physics(){
    Driver d;d.Capture();auto pull=d.Step(-.01f);Check(pull.velocity.x>0&&pull.velocity.x<=d.s.maxPull,"hand toward wall pulls body outward");
    for(int n=0;n<8;++n)d.Step(-.01f);d.i.grip=false;auto push=d.Step(-.01f);
    Check(push.launch&&push.command&&push.velocity.x>.8f&&push.velocity.x<=d.s.maxPush,"fresh physical release pushes outward in metres per second");
    Check(!d.Step().command,"pure release never refires");
    Driver inward;inward.Capture();for(int n=0;n<8;++n)inward.Step(.01f);inward.i.grip=false;
    Check(!inward.Step(.01f).launch,"release does not launch into surface");
    Driver cap;cap.s.maxPush=.5f;cap.Capture();cap.Push();cap.i.grip=false;Check(cap.Step(-.01f).velocity.x<=.50001f,"push capped");
    Driver zero;zero.s.pushScale=0;zero.Capture();zero.Push();zero.i.grip=false;Check(!zero.Step(-.01f).launch,"zero strength disables push");
    Driver tangent;tangent.Capture();for(int n=0;n<8;++n){tangent.i.localHand.y-=.01f;tangent.i.worldHand.y-=.01f;tangent.Step();}
    tangent.i.grip=false;auto t=tangent.Step();Check(t.launch&&t.velocity.y>.6f&&t.velocity.x==0,"tangential push preserved after a stationary release sample");
    Driver rotated;rotated.i.worldFromReference={0,0,std::sin(.4f),std::cos(.4f)};rotated.Capture();rotated.Push();rotated.i.grip=false;
    auto r=rotated.Step(-.01f);Check(r.launch&&r.velocity.x>.4f&&r.velocity.y>.5f,"tracking velocity rotates to world once");
}
void Provenance(){
    Driver d;d.Capture();for(int n=0;n<8;++n){d.i.worldHand.x+=.01f;d.Step();}d.i.grip=false;
    Check(!d.Step().launch,"body/camera movement with stationary raw hand cannot manufacture release velocity");
    Driver dup;dup.Capture();dup.Step(-.01f);auto expected=dup.grab.Update(dup.i,dup.s);
    for(int n=0;n<10;++n){auto out=dup.grab.Update(dup.i,dup.s);Check(std::fabs(out.velocity.x-expected.velocity.x)<1e-6f,"duplicate sample cannot double-count physical motion");}
    dup.i.localHand.x+=.01f;Check(dup.grab.Update(dup.i,dup.s).cancelled,"changed pose with duplicate sequence cancels");
    for(int mode=0;mode<14;++mode){Driver a;a.Capture();a.Push();
        switch(mode){case 0:a.i.valid=false;break;case 1:a.i.allowed=false;break;case 2:a.i.busy=true;break;
        case 3:++a.i.player;break;case 4:++a.i.epoch;break;case 5:a.i.reference+=2;break;case 6:++a.i.weapon;break;
        case 7:++a.i.generation;break;case 8:a.i.contact.collider+=8;break;case 9:a.i.contact.point.x+=.04f;break;
        case 10:a.i.worldHand.x+=.6f;break;case 11:a.i.localHand.x+=.3f;break;case 12:a.i.worldFromReference={0,0,.2f,.98f};break;
        case 13:a.i.localHand.x=NAN;break;}
        a.i.grip=false;const auto out=a.Step();Check(out.cancelled&&!out.launch&&!out.command,"invalid context cancels without push");
        a.i.grip=true;Check(!a.Step().command,"cancel requires neutral before recapture");
    }
    Driver stale;stale.Capture();stale.i.now+=101000000;auto out=stale.grab.Update(stale.i,stale.s);Check(out.cancelled&&!out.launch,"stale sample cancels");
    Driver gap;gap.Capture();gap.i.stamp+=61000000;gap.i.now=gap.i.stamp;++gap.i.sequence;Check(gap.grab.Update(gap.i,gap.s).cancelled,"sample gap cancels");
    Driver odd;odd.Capture();++odd.i.reference;Check(odd.Step().cancelled,"recenter in progress cancels");
    Driver speed;speed.Capture();Check(speed.Step(.06f).cancelled,"implausible physical hand speed cancels");
}
}
int main(){try{Acquisition();Physics();Provenance();std::cout<<"surface_grab: acquisition, physical velocity, limits and cancellation passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
