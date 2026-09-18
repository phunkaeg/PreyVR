#pragma once
#include "preyvr/VrMath.h"

namespace preyvr::twohand {
// Engine axes, metres. Endpoints are relative to the primary wrist in the
// authored barrel frame. The animation observer supplies these, not XR poses.
struct Region { Vec3 start{}, end{}; float radius = .10f; };
struct Input {
    Pose aim{}, support{};
    Vec3 primary{}, visualPrimaryOffset{};
    Region region{};
    float squeeze = 0, dt = 0;
    std::uint64_t owner = 0, reference = 0, epoch = 0;
    bool usable = false;
};
struct Output {
    Quaternion orientation{}, correction{}, supportOrientation{};
    Vec3 socket{};
    float blend = 0;
    bool held = false;
};
class Solver {
public:
    Output Update(const Input& input);
private:
    std::uint64_t owner_ = 0, reference_ = 0, epoch_ = 0;
    bool held_ = false, armed_ = false;
    float blend_ = 0;
    Vec3 socket_{};
    Quaternion swing_{}, supportInAim_{};
};
// Also useful for diagnostics/authoring. Refuses malformed regions and poses.
bool ClosestGrip(const Input& input, Vec3& localPoint);
} // namespace preyvr::twohand
