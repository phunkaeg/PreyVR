#include "preyvr/AimState.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::array<std::uint8_t, 32> kCenteredFixture = {
    0x9B, 0x70, 0x43, 0x44, 0x5D, 0x70, 0xC4, 0x44,
    0x9A, 0xA1, 0x88, 0x41, 0xEA, 0x58, 0x72, 0xBF,
    0x03, 0x6B, 0x7B, 0xBE, 0x46, 0xA8, 0x55, 0xBE,
    0x00, 0x00, 0x00, 0x3F, 0x33, 0x33, 0x13, 0x3F,
};

constexpr std::array<std::uint8_t, 32> kShiftedFixture = {
    0xE9, 0x6F, 0x43, 0x44, 0xB5, 0x71, 0xC4, 0x44,
    0x97, 0xA1, 0x88, 0x41, 0x32, 0x09, 0x78, 0xBF,
    0xA4, 0x14, 0x20, 0x3E, 0xB5, 0x78, 0x44, 0xBE,
    0x00, 0x00, 0x20, 0x3F, 0x33, 0x33, 0x13, 0x3F,
};

constexpr std::array<std::uint8_t, 32> kWrenchRayFixture = {
    0x90, 0xE4, 0x3F, 0x44, 0x9B, 0xA1, 0xC3, 0x44,
    0x58, 0x71, 0x88, 0x41, 0xCA, 0x11, 0x15, 0x3F,
    0xD6, 0xE5, 0x31, 0x3F, 0x50, 0x06, 0xD8, 0xBE,
    0x00, 0x00, 0x00, 0x3F, 0x33, 0x33, 0x13, 0x3F,
};

} // namespace

int main()
{
    const auto centered = preyvr::DecodeCachedReticleState(kCenteredFixture);
    const auto shifted = preyvr::DecodeCachedReticleState(kShiftedFixture);
    Require(centered.has_value() && shifted.has_value(), "captured fixtures decode");
    Require(std::fabs(centered->screenPosition.x - 0.5f) < 0.0001f,
        "centered fixture retains reticle X");
    Require(std::fabs(shifted->screenPosition.x - 0.625f) < 0.0001f,
        "shifted fixture retains reticle X");

    const float angularDelta = preyvr::DirectionAngularDeltaDegrees(*centered, *shifted);
    Require(angularDelta > 20.0f && angularDelta < 30.0f,
        "captured reticle shift rotates the world ray by the expected envelope");
    Require(preyvr::DidScreenReticleMoveRay(*centered, *shifted, 0.1f, 20.0f),
        "captured reversible probe satisfies detached-ray fixture policy");
    Require(!preyvr::DidScreenReticleMoveRay(*centered, *centered, 0.1f, 20.0f),
        "unchanged fixture cannot produce a false positive");

    auto corrupt = kCenteredFixture;
    corrupt[12] = 0;
    corrupt[13] = 0;
    corrupt[14] = 0;
    corrupt[15] = 0;
    corrupt[16] = 0;
    corrupt[17] = 0;
    corrupt[18] = 0;
    corrupt[19] = 0;
    corrupt[20] = 0;
    corrupt[21] = 0;
    corrupt[22] = 0;
    corrupt[23] = 0;
    Require(!preyvr::DecodeCachedReticleState(corrupt).has_value(),
        "non-direction fixture fails closed");

    const auto wrenchRay = preyvr::DecodeCachedReticleState(kWrenchRayFixture);
    Require(wrenchRay.has_value(), "captured wrench-query ray decodes");
    const auto yawProbe = preyvr::MakeBoundedYawProbe(*wrenchRay, 10.0f);
    Require(yawProbe.has_value(), "bounded ten-degree A0b yaw is accepted");
    Require(std::fabs(preyvr::DirectionYawDeltaDegrees(
            wrenchRay->direction, yawProbe->direction) - 10.0f) < 0.01f,
        "A0b yaw produces the requested horizontal azimuth delta");
    const float rayAngularDelta = preyvr::DirectionAngularDeltaDegrees(*wrenchRay, *yawProbe);
    Require(std::fabs(rayAngularDelta - 9.0643f) < 0.01f,
        "pitched wrench ray retains the measured three-dimensional angular delta");
    Require(std::fabs(yawProbe->origin.x - wrenchRay->origin.x) < 0.0001f &&
            std::fabs(yawProbe->origin.y - wrenchRay->origin.y) < 0.0001f &&
            std::fabs(yawProbe->origin.z - wrenchRay->origin.z) < 0.0001f,
        "A0b yaw preserves ray origin");
    Require(std::fabs(yawProbe->screenPosition.x - wrenchRay->screenPosition.x) < 0.0001f &&
            std::fabs(yawProbe->screenPosition.y - wrenchRay->screenPosition.y) < 0.0001f,
        "A0b yaw does not move the UI reticle");
    Require(preyvr::EncodeDirectionBytes(yawProbe->direction).has_value(),
        "A0b direction produces exactly twelve writable bytes");
    Require(std::fabs(preyvr::ExpectedRaySeparation(2.75f, rayAngularDelta) - 0.4346f) < 0.001f,
        "ten-degree probe has the expected full-range separation");
    Require(!preyvr::MakeBoundedYawProbe(*wrenchRay, 16.0f).has_value(),
        "yaw beyond the research safety bound fails closed");

    std::cout << "PreyVR aim-state fixture tests passed\n";
    return 0;
}
