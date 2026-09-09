#include "preyvr/AnimIk.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::animik;

void Require(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool Near(float a, float b) { return std::fabs(a-b) < 0.00002f; }
bool Near(Vec3 a, Vec3 b) { return Near(a.x,b.x) && Near(a.y,b.y) && Near(a.z,b.z); }
Vec3 Delta(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }

// Analytical fixture for the native producer's symmetric perspective
// unprojection at clip Z=0, matched full viewport, forward +Y, up +Z.
// This is derived math, NOT execution/emulation of the game's native function.
Vec3 NearPlanePoint(Vec3 camera, float screenX, float screenY)
{
    constexpr float nearPlane=0.1f, tanHalfHorizontal=1.73205080757f;
    constexpr float aspect=2688.0f/2880.0f;
    return {camera.x + nearPlane*tanHalfHorizontal*(2*screenX-1),
            camera.y + nearPlane,
            camera.z + nearPlane*(tanHalfHorizontal/aspect)*(1-2*screenY)};
}

int main()
{
    const Vec3 camera{10,20,1.7f};
    const Pose head{{},{0,1.6f,0}}, left{{},{-0.25f,1.3f,-0.4f}};
    const Vec3 shoulder{9.8f,20.03f,1.45f};
    const Vec3 stable=ControllerWorldFromHead(0,camera,head,left).position;
    const Vec3 nearA=NearPlanePoint(camera,0.25f,0.5f);
    const Vec3 nearB=NearPlanePoint(camera,0.75f,0.5f);
    const Vec3 a=ControllerWorldFromHead(0,nearA,head,left).position;
    const Vec3 b=ControllerWorldFromHead(0,nearB,head,left).position;
    Require(Near(Delta(b,a),Delta(nearB,nearA)),"all native-ray anchor movement enters fixed left goal");
    Require(Near(b.x-a.x,0.17320508f),"reticle-only sweep produces expected lateral coupling");
    const Vec3 scaledA=ScaleReach(shoulder,a,0.65f), scaledB=ScaleReach(shoulder,b,0.65f);
    Require(Near(scaledB.x-scaledA.x,0.1125833f),"65 percent retains 65 percent of moving anchor");
    Require(Near(ClampToReach(shoulder,scaledA,0.7f),scaledA) &&
            Near(ClampToReach(shoulder,scaledB,0.7f),scaledB),"coupling occurs with no clamp");
    Require(Near(ControllerWorldFromHead(0,camera,head,left).position,stable),
            "camera-centred negative control stays fixed across ray-origin sweep");
    std::cout << "ray_anchor: fixed_head_left_camera_yaw=1 ray_delta_m=" << b.x-a.x
              << " scaled_goal_delta_m=" << scaledB.x-scaledA.x << " clamped=0\n";

    const Vec3 movedShoulder{shoulder.x+0.2f,shoulder.y,shoulder.z};
    const Vec3 rawA=ScaleReach(shoulder,stable,0.65f);
    const Vec3 rawB=ScaleReach(movedShoulder,stable,0.65f);
    Require(Near(rawB.x-rawA.x,0.07f),"animated shoulder contributes 35 percent at reach 65");
    Require(Near(ScaleReach(shoulder,stable,1),ScaleReach(movedShoulder,stable,1)),
            "reach 100 removes shoulder blend when clamp remains inactive");
    Require(Near(ClampToReach(shoulder,rawA,0.7f),rawA) &&
            Near(ClampToReach(movedShoulder,rawB,0.7f),rawB),"shoulder coupling needs no clamp");
    std::cout << "shoulder_anchor: fixed_raw_goal=1 shoulder_delta_m=0.2"
                 " scaled_goal_delta_m=" << rawB.x-rawA.x << " clamped=0\n";

    // A correct inverse model transform by itself must not drag a world goal.
    // Moving the root and the shoulder in world space are different variables.
    for (float angle : {0.0f,0.7f,-1.1f}) {
        const Location loc{{0,0,std::sin(angle/2),std::cos(angle/2)},
                           {12+angle,19-angle,0.4f},1.3f};
        const Vec3 goalModel=WorldToModel(loc,stable);
        const Vec3 shoulderModel=WorldToModel(loc,shoulder);
        const Vec3 roundTrip=ModelToWorld(loc,ScaleReach(shoulderModel,goalModel,0.65f));
        Require(Near(roundTrip,rawA),"model transform cancels when world anchors are held fixed");
    }
    std::cout << "model_control: rotated_translated_scaled_locations=3 world_goal_invariant=1\n";
    std::cout << "PASS: fixed-input math with committed IK code; not headset acceptance\n";
}
