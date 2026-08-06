#pragma once

#include <cstdint>

namespace preyvr {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Pose {
    Quaternion orientation{};
    Vec3 position{};
};

struct PoseValidity {
    bool orientationValid = false;
    bool positionValid = false;
    bool orientationTracked = false;
    bool positionTracked = false;
    std::uint64_t ageNanoseconds = 0;
};

Quaternion Normalize(Quaternion value);
Quaternion Multiply(Quaternion parent, Quaternion child);
Vec3 Rotate(Quaternion rotation, Vec3 value);
Pose Compose(Pose parent, Pose child);
Vec2 ApplyRadialDeadzone(Vec2 input, float deadzone);
bool IsPoseUsable(const Pose& pose, const PoseValidity& validity, std::uint64_t maxAgeNanoseconds);

} // namespace preyvr
