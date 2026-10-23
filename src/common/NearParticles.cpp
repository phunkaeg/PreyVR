#include "preyvr/NearParticles.h"

#include <cmath>

namespace preyvr::nearfx {

bool IsSpriteVertexCaller(std::uint32_t rva)
{
    return rva == kSetVerticesReturn || rva == kSetTailVerticesReturn;
}

Builder BuilderFromReturnRva(std::uint32_t rva)
{
    switch (rva) {
    case kSpriteContextReturn: return Builder::Sprites;
    case kGeometryContextReturn: return Builder::Geometry;
    case kOtherContextReturn: return Builder::Other;
    default: return Builder::Unknown;
    }
}

const char* BuilderName(Builder builder)
{
    switch (builder) {
    case Builder::Sprites: return "sprites";
    case Builder::Geometry: return "geometry";
    case Builder::Other: return "other";
    default: return "unknown";
    }
}

bool CameraSpaceOrigin(Vec3 camera, const EyeCamera& eye, Vec3& out, float tolerance)
{
    const float values[] = {camera.x, camera.y, camera.z, eye.position.x, eye.position.y,
                            eye.position.z, eye.centre.x, eye.centre.y, eye.centre.z};
    for (float v : values) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    if (std::fabs(camera.x - eye.position.x) > tolerance ||
        std::fabs(camera.y - eye.position.y) > tolerance ||
        std::fabs(camera.z - eye.position.z) > tolerance) {
        return false;
    }
    // An eye is half an IPD from its cyclops. Anything else is not a record of
    // a stereo eye and must not move a particle.
    const float dx = eye.position.x - eye.centre.x;
    const float dy = eye.position.y - eye.centre.y;
    const float dz = eye.position.z - eye.centre.z;
    const float offset = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (offset > 0.06f) {
        return false;
    }
    out = eye.centre;
    return true;
}

bool EyeOffset(Vec3 camera, const EyeCamera& eye, Vec3& out, float tolerance)
{
    Vec3 centre{};
    if (!CameraSpaceOrigin(camera, eye, centre, tolerance)) {
        return false;
    }
    out = {eye.position.x - centre.x, eye.position.y - centre.y, eye.position.z - centre.z};
    return true;
}

bool BridgeShift(Vec3 positionOffset, Vec3 forward, Vec3& shift)
{
    const float values[] = {positionOffset.x, positionOffset.y, positionOffset.z, forward.x, forward.y, forward.z};
    for (float v : values) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    const float length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    // An authored offset this far back along the barrel is the bridge; a
    // weapon's own effects (coil glows, barrel crawls) are sprites.
    if (positionOffset.y > -kBridgeMinBack || std::fabs(length - 1.0f) > 0.01f || positionOffset.y < -2.0f) {
        return false;
    }
    const float distance = -2.0f * positionOffset.y;
    shift = {forward.x * distance, forward.y * distance, forward.z * distance};
    return true;
}

} // namespace preyvr::nearfx
