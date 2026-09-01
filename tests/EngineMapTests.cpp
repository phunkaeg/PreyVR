#include "preyvr/EngineMap.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<std::uint8_t> BuildSyntheticSupportedImage()
{
    std::size_t size = 0;
    for (const auto& landmark : preyvr::engine::Landmarks()) {
        size = std::max(size, static_cast<std::size_t>(landmark.rva) + landmark.expected.size());
    }
    std::vector<std::uint8_t> image(size, 0xCC);
    for (const auto& landmark : preyvr::engine::Landmarks()) {
        std::copy(
            landmark.expected.begin(),
            landmark.expected.end(),
            image.begin() + static_cast<std::size_t>(landmark.rva));
    }
    return image;
}

} // namespace

int main()
{
    using preyvr::engine::ArkPlayerInteractionLayout;
    using preyvr::engine::ArkPlayerLayout;
    using preyvr::engine::ArkPlayerTargetSelectorLayout;

    Require(preyvr::engine::Landmarks().size() == 32, "all promoted runtime landmarks are typed");

    // The camera layouts are load-bearing for the per-eye lane, so assert the
    // relationships that a transcription slip would break -- not just the values.
    {
        using preyvr::engine::CameraLayout;
        using preyvr::engine::RenderCameraLayout;
        using preyvr::engine::RenderViewLayout;
        using preyvr::engine::SystemLayout;

        // Each Vec3/Matrix34 member must abut the next with no gap. Matrix34 is
        // 48B, then seven 4B scalars, then three Vec3 edges of 12B each.
        Require(CameraLayout::matrix + 48 == CameraLayout::fov, "m_Matrix is 48B and m_fov follows it");
        Require(CameraLayout::fov + 8 == CameraLayout::width, "m_fovBase sits between m_fov and m_Width");
        Require(CameraLayout::width + 4 == CameraLayout::height, "m_Width and m_Height are adjacent ints");
        Require(
            CameraLayout::edgeNearLeftTop + 24 == CameraLayout::edgeFarLeftTop,
            "m_edge_plt occupies the 12B between the near and far edge vertices");
        Require(
            CameraLayout::edgeFarLeftTop + 12 == CameraLayout::asymLeft,
            "the asymmetry block starts immediately after m_edge_flt");

        // The four asymmetry shifts are consecutive floats. CRenderView::SetCamera
        // reads them in this order; a gap here would silently mis-project one eye.
        Require(CameraLayout::asymLeft + 4 == CameraLayout::asymRight, "asymL and asymR are adjacent");
        Require(CameraLayout::asymRight + 4 == CameraLayout::asymBottom, "asymR and asymB are adjacent");
        Require(CameraLayout::asymBottom + 4 == CameraLayout::asymTop, "asymB and asymT are adjacent");
        Require(CameraLayout::asymTop + 4 <= CameraLayout::frustumPlanes, "asymmetry precedes the cached planes");

        // Every offset must fall inside the object CCamera::operator= copies.
        Require(CameraLayout::size == 0x240, "sizeof(CCamera) matches the measured copy width");
        Require(CameraLayout::zRangeMax + 4 < CameraLayout::size, "z-range fields lie inside the object");
        Require(CameraLayout::frustumPlanes + 6 * 16 <= CameraLayout::zRangeMin, "Plane[6] fits before the z-range");

        // The render view holds a full CCamera by value, so its own derived block
        // must start beyond the end of that copy -- otherwise they would overlap.
        Require(
            RenderViewLayout::camera + CameraLayout::size <= RenderViewLayout::renderCamera,
            "CRenderView::m_camera does not overlap its CRenderCamera block");
        Require(
            RenderCameraLayout::axisX == RenderViewLayout::renderCamera,
            "CRenderCamera starts at its first member; it has no vtable");

        // CRenderCamera is Vec3 x4 then six floats, contiguous.
        Require(RenderCameraLayout::axisX + 12 == RenderCameraLayout::axisY, "vX and vY are adjacent Vec3");
        Require(RenderCameraLayout::axisY + 12 == RenderCameraLayout::axisZ, "vY and vZ are adjacent Vec3");
        Require(RenderCameraLayout::axisZ + 12 == RenderCameraLayout::origin, "vZ and vOrigin are adjacent Vec3");
        Require(
            RenderCameraLayout::origin + 12 == RenderCameraLayout::frustumLeft,
            "the frustum tangents follow vOrigin immediately");
        Require(
            RenderCameraLayout::frustumLeft + 4 == RenderCameraLayout::frustumRight &&
                RenderCameraLayout::frustumRight + 4 == RenderCameraLayout::frustumBottom &&
                RenderCameraLayout::frustumBottom + 4 == RenderCameraLayout::frustumTop,
            "fWL, fWR, fWB and fWT are four consecutive floats");
        Require(
            RenderCameraLayout::frustumTop + 4 == RenderCameraLayout::nearPlane &&
                RenderCameraLayout::nearPlane + 4 == RenderCameraLayout::farPlane,
            "fNear and fFar close the CRenderCamera block");

        // The system view camera is a member, not a pointer, and the two vtable
        // slots are adjacent because SetViewCamera is declared just before it.
        Require(SystemLayout::viewCamera == 0x788, "CSystem::m_ViewCamera offset");
        Require(
            SystemLayout::vtableSetViewCamera + 8 == SystemLayout::vtableGetViewCamera,
            "SetViewCamera and GetViewCamera occupy adjacent ISystem vtable slots");
    }
    Require(!preyvr::engine::AllLandmarksMatch({}), "empty validation cannot pass vacuously");
    Require(
        ArkPlayerLayout::interaction + ArkPlayerInteractionLayout::targetSelector == 0xC58,
        "ArkPlayer target-selector offset is stable");
    Require(
        ArkPlayerLayout::interaction + ArkPlayerInteractionLayout::usableEntityId == 0xF34,
        "ArkPlayer usable-entity offset is stable");
    Require(
        ArkPlayerTargetSelectorLayout::candidateRecordSize == 0x70 &&
            ArkPlayerTargetSelectorLayout::candidateEntityId == 0x68,
        "interaction candidate record layout is stable");

    auto image = BuildSyntheticSupportedImage();
    auto results = preyvr::engine::ValidateLandmarks(image);
    Require(preyvr::engine::AllLandmarksMatch(results), "exact supported image passes");

    const auto& target = preyvr::engine::Landmarks().back();
    image[static_cast<std::size_t>(target.rva) + 4] ^= 0x01;
    results = preyvr::engine::ValidateLandmarks(image);
    Require(!preyvr::engine::AllLandmarksMatch(results), "one-byte mutation fails closed");
    Require(results.back().status == preyvr::engine::LandmarkStatus::mismatch,
        "mutation is classified as mismatch");
    Require(results.back().mismatchOffset == 4, "mismatch byte is reported");

    const auto truncated = std::span<const std::uint8_t>(image.data(), 64);
    results = preyvr::engine::ValidateLandmarks(truncated);
    Require(!preyvr::engine::AllLandmarksMatch(results), "truncated image fails closed");
    Require(results.front().status == preyvr::engine::LandmarkStatus::outOfRange,
        "truncated image is classified as out of range");

    std::cout << "PreyVR engine-map tests passed\n";
    return 0;
}
