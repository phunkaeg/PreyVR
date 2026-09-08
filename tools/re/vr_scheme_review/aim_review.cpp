// Exercise the production cached-ray hook with a fake player buffer and external
// tracking/muzzle providers. No game module is loaded and no hook is installed.
#include <windows.h>
HMODULE WINAPI ReviewModule(LPCWSTR);
#define GetModuleHandleW ReviewModule
#include "../../../src/dll/AimTakeover.cpp"
#undef GetModuleHandleW
#include <iostream>

preyvr::dll::TrackingFrame tracked;
preyvr::Vec3 drawnOrigin, drawnDirection;
bool focused = true, calibrated = false;
const preyvr::Vec3 eye{10, 20, 30}, muzzle{10.3f, 20.5f, 29.8f};
int failures = 0;
std::array<std::uint8_t, preyvr::engine::SystemLayout::viewCamera +
    preyvr::engine::CameraLayout::size> fakeSystem{};
std::uint8_t* systemSlot = fakeSystem.data();
HMODULE WINAPI ReviewModule(LPCWSTR) {
    // Stub only the module lookup. Production GameCameraYaw still walks the
    // actual slot/camera offsets and reads the supplied matrix.
    return reinterpret_cast<HMODULE>(reinterpret_cast<std::uintptr_t>(&systemSlot) -
        preyvr::engine::SystemLayout::pointerRva);
}
preyvr::dll::ReticleAimContext drawnContext{};

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
bool WriteReticleScreenPosition(void*, const Vec3& origin, const Vec3& direction,
                               const ReticleAimContext* context) {
    drawnOrigin = origin; drawnDirection = direction;
    if (context) drawnContext = *context;
    return true;
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
    const auto camera = std::span<std::uint8_t>(fakeSystem).subspan(
        engine::SystemLayout::viewCamera, engine::CameraLayout::size);
    stereo::WriteMatrix(camera, stereo::MatrixFromPose({}));
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
    // Hold a nontrivial controller orientation in LOCAL space, turn the head
    // through a 60-degree arc, and let the production producer derive play yaw
    // from the matching camera. A camera-relative implementation must fail.
    const float bodyYaw = 0.71f;
    hand.aimPose.orientation = Normalize({0.13f, -0.21f, 0.07f, 0.95f});
    gOriginMode = 1;
    Vec3 heldDirection{};
    for (int degrees = -30; degrees <= 30; degrees += 5) {
        const float yaw = degrees * 0.0174532925199433f;
        tracked.head.orientation = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
        stereo::WriteMatrix(camera, stereo::MatrixFromPose(
            {stereo::YawQuaternion(bodyYaw + yaw), eye}));
        ++tracked.sequence;
        tracked.publishedNs = MonotonicNanoseconds();
        UpdateCachedRayWithTakeover(player.data());
        UpdateAimReticleForRender();
        aim::Sample swept{};
        const bool available = TryGetAimSample(swept);
        if (degrees == -30) heldDirection = swept.direction;
        Check(available && Same(swept.direction, heldDirection) &&
            Same(drawnDirection, heldDirection), "fixed LOCAL controller, head yaw=" + std::to_string(degrees));
        Check(drawnContext.trackingSequence == tracked.sequence &&
              std::fabs(drawnContext.playYaw - bodyYaw) < 1e-5f &&
              drawnContext.controller.orientation.w == hand.aimPose.orientation.w,
              "render provenance belongs to selected aim publication");
    }
    gBodyYaw = false;
    tracked.publishedNs = MonotonicNanoseconds();
    UpdateCachedRayWithTakeover(player.data());
    aim::Sample control{};
    Check(TryGetAimSample(control) && !Same(control.direction, heldDirection),
        "positive control: camera-relative mode reintroduces head yaw");
    focused = false;
    UpdateCachedRayWithTakeover(player.data());
    aim::Sample sample;
    Check(!TryGetAimSample(sample), "failed tracking invalidates previous aim publication");
    return failures ? 1 : 0;
}
