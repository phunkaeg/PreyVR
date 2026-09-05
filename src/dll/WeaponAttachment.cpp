#include "WeaponAttachment.h"

#include "HeadTrackingHook.h"
#include "Logger.h"
#include "XrInput.h"
#include "preyvr/EngineMap.h"
#include "preyvr/MotionController.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/VrMath.h"

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
    lifecycle::Log("preyvr_weapon_attach " + line);
}

// R-024 CArkWeapon::AttachToHand. The equipped weapon resolves its IAttachment*
// into CArkWeapon+0x2B0 and installs a binding through attachment vtable +0xD8.
using AttachToHandFn = void*(__fastcall*)(void*, void*, void*, void*);

constexpr std::uintptr_t kAttachToHandRva = 0x16914F0;
constexpr std::array<std::uint8_t, 28> kAttachToHandPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
    0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x81,
    0xEC, 0x90, 0x00, 0x00,
};

constexpr std::size_t kWeaponAttachmentField = 0x2B0;   // R-024

// IAttachment vtable, aligned against R-024's independently recorded +0xD8
// (AddBinding, index 27).
constexpr std::size_t kSetAttAbsoluteDefault = 9 * 8;   // +0x48
constexpr std::size_t kGetAttAbsoluteDefault = 10 * 8;  // +0x50

// CryEngine QuatT: Quat(x,y,z,w) then Vec3(x,y,z), 28 bytes.
struct QuatT {
    float x, y, z, w;
    float px, py, pz;
};

using SetAbsFn = void(__fastcall*)(void* attachment, const QuatT* value);
using GetAbsFn = const QuatT*(__fastcall*)(void* attachment);

void* gTarget = nullptr;
std::atomic<AttachToHandFn> gOriginal{nullptr};
bool gInstalled = false;

std::atomic<bool> gObserving{false};
std::atomic<bool> gOffsetEnabled{false};
std::atomic<unsigned long long> gAttachment{0};
std::atomic<float> gOffsetX{0.0f}, gOffsetY{0.0f}, gOffsetZ{0.0f};
std::atomic<unsigned long long> gApplied{0}, gRefused{0};
QuatT gBaseline{};
std::atomic<bool> gHaveBaseline{false};

std::atomic<bool> gRotationDrive{false};
std::atomic<bool> gRotationCalibrated{false};
Quaternion gZeroAim{};
std::atomic<unsigned long long> gRotationApplied{0}, gRotationNoPose{0};

Quaternion Conjugate(const Quaternion& q) { return Quaternion{-q.x, -q.y, -q.z, q.w}; }

// The controller's aim rotation in world terms, through the same reference the
// aim lane uses. Origin-anchored: the camera is offset per eye by the synthetic
// stereo, and anchoring there leaks half an IPD into the result (FAIL-HAND-037).
bool ControllerAimRotation(Quaternion& out, float& bodyYaw)
{
    ControllerState state{};
    if (!TryGetControllerState(Hand::right, state)) {
        return false;
    }
    if (!state.aimValidity.orientationTracked) {
        return false;
    }
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
    bodyYaw = stereo::CameraYawOf(m) - HeadTrackingReferenceYaw();
    stereo::ReferenceFrame reference{};
    reference.yawRadians = bodyYaw;
    reference.worldPosition = Vec3{};
    out = controller::ControllerPoseInWorld(reference, state.aimPose).orientation;
    return true;
}

bool ReadMount(void* attachment, QuatT& out)
{
    if (attachment == nullptr) {
        return false;
    }
    __try {
        auto* const vtable = *reinterpret_cast<std::uint8_t**>(attachment);
        const auto get = *reinterpret_cast<GetAbsFn*>(vtable + kGetAttAbsoluteDefault);
        const QuatT* const value = get(attachment);
        if (value == nullptr) {
            return false;
        }
        out = *value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    // The data names itself: a mount is a unit quaternion and a finite
    // translation. Anything else means this pointer is not an attachment, and a
    // write would go somewhere unknown.
    const float length = std::sqrt(out.x*out.x + out.y*out.y + out.z*out.z + out.w*out.w);
    if (!std::isfinite(length) || length < 0.9f || length > 1.1f) {
        return false;
    }
    return std::isfinite(out.px) && std::isfinite(out.py) && std::isfinite(out.pz);
}

bool WriteMount(void* attachment, const QuatT& value)
{
    __try {
        auto* const vtable = *reinterpret_cast<std::uint8_t**>(attachment);
        const auto set = *reinterpret_cast<SetAbsFn*>(vtable + kSetAttAbsoluteDefault);
        set(attachment, &value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* __fastcall AttachToHandObserved(void* weapon, void* a, void* b, void* c)
{
    const AttachToHandFn original = gOriginal.load(std::memory_order_acquire);
    void* const result = original != nullptr ? original(weapon, a, b, c) : nullptr;
    // **After the original**, because the attachment is resolved *by* this call --
    // reading +0x2B0 on entry would capture whatever the previous weapon left.
    if (gObserving.load(std::memory_order_acquire) && weapon != nullptr) {
        __try {
            void* const attachment = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(weapon) + kWeaponAttachmentField);
            if (attachment != nullptr) {
                QuatT mount{};
                if (ReadMount(attachment, mount)) {
                    gAttachment.store(reinterpret_cast<unsigned long long>(attachment),
                                      std::memory_order_release);
                    gBaseline = mount;
                    gHaveBaseline.store(true, std::memory_order_release);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return result;
}

bool Install()
{
    if (gInstalled) {
        return true;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + kAttachToHandRva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    kAttachToHandPrologue.data(), kAttachToHandPrologue.size()) != 0) {
        Log("result=unavailable detail=attach_prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    AttachToHandFn original = nullptr;
    if (MH_CreateHook(gTarget, reinterpret_cast<void*>(&AttachToHandObserved),
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
    Log("result=0 detail=hook_installed target=CArkWeapon::AttachToHand");
    return true;
}

} // namespace

DWORD SetWeaponAttachmentObserving(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on && !Install()) {
        return 1;
    }
    gObserving.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=observing value=") + (on ? "1" : "0") +
        " note=re-equip a weapon to capture");
    return 0;
}

unsigned long long WeaponAttachmentPointer() { return gAttachment.load(std::memory_order_acquire); }

int WeaponMountPositionMillimetres(unsigned int axis)
{
    if (!gHaveBaseline.load(std::memory_order_acquire)) {
        return 0;
    }
    const float v = axis == 0 ? gBaseline.px : axis == 1 ? gBaseline.py : gBaseline.pz;
    return static_cast<int>(v * 1000.0f);
}

int WeaponMountQuaternionMilli(unsigned int component)
{
    if (!gHaveBaseline.load(std::memory_order_acquire)) {
        return 0;
    }
    const float v = component == 0 ? gBaseline.x : component == 1 ? gBaseline.y
                  : component == 2 ? gBaseline.z : gBaseline.w;
    return static_cast<int>(v * 1000.0f);
}

DWORD SetWeaponOffsetMillimetres(int x, int y, int z)
{
    // A weapon does not sit a metre from its own mount.
    const auto tooBig = [](int v) { return v < -1000 || v > 1000; };
    if (tooBig(x) || tooBig(y) || tooBig(z)) {
        return 1;
    }
    gOffsetX.store(static_cast<float>(x) / 1000.0f, std::memory_order_relaxed);
    gOffsetY.store(static_cast<float>(y) / 1000.0f, std::memory_order_relaxed);
    gOffsetZ.store(static_cast<float>(z) / 1000.0f, std::memory_order_relaxed);
    Log("result=0 detail=offset_mm x=" + std::to_string(x) + " y=" + std::to_string(y) +
        " z=" + std::to_string(z));
    return 0;
}

DWORD SetWeaponOffsetEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    auto* const attachment =
        reinterpret_cast<void*>(gAttachment.load(std::memory_order_acquire));
    if (attachment == nullptr || !gHaveBaseline.load(std::memory_order_acquire)) {
        Log("result=refused detail=no_attachment note=equip_or_reequip_a_weapon");
        return 2;
    }
    QuatT value = gBaseline;
    if (on) {
        value.px += gOffsetX.load(std::memory_order_relaxed);
        value.py += gOffsetY.load(std::memory_order_relaxed);
        value.pz += gOffsetZ.load(std::memory_order_relaxed);
    }
    // Disarming writes the captured baseline back, so this is reversible by
    // construction rather than by remembering what it used to be.
    if (!WriteMount(attachment, value)) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        Log("result=failed detail=write_mount");
        return 3;
    }
    gOffsetEnabled.store(on, std::memory_order_release);
    gApplied.fetch_add(1, std::memory_order_relaxed);
    Log(std::string("result=0 detail=offset_enabled value=") + (on ? "1" : "0"));
    return 0;
}

DWORD SetWeaponRotationDrive(unsigned int enabled)
{
    gRotationDrive.store(enabled != 0u, std::memory_order_release);
    gRotationNoPose.store(0, std::memory_order_relaxed);
    Log(std::string("result=0 detail=rotation_drive value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD CalibrateWeaponRotation()
{
    Quaternion aim{};
    float yaw = 0.0f;
    if (!ControllerAimRotation(aim, yaw)) {
        Log("result=refused detail=aim_pose_untracked");
        return 1;
    }
    if (!gHaveBaseline.load(std::memory_order_acquire)) {
        Log("result=refused detail=no_mount note=equip_or_reequip_a_weapon");
        return 2;
    }
    gZeroAim = aim;
    gRotationCalibrated.store(true, std::memory_order_release);
    Log("result=0 detail=rotation_calibrated");
    return 0;
}

void UpdateWeaponMountFromController()
{
    if (!gRotationDrive.load(std::memory_order_acquire) ||
        !gRotationCalibrated.load(std::memory_order_acquire) ||
        !gHaveBaseline.load(std::memory_order_acquire)) {
        return;
    }
    auto* const attachment =
        reinterpret_cast<void*>(gAttachment.load(std::memory_order_acquire));
    if (attachment == nullptr) {
        return;
    }
    Quaternion aim{};
    float yaw = 0.0f;
    if (!ControllerAimRotation(aim, yaw)) {
        gRotationNoPose.fetch_add(1, std::memory_order_relaxed);
        return;   // untracked: leave the engine's own mount alone
    }
    // The tracked delta since calibration, in world terms.
    const Quaternion delta = Normalize(Multiply(aim, Conjugate(gZeroAim)));

    QuatT value = gBaseline;
    // **currentController * authoredMount.** The mount is the basis being changed,
    // so it goes on the RIGHT. Composing it the other way is the failure that
    // looks like a sign error and survives every sign flip.
    const Quaternion composed = Normalize(Multiply(delta, Quaternion{
        gBaseline.x, gBaseline.y, gBaseline.z, gBaseline.w}));
    if (!std::isfinite(composed.x) || !std::isfinite(composed.y) ||
        !std::isfinite(composed.z) || !std::isfinite(composed.w)) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    value.x = composed.x; value.y = composed.y; value.z = composed.z; value.w = composed.w;
    value.px += gOffsetX.load(std::memory_order_relaxed);
    value.py += gOffsetY.load(std::memory_order_relaxed);
    value.pz += gOffsetZ.load(std::memory_order_relaxed);
    if (WriteMount(attachment, value)) {
        gRotationApplied.fetch_add(1, std::memory_order_relaxed);
    } else {
        gRefused.fetch_add(1, std::memory_order_relaxed);
    }
}

unsigned long long WeaponOffsetAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long WeaponOffsetRefusedCount() { return gRefused.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
