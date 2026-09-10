#include "preyvr/UiPanel.h"
#include "preyvr/StereoCamera.h"
#include <algorithm>
#include <cmath>

namespace preyvr::ui {
Pose CylinderAxis(const Surface& s) {
    return s.angle>0 ? Compose(s.panel.pose,Pose{{},{0,0,s.panel.width/s.angle}}) : s.panel.pose;
}
Pose SurfacePoint(const Surface& s,float u,float v) {
    if(s.angle<=0)return Compose(s.panel.pose,Pose{{},{(u-.5f)*s.panel.width,(.5f-v)*s.panel.height,0}});
    const float a=(u-.5f)*s.angle,r=s.panel.width/s.angle;
    // The tangent faces the cylinder axis (the viewer is on the inside).
    return Compose(CylinderAxis(s),Pose{{0,-std::sin(a*.5f),0,std::cos(a*.5f)},
        {r*std::sin(a),(.5f-v)*s.panel.height,-r*std::cos(a)}});
}
int PointerButtons::Update(bool active,bool inside,bool pressed) {
    if(!active){armed=false;if(down){down=false;return -1;}return 0;}
    if(!pressed){armed=true;if(down){down=false;return -1;}return 0;}
    if(armed){armed=false;if(inside){down=true;return 1;}}
    return 0;
}
std::optional<std::array<float,2>> PanelFraction(float tanX,float tanY,float width,float height,float distance) {
    if(!std::isfinite(tanX)||!std::isfinite(tanY)||!std::isfinite(width)||!std::isfinite(height)||
       !std::isfinite(distance)||width<=0||height<=0||distance<=0)return {};
    const std::array<float,2> result{.5f+tanX*distance/width,.5f-tanY*distance/height};
    if(!std::isfinite(result[0])||!std::isfinite(result[1]))return {};
    return result;
}
namespace {
bool Valid(const Pose& p) {
    PoseValidity v{true,true,true,true,0};
    return IsPoseUsable(p,v,1);
}
bool Valid(const Eye& e) {
    return Valid(e.pose) && std::isfinite(e.left) && std::isfinite(e.right) &&
        std::isfinite(e.up) && std::isfinite(e.down) &&
        e.left < 0 && e.left > -1.55f && e.right > 0 && e.right < 1.55f &&
        e.down < 0 && e.down > -1.55f && e.up > 0 && e.up < 1.55f;
}
}
std::optional<RayHit> Intersect(const Surface& s,const Pose& aim) {
    if(!Valid(s.panel.pose)||!Valid(aim)||!std::isfinite(s.panel.width)||!std::isfinite(s.panel.height)||
       s.panel.width<=0||s.panel.height<=0||!std::isfinite(s.angle)||s.angle<0||s.angle>1.2f)return {};
    const auto axis=CylinderAxis(s);
    const auto inv=stereo::Conjugate(axis.orientation);
    const auto o=Rotate(inv,{aim.position.x-axis.position.x,aim.position.y-axis.position.y,aim.position.z-axis.position.z});
    const auto d=Rotate(inv,Rotate(aim.orientation,{0,0,-1}));
    float t=0,u=0;
    if(s.angle==0) {
        if(d.z>=-.00001f||o.z<=0)return {};
        t=-o.z/d.z;u=.5f+(o.x+t*d.x)/s.panel.width;
    } else {
        const float r=s.panel.width/s.angle;
        const float a=d.x*d.x+d.z*d.z,b=o.x*d.x+o.z*d.z,c=o.x*o.x+o.z*o.z-r*r;
        const float discriminant=b*b-a*c;
        if(a<.000001f||discriminant<0)return {};
        // Exit intersection hits the inside face. The entry root is back-facing.
        t=(-b+std::sqrt(discriminant))/a;
        u=.5f+std::atan2(o.x+t*d.x,-(o.z+t*d.z))/s.angle;
    }
    if(!std::isfinite(t)||t<=.01f||t>10)return {};
    const float v=.5f-(o.y+t*d.y)/s.panel.height;
    const auto direction=Rotate(aim.orientation,{0,0,-1});
    return RayHit{u,v,t,{aim.position.x+t*direction.x,aim.position.y+t*direction.y,aim.position.z+t*direction.z},
        u>=0&&u<=1&&v>=0&&v<=1};
}
bool SurfaceVisible(const Surface& s,const std::array<Eye,2>& eyes) {
    if(!Valid(s.panel.pose)||!std::isfinite(s.panel.width)||!std::isfinite(s.panel.height)||
       s.panel.width<=0||s.panel.height<=0||!std::isfinite(s.angle)||s.angle<0||s.angle>1.2f)return false;
    for(const auto& eye:eyes) {
        if(!Valid(eye))return false;
        for(int i=0;i<=16;++i)for(float v:{0.f,1.f}) {
            const auto p=SurfacePoint(s,static_cast<float>(i)/16,v).position;
            const auto q=Rotate(stereo::Conjugate(eye.pose.orientation),{p.x-eye.pose.position.x,p.y-eye.pose.position.y,p.z-eye.pose.position.z});
            if(q.z>=-.01f)return false;
            const float x=q.x/-q.z,y=q.y/-q.z;
            if(x<std::tan(eye.left)*.72f||x>std::tan(eye.right)*.72f||y<std::tan(eye.down)*.72f||y>std::tan(eye.up)*.72f)return false;
        }
    }
    return true;
}
bool CornersVisible(const Panel& panel, const std::array<Eye,2>& eyes) {
    if (!Valid(panel.pose) || !std::isfinite(panel.width) || !std::isfinite(panel.height) ||
        panel.width <= 0 || panel.height <= 0) return false;
    for (const auto& eye : eyes) {
        if (!Valid(eye)) return false;
        for (float x : {-0.5f,0.5f}) for (float y : {-0.5f,0.5f}) {
            const auto world = Compose(panel.pose, Pose{{},{x*panel.width,y*panel.height,0}}).position;
            const Vec3 delta{world.x-eye.pose.position.x,world.y-eye.pose.position.y,
                             world.z-eye.pose.position.z};
            const auto local=Rotate(stereo::Conjugate(eye.pose.orientation),delta);
            if (local.z >= -.01f) return false;
            const float tx=local.x/-local.z, ty=local.y/-local.z;
            constexpr float margin=.72f;
            if (tx < std::tan(eye.left)*margin || tx > std::tan(eye.right)*margin ||
                ty < std::tan(eye.down)*margin || ty > std::tan(eye.up)*margin) return false;
        }
    }
    return true;
}
std::optional<Panel> FitPanel(const Pose& head, const std::array<Eye,2>& eyes,
                             float aspect, float distance, float scale) {
    if (!Valid(head) || !Valid(eyes[0]) || !Valid(eyes[1]) ||
        !std::isfinite(aspect) || aspect < .25f || aspect > 5 ||
        !std::isfinite(distance) || distance < 1 || distance > 4 ||
        !std::isfinite(scale)) return {};
    if (scale > 1) scale = 1; if (scale < .2f) scale = .2f;
    Panel panel{};
    panel.pose=Compose(head,Pose{{},{0,0,-distance}});
    // At most 60 degrees horizontally / 40 vertically, times the wearer's scale.
    // The corner test below only shrinks until the corners fit the frustum the
    // runtime REPORTS, which is an optical bound rather than what the lenses
    // show -- so passing it is not the same as being visible.
    panel.width=scale*std::min(2*distance*std::tan(.523598776f),
                         2*distance*std::tan(.34906585f)*aspect);
    panel.height=panel.width/aspect;
    for (int attempt=0;attempt<80;++attempt) {
        if (CornersVisible(panel,eyes)) return panel;
        panel.width*=.95f; panel.height*=.95f;
    }
    return {};
}
}
