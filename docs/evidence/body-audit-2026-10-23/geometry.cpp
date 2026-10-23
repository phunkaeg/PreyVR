#include "preyvr/BodyPose.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace preyvr;using namespace preyvr::bodypose;
float Distance(Vec3 a,Vec3 b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
Skeleton Rest(){
 Skeleton s{};s.hip={0,0,.9f};s.spine[0]={0,0,1};s.spine[1]={0,0,1.15f};s.spine[2]={0,0,1.3f};s.spine[3]={0,0,1.4f};
 s.neck={0,0,1.5f};s.head={0,0,1.65f};s.legs=true;
 for(int k=0;k<2;++k){const float x=k==0?.1f:-.1f;s.thigh[k]={x,0,.9f};s.knee[k]={x,.08f,.5f};s.foot[k]={x,0,.1f};}
 return s;
}
int main(){try{
 const auto s=Rest();const auto rest=Solve(s,Goal{0,s.head,1});if(!rest.valid||rest.miss>.001f)throw std::runtime_error("rest control failed");
 FootPlanter feet;Vec3 natural[2]{s.foot[0],s.foot[1]},thigh[2]{s.thigh[0],s.thigh[1]},placed[2]{};
 float leg[2]{};for(int k=0;k<2;++k)leg[k]=Distance(s.thigh[k],s.knee[k])+Distance(s.knee[k],s.foot[k]);
 feet.Update(natural,thigh,leg,0,.014f,false,{},placed);
 for(int k=0;k<2;++k){natural[k].x+=.9f;thigh[k].x+=.9f;}
 // A 0.9m world-position discontinuity is below the planter's 1m reset.
 // Horizontal speed does not free the feet in PlanBody, and no new owner resets it.
 feet.Update(natural,thigh,leg,64,.014f,false,{},placed);
 Goal g{0,s.head,1};for(int k=0;k<2;++k){g.footSet[k]=true;g.foot[k]=placed[k];g.foot[k].x-=.9f;}
 const auto p=Solve(s,g);
 const float gap=Distance(Apply(p.shin[0],s.foot[0]),Apply(p.foot[0],s.foot[0]));
 std::cout<<"submetre_discontinuity_plan_valid="<<p.valid<<" shin_to_foot_gap_m="<<gap<<" both_feet_stepping="<<(feet.Stepping(0)&&feet.Stepping(1))<<'\n';
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
