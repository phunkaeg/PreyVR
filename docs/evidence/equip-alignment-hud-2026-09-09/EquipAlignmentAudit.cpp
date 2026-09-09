#include "preyvr/AnimIk.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace preyvr;
using namespace preyvr::animik;
constexpr float pi = 3.14159265358979323846f;
Quaternion Inv(Quaternion q) { q = Normalize(q); return {-q.x,-q.y,-q.z,q.w}; }
Quaternion Q(Vec3 a, float deg) {
    const float length = std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
    const float s = std::sin(deg*pi/360.0f)/length;
    return {a.x*s,a.y*s,a.z*s,std::cos(deg*pi/360.0f)};
}
Quaternion Mul(Quaternion a, Quaternion b) { return Normalize(Multiply(a,b)); }
float Error(Quaternion a, Quaternion b) {
    a=Normalize(a); b=Normalize(b);
    float dot=std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
    if(dot>1.0f) dot=1.0f;
    return 2.0f*std::acos(dot)*180.0f/pi;
}
void Require(bool pass, const char* message) {
    if(!pass) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}

int main() {
    // Controlled reproduction, not a native asset model: the settled animated
    // wrist and neutral grip are identity, while the controller points 45 deg
    // away at re-equip. Only that calibration-time angle varies.
    CalibrationState state{};
    state.Bind(1,2,3); state.Request(1); state.Commit(0,Quaternion{});
    state.Bind(2,2,3);
    for(unsigned i=0;i<CalibrationState::kSettleFrames;++i) state.Tick();
    Require(state.Pending(0),"weapon rebind requests an automatic capture");
    const Quaternion atCapture=Q({0,0,1},45);
    const Quaternion nativeWrist{};
    state.Commit(0,CalibrateRotationOffset(atCapture,nativeWrist));
    const auto captureGoal=ApplyRotationOffset(atCapture,state.offsets[0]);
    const auto returnedGoal=ApplyRotationOffset(Quaternion{},state.offsets[0]);
    const float captureError=Error(captureGoal,atCapture);
    const float returnedError=Error(returnedGoal,Quaternion{});
    Require(std::fabs(captureError-45)<0.01f && std::fabs(returnedError-45)<0.01f,
            "the initial aiming mismatch is preserved after returning to neutral");
    std::printf("existing_calibration: callback_delay=%u capture_error_deg=%.3f returned_to_neutral_error_deg=%.3f\n",
                CalibrationState::kSettleFrames,captureError,returnedError);

    // Proposed full-basis contract with SYNTHETIC authored data. It demonstrates
    // algebra, not the existence/correctness of any Prey barrel-helper basis.
    // B maps barrel frame into weapon frame; M maps weapon into wrist frame.
    // WeaponWorld = AimWorld * inverse(B); WristWorld = WeaponWorld * inverse(M).
    // Then WristWorld * M * B must equal AimWorld for every equip-time pose.
    const Quaternion barrels[]{Q({1,0,0},20),Mul(Q({0,0,1},-35),Q({0,1,0},15))};
    const Quaternion mounts[]{Q({0,1,0},-30),Mul(Q({1,0,0},50),Q({0,0,1},20))};
    const Quaternion roots[]{Quaternion{},Mul(Q({0,0,1},70),Q({1,0,0},25))};
    const Quaternion aims[]{Quaternion{},Q({0,0,1},-65),Q({1,0,0},50),
                            Mul(Q({0,1,0},80),Q({1,0,0},-35))};
    float maxError=0; unsigned count=0;
    for(unsigned weapon=0;weapon<2;++weapon) for(auto root:roots) for(auto aim:aims) {
        const Quaternion weaponWorld=Mul(aim,Inv(barrels[weapon]));
        const Quaternion wristWorld=Mul(weaponWorld,Inv(mounts[weapon]));
        const Location location{root,{7,-3,1.5f},1.3f};
        const Quaternion modelGoal=WorldToModel(location,wristWorld);
        const Quaternion renderedBarrel=Mul(Mul(Mul(root,modelGoal),mounts[weapon]),barrels[weapon]);
        const float error=Error(renderedBarrel,aim);
        if(error>maxError) maxError=error;
        Require(error<0.1f,"authored full-basis solve closes through changed model roots");
        ++count;
    }
    std::printf("proposed_contract: synthetic_asset_pairs=2 noncommuting_pose_root_cases=%u max_barrel_error_deg=%.4f\n",count,maxError);
    std::puts("PASS: source calibration reproduction and synthetic design controls; no shipped fix or headset acceptance");
}
