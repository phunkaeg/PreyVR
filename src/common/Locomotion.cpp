#include "preyvr/Locomotion.h"

#include <cmath>

namespace preyvr::locomotion {

bool ShapeStick(float x, float y, float deadzone, float* outX, float* outY)
{
    if (outX == nullptr || outY == nullptr) {
        return false;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(deadzone)) {
        return false;
    }
    // A deadzone at or past the rim would divide by zero below and, before that,
    // means every reading is dead -- a configuration that silently disables
    // movement is worse than a refusal.
    if (deadzone < 0.0f || deadzone >= 1.0f) {
        return false;
    }

    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude <= deadzone) {
        *outX = 0.0f;
        *outY = 0.0f;
        return true;
    }

    // Clamped to the unit circle *before* rescaling: a square-gated stick reports
    // magnitudes past 1 on the diagonals, and letting those through would hand
    // the engine an axis value it never sees from real hardware.
    const float clamped = magnitude > 1.0f ? 1.0f : magnitude;
    const float scale = (clamped - deadzone) / (1.0f - deadzone);
    *outX = (x / magnitude) * scale;
    *outY = (y / magnitude) * scale;
    return true;
}

unsigned int StickAxis::Update(float x, float y, AxisEvent out[2])
{
    if (out == nullptr) {
        return 0;
    }
    float shapedX = 0.0f;
    float shapedY = 0.0f;
    if (!ShapeStick(x, y, policy_.deadzone, &shapedX, &shapedY)) {
        return 0;   // state untouched: a bad reading must not become the baseline
    }

    unsigned int count = 0;
    // The first update always emits, so the engine's stored axis starts at a
    // value we chose rather than whatever the last real gamepad left there.
    const bool forced = !primed_;

    if (forced || std::fabs(shapedX - lastX_) > policy_.changeEpsilon) {
        out[count].keyId = kKeyThumbLX;
        out[count].keyName = "xi_thumblx";
        out[count].value = shapedX;
        out[count].deviceType = kDeviceGamepad;
        out[count].state = kStateChanged;
        ++count;
        lastX_ = shapedX;
    }
    if (forced || std::fabs(shapedY - lastY_) > policy_.changeEpsilon) {
        out[count].keyId = kKeyThumbLY;
        out[count].keyName = "xi_thumbly";
        out[count].value = shapedY;
        out[count].deviceType = kDeviceGamepad;
        out[count].state = kStateChanged;
        ++count;
        lastY_ = shapedY;
    }
    primed_ = true;
    return count;
}

void StickAxis::Reset()
{
    lastX_ = 0.0f;
    lastY_ = 0.0f;
    primed_ = false;
}

} // namespace preyvr::locomotion
