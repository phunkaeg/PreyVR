#include "preyvr/UiPanel.h"
#include <cstdlib>
#include <cmath>
#include <iostream>
using namespace preyvr;
void Require(bool b,const char* why) { if(!b){std::cerr<<why<<'\n';std::exit(1);} }
int main() {
    const auto near=[](float a,float b){return std::abs(a-b)<.0001f;};
    const ui::Surface flat{{Pose{{},{0,0,-2}},2,1},0};
    auto hit=ui::Intersect(flat,Pose{{},{.4f,.2f,0}});
    Require(hit&&hit->inside&&near(hit->u,.7f)&&near(hit->v,.3f)&&near(hit->distance,2),"translated ray maps to exact screen pixels");
    Require(!ui::Intersect(flat,Pose{{0,1,0,0},{}}),"ray pointing away cannot select");
    Require(!ui::Intersect(flat,Pose{{},{0,0,-3}}),"back face rejected");
    Require(!ui::Intersect(flat,Pose{{0,.70710678f,0,.70710678f},{}}),"parallel ray rejected");
    hit=ui::Intersect(flat,Pose{{},{1.2f,0,0}});
    Require(hit&&!hit->inside&&hit->u>1,"dragging offscreen must not clamp onto a button");
    const ui::Surface curved{flat.panel,.6f};
    const auto axis=ui::CylinderAxis(curved);
    Require(near(axis.position.z,2/.6f-2),"cylinder axis differs from visible centre by radius");
    Require(near(ui::SurfacePoint(curved,.5f,.5f).position.z,-2),"curvature preserves centre distance");
    for(float u:{.02f,.25f,.5f,.75f,.98f})for(float v:{.02f,.5f,.98f}) {
        const float theta=(u-.5f)*.6f,r=2/.6f;
        const Vec3 target{r*std::sin(theta),.5f-v,r-2-r*std::cos(theta)};
        const Vec3 origin{.25f,-.15f,.1f};
        Vec3 d{target.x-origin.x,target.y-origin.y,target.z-origin.z};
        const float len=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);d={d.x/len,d.y/len,d.z/len};
        Pose ray{Normalize({d.y,-d.x,0,1-d.z}),origin};
        auto h=ui::Intersect(curved,ray);
        Require(h&&h->inside&&near(h->u,u)&&near(h->v,v),"curved intersection agrees with independent surface points");
        const Pose room{{0,.4f,0,std::sqrt(.84f)},{10,1,-3}};
        auto transformed=curved;transformed.panel.pose=Compose(room,curved.panel.pose);
        h=ui::Intersect(transformed,Compose(room,ray));
        Require(h&&near(h->u,u)&&near(h->v,v),"recenter and room transforms preserve curved UVs");
    }
    ui::PointerButtons buttons;
    Require(buttons.Update(true,true,true)==0,"held trigger on menu entry cannot click");
    Require(buttons.Update(true,true,false)==0,"release arms pointer");
    Require(buttons.Update(true,true,true)==1,"fresh squeeze presses");
    Require(buttons.Update(true,false,true)==0&&buttons.down,"drag remains captured beyond panel");
    Require(buttons.Update(false,false,true)==-1&&!buttons.down,"focus loss releases capture");
    Require(buttons.Update(true,true,true)==0,"focus regain while held cannot click");
    buttons.Update(true,false,false);
    Require(buttons.Update(true,false,true)==0,"press outside never captures");
    Require(buttons.Update(true,true,true)==0,"held ray entering button never presses");
    buttons.Update(true,true,false);
    Require(buttons.Update(true,true,true)==1&&buttons.Update(true,true,false)==-1,"ordinary press/release");
    for(float angle:{-.9f,-.2f,0.f,.3f,.9f}) {
        const auto f=ui::PanelFraction(std::tan(angle),.1f,2.3f,1.3f,2);
        Require(f.has_value(),"valid HUD projection");
        const Vec3 point{((*f)[0]-.5f)*2.3f,(.5f-(*f)[1])*1.3f,-2};
        Require(std::abs(std::atan2(point.x,-point.z)-angle)<.0001f,"HUD pixel reconstructs original aim direction");
    }
    const auto outside=ui::PanelFraction(2,0,2,1,2);
    Require(outside && (*outside)[0]>1,"off-panel aim must leave the texture, not label its edge");
    Require(!ui::PanelFraction(0,0,0,1,2),"degenerate HUD plane rejected");
    std::array<ui::Eye,2> eyes{{{Pose{{},{-.032f,0,0}},-.95f,.75f,.8f,-.85f},
                              {Pose{{},{.032f,0,0}},-.75f,.95f,.8f,-.85f}}};
    for(float aspect : {.5f,1.f,16.f/9,3440.f/1440,4.f}) {
        const auto panel=ui::FitPanel({},eyes,aspect);
        Require(panel && ui::CornersVisible(*panel,eyes),"both eyes must contain every corner");
        Require(std::abs(panel->width/panel->height-aspect)<.0001f,"no aspect distortion");
        auto s=ui::Surface{*panel,.6f};
        for(int n=0;n<80&&!ui::SurfaceVisible(s,eyes);++n){s.panel.width*=.95f;s.panel.height*=.95f;}
        Require(ui::SurfaceVisible(s,eyes),"curved canvas fits both optical frusta");
    }
    // A narrow asymmetric/canted runtime must shrink the panel, never crop it.
    auto invalidSurface=curved;
    invalidSurface.panel.width=std::nanf("");
    Require(!ui::SurfaceVisible(invalidSurface,eyes)&&!ui::Intersect(invalidSurface,{}),"non-finite surface rejected");
    invalidSurface=curved;invalidSurface.panel.height=INFINITY;
    Require(!ui::SurfaceVisible(invalidSurface,eyes)&&!ui::Intersect(invalidSurface,{}),"infinite surface rejected");
    eyes[0].right=.2f; eyes[1].left=-.23f;
    eyes[0].pose.orientation={0,.05f,0,std::sqrt(1-.05f*.05f)};
    auto narrow=ui::FitPanel({},eyes,16.f/9);
    Require(narrow && ui::CornersVisible(*narrow,eyes),"canted narrow frusta");
    const Pose room{{0,.4f,0,std::sqrt(.84f)},{10,1,-3}};
    for(auto& eye:eyes) eye.pose=Compose(room,eye.pose);
    auto moved=ui::FitPanel(room,eyes,16.f/9);
    Require(moved && ui::CornersVisible(*moved,eyes),"invariant under common room transform");
    eyes[0].left=std::nanf("");
    Require(!ui::FitPanel({},eyes,1),"reject bad FOV");
    Require(!ui::FitPanel({},eyes,0),"reject zero aspect");
    // --- the wearer's scale dial ------------------------------------------
    // A clean pair of eyes: the ones above were deliberately damaged by the
    // rejection cases, so reusing them would test the wrong thing.
    std::array<ui::Eye,2> plain{};
    for(int e=0;e<2;++e){plain[e].pose={{0,0,0,1},{e?.032f:-.032f,0,0}};
        plain[e].left=-.9f;plain[e].right=.9f;plain[e].up=.9f;plain[e].down=-.9f;}

    auto base=ui::FitPanel({},plain,16.f/9);
    auto same=ui::FitPanel({},plain,16.f/9,2.0f,1.0f);
    Require(base && same,"premise: both fit");
    Require(std::abs(base->width-same->width)<.0001f,"scale 1.0 is the default");

    auto half=ui::FitPanel({},plain,16.f/9,2.0f,.5f);
    Require(half && std::abs(half->width-base->width*.5f)<.0001f,"0.5 halves the width");
    Require(std::abs(half->height-base->height*.5f)<.0001f,"and the height");
    Require(std::abs(half->width/half->height-16.f/9)<.0001f,"aspect survives scaling");

    // **Above 1 must actually grow.** The 60/40 caps are a comfort default, and
    // at a portrait aspect the vertical one binds and forces the panel narrow --
    // the case a wearer hit at 2688x2880, where it came out around 37.5 degrees:
    // too small, and notably NOT clipping anywhere, which is what proves the cap
    // was binding rather than the corner test.
    auto big=ui::FitPanel({},plain,16.f/9,2.0f,1.5f);
    Require(big && big->width>base->width,"1.5 grows the panel past the default cap");
    Require(std::abs(big->width-base->width*1.5f)<.0001f,"by exactly the factor asked for");

    // Clamped at both ends rather than refused, so a fat-fingered value degrades
    // to the nearest sane panel instead of removing the UI entirely.
    auto huge=ui::FitPanel({},plain,16.f/9,2.0f,50.f);
    auto twice=ui::FitPanel({},plain,16.f/9,2.0f,2.0f);
    Require(huge && twice && std::abs(huge->width-twice->width)<.0001f,"clamped at 2.0");
    auto tiny=ui::FitPanel({},plain,16.f/9,2.0f,.001f);
    auto floorPanel=ui::FitPanel({},plain,16.f/9,2.0f,.2f);
    Require(tiny && floorPanel && std::abs(tiny->width-floorPanel->width)<.0001f,"clamped at 0.2");
    Require(!ui::FitPanel({},plain,16.f/9,2.0f,std::nanf("")),"a non-finite scale is refused");

    // The portrait aspect is the case that motivated the dial.
    auto wide=ui::FitPanel({},plain,2560.f/1440);
    auto square=ui::FitPanel({},plain,2688.f/2880);
    Require(wide && square,"both aspects fit");
    Require(square->width<wide->width,"the portrait aspect yields a NARROWER panel");

    std::cout<<"UI panel geometry passed\n";
}
