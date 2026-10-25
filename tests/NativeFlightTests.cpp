#include "preyvr/NativeFlight.h"
#include "preyvr/StereoCamera.h"
#include <stdexcept>
#include <cmath>
#include <iostream>
using namespace preyvr;
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Driver {
    flight::Controls state;
    flight::Input in{};
    Driver(){in.valid=in.active=true;in.now=in.stamp=1000000000ull;in.sequence=in.player=in.epoch=in.generation=1;in.reference=2;}
    flight::Output Step(bool commit=true){in.stamp=in.now+=10000000;++in.sequence;auto o=state.Update(in);if(commit)state.Commit(o);return o;}
};
void Controls(){
    Driver d;d.in.up=true;Check(!d.Step().ready,"held entry cannot acquire ascent");
    d.in.up=false;Check(d.Step().ready,"neutral rearms flight");
    d.in.up=true;auto o=d.Step();Check(o.press==flight::Up&&o.move,"A ascends without stopping horizontal thrust");
    Check(d.Step().press==0,"held ascent does not repeat native press");
    d.in.down=true;o=d.Step();Check(o.release==flight::Up&&o.press==flight::Brake&&!o.move,"A+B replaces altitude with native braking");
    d.in.up=false;o=d.Step();Check(o.release==flight::Brake&&o.press==flight::Down,"leaving brake can descend after release");
    ++d.in.reference;o=d.Step();Check(!o.ready&&o.release==flight::Down,"odd reference releases owned flight");
    ++d.in.reference;d.in.down=false;Check(d.Step().ready,"even reference with neutral recovers");
    d.in.boost=true;o=d.Step();Check(o.press==flight::Boost,"Y requests native boost");
    d.in.active=false;o=d.Step(false);Check(o.release==flight::Boost,"menus/disable release boost");
    auto none=o;none.release=0;d.state.Commit(none);Check(d.Step(false).release==flight::Boost,"refused cleanup retries");
    d.in.active=true;++d.in.player;d.in.boost=false;o=d.Step(false);Check(!o.ready,"replacement waits for delivered cleanup");
    d.state.Commit(o);Check(d.Step().ready,"acknowledged cleanup and neutral rearm replacement");
    d.in.x=NAN;Check(!d.Step().ready,"bad analog cannot acquire native controls");
    Driver a;Check(a.Step().ready,"positive neutral control");a.in.boost=true;a.Step();
    a.in.now+=101000000;auto stale=a.state.Update(a.in);Check(stale.release==flight::Boost&&!stale.ready,"stalled source releases even without a new sequence");
    Driver b;b.Step();b.in.up=true;o=b.Step(false);b.in.down=true;auto newer=b.Step(false);b.state.Commit(o);
    Check(!b.state.Held(),"old acknowledgement cannot invent held input");b.state.Commit(newer);Check(b.state.Held()==flight::Brake,"current proposal can be accepted");
}
void Basis(){
    Pose head{};PoseValidity valid{true,true,true,true,0};
    auto q=flight::Steering(0,head,valid);Check(q.has_value(),"valid tracked basis");
    auto f=Rotate(*q,{0,1,0});Check(std::fabs(f.y-1)<.0001f,"OpenXR forward maps to engine +Y");
    head.orientation={std::sin(.35f),0,0,std::cos(.35f)};
    q=flight::Steering(0,head,valid);f=Rotate(*q,{0,1,0});Check(f.z>.6f,"look up tilts native forward thrust upward");
    q=flight::Steering(1.57079632679f,head,valid);f=Rotate(*q,{0,1,0});Check(f.x<-.7f&&f.z>.6f,"play yaw and pitch each applied once");
    flight::NativeQuat out{};Check(flight::OverrideRotation(out,*q),"valid native quaternion overwritten");
    const auto expected=Normalize(*q);Check(std::fabs(out.w-expected.w)<.0001f&&std::fabs(out.x-expected.x)<.0001f,"native w-first packing explicit");
    out.w=NAN;const auto old=out;Check(!flight::OverrideRotation(out,*q)&&std::isnan(out.w)&&out.x==old.x,"bad native output preserved");
    valid.orientationValid=false;Check(!flight::Steering(0,head,valid),"invalid tracking refuses basis");
}
}
int main(){try{Controls();Basis();std::cout<<"native_flight: controls, cleanup, context, freshness and 3D basis passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
