#include "NearViewStereo.h"

#include "CameraEditHook.h"
#include "Logger.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <span>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_near_stereo " + line);
}

// The per-view constant-buffer packer. Its second argument is the finished
// view-info, which is why this is the seam: the near VP is complete here and has
// not yet been transposed into the upload payload.
//
// Signature is deliberately wider than the three arguments the call site is known
// to pass. On x64 the extra register arguments are harmless, and forwarding the
// return value costs nothing if the real function is void.
using PackViewInfoFn = void*(__fastcall*)(void*, std::uint8_t*, void*, void*);

constexpr std::uintptr_t kPackerRva = 0xFB57A0;

// **22 bytes, stopping before the RIP-relative displacement at offset 23.** A
// signature embedding a `disp32` encodes a distance to a global and is exact for
// one build only -- RE-010 in the fleet playbook, and this project has three such
// signatures already recorded as a mistake.
constexpr std::array<std::uint8_t, 22> kPackerPrologue{
    0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x81, 0xEC, 0x30, 0x02, 0x00,
    0x00, 0x48, 0x8D, 0x6C, 0x24, 0x60,
};

// Translation-free view x near projection, per the H-011 investigation.
constexpr std::size_t kNearViewProjection = 0xA0;

void* gTarget = nullptr;
std::atomic<PackViewInfoFn> gOriginal{nullptr};
bool gInstalled = false;

std::atomic<bool> gEnabled{false};
std::atomic<bool> gZeroDelta{false};
std::atomic<float> gHalfIpd{0.032f};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gRefused{0};
std::atomic<unsigned long long> gNoEye{0};
std::atomic<int> gLastDeltaMicrometres{0};

// The camera's right axis in world space, from the engine's own view camera.
// The displacement has to be a world-space vector because the matrix rows it is
// combined with are the world axes' contributions to clip space.
bool CameraRightAxis(Vec3& out)
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const systemPtr =
        *reinterpret_cast<std::uint8_t**>(base + engine::SystemLayout::pointerRva);
    if (systemPtr == nullptr) {
        return false;
    }
    const auto* const camera = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(systemPtr) + engine::SystemLayout::viewCamera);
    const stereo::Matrix34 m =
        stereo::ReadMatrix(std::span<const std::uint8_t>(camera, engine::CameraLayout::size));
    // Engine basis: column 0 is right.
    out = Vec3{m[0], m[4], m[8]};
    const float length = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
    if (!std::isfinite(length) || length < 0.9f || length > 1.1f) {
        return false;   // not an orthonormal basis; refuse rather than guess
    }
    return true;
}

// A projection-shaped matrix has a finite, non-trivial magnitude. Several callers
// build temporary view-info and share this packer, so this is the guard against
// editing something that is not the pass we mean.
bool LooksLikeProjection(const float* m)
{
    float magnitude = 0.0f;
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) {
            return false;
        }
        magnitude += std::fabs(m[i]);
    }
    return magnitude > 0.001f && magnitude < 1.0e9f;
}

void* __fastcall PackViewInfoWithEyeOffset(void* owner, std::uint8_t* viewInfo,
                                           void* third, void* fourth)
{
    const PackViewInfoFn original = gOriginal.load(std::memory_order_acquire);
    if (!gEnabled.load(std::memory_order_acquire) || viewInfo == nullptr) {
        return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
    }

    float* const nearVp = reinterpret_cast<float*>(viewInfo + kNearViewProjection);
    if (!LooksLikeProjection(nearVp)) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
    }

    // Which eye is being built. -1 means the camera hook has not published one,
    // which is a real state during startup and while stereo is disarmed.
    const int eye = LastRenderedEye();
    if (eye != 0 && eye != 1) {
        gNoEye.fetch_add(1, std::memory_order_relaxed);
        return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
    }
    Vec3 right{};
    if (!CameraRightAxis(right)) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
    }

    // Eye minus cyclops: half the IPD, left negative and right positive.
    const float half = gZeroDelta.load(std::memory_order_acquire)
                           ? 0.0f
                           : gHalfIpd.load(std::memory_order_relaxed);
    const float signed_ = (eye == 0) ? -half : half;
    const Vec3 delta{right.x * signed_, right.y * signed_, right.z * signed_};

    // Snapshot, edit, call, restore. Nothing accumulates and no other consumer of
    // this view-info sees an edited matrix, which is also what makes the
    // zero-delta control meaningful -- it exercises every step and must be
    // pixel-identical to no edit.
    float original_[16];
    std::memcpy(original_, nearVp, sizeof(original_));

    // Row-vector layout: row 3 is the translation row.
    //   M[3][j] = M0[3][j] - d.x*M0[0][j] - d.y*M0[1][j] - d.z*M0[2][j]
    for (int j = 0; j < 4; ++j) {
        nearVp[12 + j] = original_[12 + j]
                         - delta.x * original_[0 + j]
                         - delta.y * original_[4 + j]
                         - delta.z * original_[8 + j];
    }
    gLastDeltaMicrometres.store(static_cast<int>(signed_ * 1.0e6f), std::memory_order_relaxed);
    gApplied.fetch_add(1, std::memory_order_relaxed);

    void* const result = original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;

    std::memcpy(nearVp, original_, sizeof(original_));
    return result;
}

bool Install()
{
    if (gInstalled) {
        return true;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + kPackerRva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    kPackerPrologue.data(), kPackerPrologue.size()) != 0) {
        Log("result=unavailable detail=packer_prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    PackViewInfoFn original = nullptr;
    if (MH_CreateHook(gTarget, reinterpret_cast<void*>(&PackViewInfoWithEyeOffset),
                      reinterpret_cast<void**>(&original)) != MH_OK) {
        Log("result=failed detail=create_hook");
        return false;
    }
    gOriginal.store(original, std::memory_order_release);
    if (MH_EnableHook(gTarget) != MH_OK) {
        Log("result=failed detail=enable_hook");
        MH_RemoveHook(gTarget);
        return false;
    }
    gInstalled = true;
    Log("result=0 detail=hook_installed target=PackViewInfo rva=0xFB57A0");
    return true;
}

} // namespace

DWORD SetNearViewStereo(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        if (!Install()) {
            return 2;
        }
        gApplied.store(0, std::memory_order_relaxed);
        gRefused.store(0, std::memory_order_relaxed);
        gNoEye.store(0, std::memory_order_relaxed);
    }
    gEnabled.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (on ? "1" : "0"));
    return 0;
}

DWORD SetNearViewHalfIpdMillimetres(unsigned int millimetres)
{
    // A human half-IPD is 27 to 38 mm. Outside that is a caller error, and this
    // multiplies a matrix on a render thread.
    if (millimetres < 20u || millimetres > 45u) {
        Log("result=refused detail=half_ipd_out_of_range value=" + std::to_string(millimetres));
        return 1;
    }
    gHalfIpd.store(static_cast<float>(millimetres) / 1000.0f, std::memory_order_relaxed);
    Log("result=0 detail=half_ipd_mm value=" + std::to_string(millimetres));
    return 0;
}

DWORD SetNearViewZeroDeltaControl(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gZeroDelta.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=zero_delta_control value=") + (on ? "1" : "0"));
    return 0;
}

unsigned long long NearViewAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long NearViewRefusedCount() { return gRefused.load(std::memory_order_relaxed); }
unsigned long long NearViewNoEyeCount() { return gNoEye.load(std::memory_order_relaxed); }
int NearViewLastDeltaMicrometres() { return gLastDeltaMicrometres.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
