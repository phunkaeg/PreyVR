#include "PassCameraProbe.h"

#include "CameraEditHook.h"
#include "Logger.h"
#include "preyvr/CameraEdit.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <span>
#include <sstream>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_pass_camera " + line);
}

// R-071 CreateGeneralPassRenderingInfo(out, const CCamera*, flags, bAuxWindow).
// RCX is a caller-supplied 64-byte stack buffer, RDX the camera.
using CreateGeneralPassFn =
    void*(__fastcall*)(void* out, const std::uint8_t* camera, std::uint32_t flags, std::uint32_t aux);

void* gTarget = nullptr;
std::atomic<CreateGeneralPassFn> gOriginal{nullptr};
bool gHookCreated = false;

std::atomic<bool> gEnabled{false};
std::atomic<unsigned long long> gSamples{0};
std::atomic<unsigned long long> gAgreements{0};
std::atomic<unsigned long long> gDisagreements{0};
std::atomic<unsigned long long> gLastDifference{0};
std::atomic<unsigned long long> gMaxDifference{0};

// Resolved from the landmark table, so this inherits the fail-closed byte gate
// rather than trusting a hardcoded address.
const engine::Landmark* FindLandmark(std::string_view id)
{
    for (const auto& landmark : engine::Landmarks()) {
        if (landmark.id == id) {
            return &landmark;
        }
    }
    return nullptr;
}

Vec3 ForwardOf(const std::uint8_t* camera)
{
    const auto span = std::span<const std::uint8_t>(camera, cameraedit::kCameraSize);
    const stereo::Matrix34 matrix = stereo::ReadMatrix(span);
    // Row-major Matrix34, engine forward is +Y with Z up, so the forward axis is
    // column 1: elements 1, 5 and 9.
    return Vec3{matrix[1], matrix[5], matrix[9]};
}

float AngleBetweenDegrees(const Vec3& a, const Vec3& b)
{
    const float lengthA = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    const float lengthB = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    if (lengthA <= 0.0f || lengthB <= 0.0f) {
        return -1.0f;
    }
    float cosine = (a.x * b.x + a.y * b.y + a.z * b.z) / (lengthA * lengthB);
    cosine = cosine > 1.0f ? 1.0f : (cosine < -1.0f ? -1.0f : cosine);
    return std::acos(cosine) * 57.29577951f;
}

void* __fastcall CreateGeneralPassObserved(
    void* out, const std::uint8_t* camera, std::uint32_t flags, std::uint32_t aux)
{
    if (gEnabled.load(std::memory_order_acquire) && camera != nullptr) {
        Vec3 written{};
        if (LastWrittenCameraForward(written)) {
            const Vec3 seen = ForwardOf(camera);
            const float degrees = AngleBetweenDegrees(written, seen);
            if (degrees >= 0.0f) {
                const auto milli = static_cast<unsigned long long>(degrees * 1000.0f);
                gLastDifference.store(milli, std::memory_order_relaxed);
                unsigned long long previousMax = gMaxDifference.load(std::memory_order_relaxed);
                while (milli > previousMax &&
                       !gMaxDifference.compare_exchange_weak(previousMax, milli)) {
                }
                // A degree of tolerance: the edit and this call are a moment
                // apart and the head is moving, so exact equality would report
                // disagreement for a reason that is not the question being asked.
                if (degrees < 1.0f) {
                    gAgreements.fetch_add(1, std::memory_order_relaxed);
                } else {
                    gDisagreements.fetch_add(1, std::memory_order_relaxed);
                }
                gSamples.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    const CreateGeneralPassFn original = gOriginal.load(std::memory_order_acquire);
    return original != nullptr ? original(out, camera, flags, aux) : nullptr;
}

bool EnsureHook()
{
    if (gHookCreated) {
        return true;
    }
    const engine::Landmark* const landmark = FindLandmark("pass.create_general");
    if (landmark == nullptr) {
        Log("result=unavailable detail=landmark_missing");
        return false;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + landmark->rva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    landmark->expected.data(), landmark->expected.size()) != 0) {
        Log("result=unavailable detail=prologue_mismatch");
        return false;
    }

    gTarget = reinterpret_cast<void*>(target);
    CreateGeneralPassFn original = nullptr;
    MH_STATUS status = MH_CreateHook(
        gTarget, reinterpret_cast<void*>(&CreateGeneralPassObserved),
        reinterpret_cast<void**>(&original));
    if (status != MH_OK) {
        std::ostringstream line;
        line << "result=failed detail=create_hook minhook=" << MH_StatusToString(status);
        Log(line.str());
        return false;
    }
    gOriginal.store(original, std::memory_order_release);

    status = MH_EnableHook(gTarget);
    if (status != MH_OK) {
        std::ostringstream line;
        line << "result=failed detail=enable_hook minhook=" << MH_StatusToString(status);
        Log(line.str());
        MH_RemoveHook(gTarget);
        return false;
    }
    gHookCreated = true;
    Log("result=0 detail=hook_installed target=CreateGeneralPassRenderingInfo");
    return true;
}

} // namespace

DWORD SetPassCameraProbeEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        if (!EnsureHook()) {
            return 1;
        }
        gSamples.store(0, std::memory_order_relaxed);
        gAgreements.store(0, std::memory_order_relaxed);
        gDisagreements.store(0, std::memory_order_relaxed);
        gLastDifference.store(0, std::memory_order_relaxed);
        gMaxDifference.store(0, std::memory_order_relaxed);
    }
    gEnabled.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (on ? "1" : "0"));
    return 0;
}

unsigned long long PassCameraSamples() { return gSamples.load(std::memory_order_relaxed); }
unsigned long long PassCameraAgreements() { return gAgreements.load(std::memory_order_relaxed); }
unsigned long long PassCameraDisagreements() { return gDisagreements.load(std::memory_order_relaxed); }
unsigned long long PassCameraLastDifferenceMillidegrees()
{
    return gLastDifference.load(std::memory_order_relaxed);
}
unsigned long long PassCameraMaxDifferenceMillidegrees()
{
    return gMaxDifference.load(std::memory_order_relaxed);
}

} // namespace preyvr::dll
