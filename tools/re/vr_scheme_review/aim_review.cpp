// Exercise the production cached-ray hook with a fake player buffer and external
// tracking/muzzle providers. No game module is loaded and no hook is installed.
#include "../../../src/dll/AimTakeover.cpp"
#include <iostream>

preyvr::dll::TrackingFrame tracked;
preyvr::Vec3 drawnOrigin, drawnDirection;
bool focused = true, calibrated = false;
const preyvr::Vec3 eye{10, 20, 30}, muzzle{10.3f, 20.5f, 29.8f};
int failures = 0;

namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
bool EnsureMinHook() { return false; }
bool TryGetTrackingFrame(TrackingFrame& out) {
    if (!focused) return false;
    out = tracked;
    return true;
}
unsigned long long HeadTrackingReferenceGeneration() { return 2; }
float HeadTrackingReferenceYaw() { return 0; }
std::uint64_t WeaponEquipGeneration() { return 7; }
bool TryGetMuzzleFromGrip(const Pose&, std::uint64_t, Vec3& out) {
    if (!calibrated) return false;
    out = muzzle;
    return true;
}
bool WriteReticleScreenPosition(void*, const Vec3& origin, const Vec3& direction) {
    drawnOrigin = origin; drawnDirection = direction; return true;
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID*) { return MH_ERROR_UNSUPPORTED_FUNCTION; }
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_ERROR_NOT_CREATED; }
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_ERROR_NOT_CREATED; }
void __fastcall NativeRay(void* player) {
    auto* bytes = static_cast<std::uint8_t*>(player);
    const preyvr::Vec3 forward{0, 1, 0};
    std::memcpy(bytes + preyvr::engine::ArkPlayerLayout::cachedReticleOrigin, &eye, sizeof(eye));
    std::memcpy(bytes + preyvr::engine::ArkPlayerLayout::cachedReticleDirection, &forward, sizeof(forward));
}
bool Same(preyvr::Vec3 a, preyvr::Vec3 b) {
    return std::fabs(a.x-b.x) < 1e-5f && std::fabs(a.y-b.y) < 1e-5f && std::fabs(a.z-b.z) < 1e-5f;
}
void Check(bool ok, const std::string& name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
int main() {
    using namespace preyvr;
    using namespace preyvr::dll;
    std::array<std::uint8_t, 0x1900> player{};
    tracked.headValidity = {true, true, true, true, 0};
    tracked.epoch = 1; tracked.sequence = 3;
    auto& hand = tracked.hands[1];
    hand.aimValidity = hand.gripValidity = tracked.headValidity;
    hand.aimPose.position = {0.2f, -0.1f, -0.4f};
    hand.gripPose.position = hand.aimPose.position;
    gOriginal = &NativeRay;
    gEnabled = true;
    for (int hasCalibration = 0; hasCalibration <= 1; ++hasCalibration) {
        calibrated = hasCalibration != 0;
        for (unsigned mode = 0; mode <= 2; ++mode) {
            gOriginMode = mode;
            tracked.publishedNs = MonotonicNanoseconds();
            UpdateCachedRayWithTakeover(player.data());
            UpdateAimReticleForRender();
            Vec3 installed{};
            std::memcpy(&installed, player.data() + engine::ArkPlayerLayout::cachedReticleOrigin, sizeof(installed));
            aim::Sample sample;
            const std::string label = "origin mode=" + std::to_string(mode) +
                                      " calibration=" + std::to_string(hasCalibration);
            Check(TryGetAimSample(sample) && Same(sample.origin, installed) && Same(drawnOrigin, installed), label);
            Check(sample.confidence == aim::Confidence::origin, label + " does not claim a calibrated barrel direction");
            Check(Same(Rotate(sample.orientation, {0, 1, 0}), sample.direction), label + " orientation and direction share world space");
        }
    }
    focused = false;
    UpdateCachedRayWithTakeover(player.data());
    aim::Sample sample;
    Check(!TryGetAimSample(sample), "failed tracking invalidates previous aim publication");
    return failures ? 1 : 0;
}
