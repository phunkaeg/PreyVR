#include "NearViewStereo.h"
#include "MinHookInit.h"

#include "CameraEditHook.h"
#include "Logger.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <mutex>
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
// The render view's own current CCamera, stored by the view-info builder
// 0x180FB2AC0 (`param_1[1] = param_3`, caller passing renderView + 0x11A0).
// This is the provenance anchor that replaces the game-thread eye global.
constexpr std::size_t kViewInfoCamera = 0x08;

void* gTarget = nullptr;
std::atomic<PackViewInfoFn> gOriginal{nullptr};
bool gInstalled = false;

std::atomic<bool> gEnabled{false};
std::atomic<bool> gZeroDelta{false};
std::atomic<unsigned long long> gReentered{0};
std::atomic<unsigned long long> gNoProvenance{0};

// **The near view-projection is a shared buffer, so the edit must not nest.**
// This hook snapshots it, offsets it, calls the original and restores. If the
// original re-enters this hook with the same buffer, the inner call snapshots a
// matrix that is ALREADY offset for this eye and offsets it a second time: the
// weapon lands at twice the half-IPD, in the correct direction for each eye,
// only on frames that take the nested path. A wearer described precisely that on
// 2026-09-08 -- "the flicker in the left eye flickers the weapon model FURTHER
// to the left... almost like the offset is applying twice occasionally."
//
// Skipping the inner edit is the correct answer rather than a compromise: the
// buffer is already offset for this eye, so the nested draw is already right.
// Thread-local because re-entrancy is by definition on one stack; separate
// render jobs carry their own view info and do not share this pointer.
thread_local void* tEditing = nullptr;

// --- the cross-thread double-offset (H-022) ---------------------------------
//
// The thread-local guard above catches re-entrancy on one stack and measured
// **zero** hits, while the artefact continued. A wearer then supplied the
// decisive observation: the flicker appears "when facing the more complex areas
// of the level... when facing empty space its not flickering". Load-dependent
// means concurrency, not nesting.
//
// The near view-projection is a shared buffer and this hook runs ~190 times per
// frame across the engine's render jobs. Two threads on the same buffer race:
// A snapshots it clean and offsets it; B snapshots **A's already-offset value**
// and offsets it again, landing at twice the half-IPD in the correct direction.
// A busier scene runs more jobs in parallel, so collisions rise with complexity
// exactly as reported. The steady mirage is a pass that always runs alongside
// the main draw and therefore collides every frame.
//
// **Idempotence rather than locking.** Serialising would mean holding a lock
// across the original call -- across real engine rendering -- which trades a
// cosmetic artefact for a frame-rate one. Instead, remember the exact
// translation row we last wrote for each buffer and eye; if an incoming matrix
// already carries it, the offset is in place and this call must forward it
// untouched. That is correct for the racing thread (the buffer it renders from
// really is offset for this eye) and it also catches a copied matrix, which was
// the other surviving candidate.
//
// Exact float comparison is deliberate: these are the bits we wrote, reaching
// us by memcpy. A false positive needs the engine to independently produce a
// four-float row identical to our last written one.
struct WrittenRow {
    const void* viewInfo;
    int eye;
    float row[4];
};
constexpr unsigned int kWrittenSlots = 16;
std::array<WrittenRow, kWrittenSlots> gWritten{};
unsigned int gWrittenNext = 0;
std::mutex gWrittenMutex;
std::atomic<unsigned long long> gAlreadyOffset{0};

// Held only around the table, never across the original call.
bool RowAlreadyWritten(const void* viewInfo, int eye, const float* row)
{
    std::lock_guard lock(gWrittenMutex);
    for (const auto& slot : gWritten) {
        if (slot.viewInfo == viewInfo && slot.eye == eye &&
            slot.row[0] == row[0] && slot.row[1] == row[1] &&
            slot.row[2] == row[2] && slot.row[3] == row[3]) {
            return true;
        }
    }
    return false;
}

void RememberWrittenRow(const void* viewInfo, int eye, const float* row)
{
    std::lock_guard lock(gWrittenMutex);
    for (auto& slot : gWritten) {
        if (slot.viewInfo == viewInfo && slot.eye == eye) {
            slot.row[0] = row[0]; slot.row[1] = row[1];
            slot.row[2] = row[2]; slot.row[3] = row[3];
            return;
        }
    }
    auto& slot = gWritten[gWrittenNext % kWrittenSlots];
    ++gWrittenNext;
    slot.viewInfo = viewInfo; slot.eye = eye;
    slot.row[0] = row[0]; slot.row[1] = row[1];
    slot.row[2] = row[2]; slot.row[3] = row[3];
}

// --- lineage diagnostic (H-022) --------------------------------------------
//
// The wearer's zero-delta test proved both the steady ghost and the occasional
// flicker scale with OUR delta, and `nearReentered` proved the hook never
// nests. What remains is that the engine copies an already-offset matrix into a
// second view-info and hands it back to us later in the same frame, so the copy
// is offset twice. Different pointers, so the re-entrancy guard cannot see it.
//
// This records, per frame, the distinct view-info pointers we edit and the
// translation row we FOUND on each (pre-edit, in micrometres). If a later
// pointer arrives already carrying a delta, its row will differ from the clean
// one by exactly the half-IPD, which names the copy without guessing.
struct LineageSlot {
    const void* viewInfo;
    int foundMicro[4];
    int eye;
    unsigned int edits;
};
constexpr unsigned int kLineageSlots = 12;
std::array<LineageSlot, kLineageSlots> gLineage{};
std::atomic<unsigned int> gLineageUsed{0};
std::atomic<bool> gLineageArmed{false};
std::mutex gLineageMutex;

void RecordLineage(const void* viewInfo, const float* row, int eye)
{
    if (!gLineageArmed.load(std::memory_order_acquire)) { return; }
    std::lock_guard lock(gLineageMutex);
    unsigned int used = gLineageUsed.load(std::memory_order_relaxed);
    for (unsigned int i = 0; i < used; ++i) {
        if (gLineage[i].viewInfo == viewInfo && gLineage[i].eye == eye) {
            ++gLineage[i].edits;
            return;
        }
    }
    if (used >= kLineageSlots) { return; }
    gLineage[used].viewInfo = viewInfo;
    gLineage[used].eye = eye;
    gLineage[used].edits = 1;
    for (int j = 0; j < 4; ++j) {
        gLineage[used].foundMicro[j] = static_cast<int>(row[j] * 1.0e6f);
    }
    gLineageUsed.store(used + 1, std::memory_order_release);
}

struct EditClaim {
    void* previous;
    explicit EditClaim(void* viewInfo) : previous(tEditing) { tEditing = viewInfo; }
    ~EditClaim() { tEditing = previous; }
};
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
// POD only, both of them: `__try` cannot appear in a function that requires
// object unwinding, and the caller below builds Vec3/Matrix34/span. This rake
// has now been hit three times in this project.
bool ReadViewInfoCamera(const std::uint8_t* viewInfo, void** out)
{
    __try {
        *out = *reinterpret_cast<void* const*>(viewInfo + kViewInfoCamera);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadCameraBytes(const void* camera, std::uint8_t* out, std::size_t size)
{
    __try {
        std::memcpy(out, camera, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

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

    // Which eye this VIEW-INFO belongs to, resolved from its own camera.
    //
    // **Not `LastRenderedEye()`.** That is a mutable game-thread global, and
    // CameraEditHook.h says in its own words that the game and render threads
    // run about a frame apart, so a view-info queued earlier can be packed
    // while the global already names the next eye. Every tag on that path is
    // individually valid, which is exactly why `nearNoEye`, `nearRefused`,
    // `nearReentered` and `nearAlreadyOffset` could all read zero while the
    // wrong eye was used (H-022).
    //
    // The view-info's `+0x08` is the render view's own current CCamera --
    // verified by reading the builder `0x180FB2AC0`, whose first act is
    // `param_1[1] = param_3` with the caller passing `renderView + 0x11A0`.
    // That camera is a by-value copy of the one the game thread built, so
    // matching its position against the published eye records identifies the
    // eye from the data being rendered, and supplies the right axis that
    // belongs to it rather than a newer head orientation.
    int eye = -1;
    Vec3 right{};
    {
        void* sourceCamera = nullptr;
        std::array<std::uint8_t, engine::CameraLayout::size> cameraBytes{};
        if (!ReadViewInfoCamera(viewInfo, &sourceCamera) || sourceCamera == nullptr ||
            !ReadCameraBytes(sourceCamera, cameraBytes.data(), cameraBytes.size())) {
            gNoProvenance.fetch_add(1, std::memory_order_relaxed);
            return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
        }
        const stereo::Matrix34 m = stereo::ReadMatrix(cameraBytes);
        const float position[3] = {m[3], m[7], m[11]};
        BuiltEye record{};
        if (!FindBuiltEyeByPosition(position, record)) {
            // Refuse rather than fall back to the global. A guessed eye is the
            // defect this replaces, and an un-offset near pass is a smaller,
            // visible error than a confidently wrong one.
            gNoProvenance.fetch_add(1, std::memory_order_relaxed);
            return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
        }
        eye = record.eye;
        right = Vec3{record.right[0], record.right[1], record.right[2]};
    }
    if (eye != 0 && eye != 1) {
        gNoEye.fetch_add(1, std::memory_order_relaxed);
        return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
    }
    {
        const float length = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
        if (!std::isfinite(length) || length < 0.9f || length > 1.1f) {
            gRefused.fetch_add(1, std::memory_order_relaxed);
            return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
        }
    }

    // Already offset for this eye by an enclosing call on this stack: forward it
    // untouched rather than offsetting the same matrix twice.
    if (tEditing == static_cast<void*>(viewInfo)) {
        gReentered.fetch_add(1, std::memory_order_relaxed);
        return original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
    }

    // Already offset for this eye -- by an enclosing call on this stack, or by
    // another render-job thread racing us on the same shared buffer. Forward it
    // untouched: the matrix it will render from is already correct.
    if (RowAlreadyWritten(static_cast<const void*>(viewInfo), eye, nearVp + 12)) {
        gAlreadyOffset.fetch_add(1, std::memory_order_relaxed);
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
    RememberWrittenRow(static_cast<const void*>(viewInfo), eye, nearVp + 12);
    RecordLineage(static_cast<const void*>(viewInfo), original_ + 12, eye);
    gLastDeltaMicrometres.store(static_cast<int>(signed_ * 1.0e6f), std::memory_order_relaxed);
    gApplied.fetch_add(1, std::memory_order_relaxed);

    void* result = nullptr;
    {
        const EditClaim claim(static_cast<void*>(viewInfo));
        result = original != nullptr ? original(owner, viewInfo, third, fourth) : nullptr;
    }

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
    // Shared and idempotent: without it MH_CreateHook returns
    // MH_ERROR_NOT_INITIALIZED and the feature reports "unavailable" for a
    // reason unrelated to itself. See MinHookInit.h.
    EnsureMinHook();
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
unsigned long long NearViewReenteredCount() { return gReentered.load(std::memory_order_relaxed); }
unsigned long long NearViewAlreadyOffsetCount() { return gAlreadyOffset.load(std::memory_order_relaxed); }
unsigned long long NearViewNoProvenanceCount() { return gNoProvenance.load(std::memory_order_relaxed); }

DWORD ArmNearViewLineage(unsigned int enabled)
{
    std::lock_guard lock(gLineageMutex);
    gLineageUsed.store(0, std::memory_order_release);
    gLineage = {};
    gLineageArmed.store(enabled != 0u, std::memory_order_release);
    Log(std::string("result=0 detail=lineage_armed value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD DumpNearViewLineage()
{
    std::lock_guard lock(gLineageMutex);
    const unsigned int used = gLineageUsed.load(std::memory_order_acquire);
    if (used == 0) { Log("result=refused detail=no_lineage note=arm_it_first"); return 1; }
    for (unsigned int i = 0; i < used; ++i) {
        char line[220];
        std::snprintf(line, sizeof(line),
                      "lineage[%u] viewInfo=0x%llx eye=%d edits=%u foundUm=%d,%d,%d,%d",
                      i, static_cast<unsigned long long>(
                             reinterpret_cast<std::uintptr_t>(gLineage[i].viewInfo)),
                      gLineage[i].eye, gLineage[i].edits,
                      gLineage[i].foundMicro[0], gLineage[i].foundMicro[1],
                      gLineage[i].foundMicro[2], gLineage[i].foundMicro[3]);
        Log(line);
    }
    Log("result=0 detail=lineage_dumped count=" + std::to_string(used));
    return 0;
}
int NearViewLastDeltaMicrometres() { return gLastDeltaMicrometres.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
