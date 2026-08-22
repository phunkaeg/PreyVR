#pragma once

#include "preyvr/VrMath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// Read-only capture of engine facts that only a running Prey can supply.
//
// Every offset here is verified against the installed PreyDll.dll and recorded
// in docs/ADDRESS_REGISTRY.md (R-039 to R-055). Nothing in this header writes to
// game memory, and the capture is designed so a single live session answers the
// questions queued for the next three milestones at once -- see
// docs/LIVE_CAPTURE_PLAN.md. Live-host time is the scarce resource; the point of
// this module is to spend it once.
namespace preyvr::snapshot {

// ---------------------------------------------------------------------------
// Verified layout constants. These duplicate nothing: EngineMap.h owns the
// offsets, this header owns the *shape* the bytes decode into.
// ---------------------------------------------------------------------------

inline constexpr std::size_t kCameraSize = 0x240;
inline constexpr std::size_t kRenderCameraSize = 0x48;

// CSystem vtable and C3DEngine vtable RVAs. Reading a live object's first qword
// and comparing against these proves we are pointed at the class we think we
// are -- the cheapest possible identity check, and the one that catches a wrong
// gEnv offset immediately rather than three reads later.
inline constexpr std::uintptr_t kSystemVtableRva = 0x1D9B9C8;
inline constexpr std::uintptr_t kThreeDEngineVtableRva = 0x1C912A0;
inline constexpr std::uintptr_t kRenderWorldRva = 0x21F520;
inline constexpr std::uintptr_t kThreeDEngineRenderWorldSlot = 0x18;

// ---------------------------------------------------------------------------
// Typed views
// ---------------------------------------------------------------------------

// The fields of CCamera the VR lanes actually need. Decoded from a 0x240 blob
// rather than mirrored as a struct, because the engine's own layout contains
// ~0x150 bytes of cached frustum data we never want to depend on.
struct CameraView {
    // Matrix34, row-major 3x4. Translation is column 3: {m[3], m[7], m[11]}.
    std::array<float, 12> matrix{};
    float fov = 0.0f;
    std::int32_t width = 0;
    std::int32_t height = 0;
    float projectionRatio = 0.0f;
    float nearPlane = 0.0f; // m_edge_nlt.y
    float farPlane = 0.0f;  // m_edge_flt.y
    // The four asymmetric frustum shifts. CRenderView::SetCamera folds these
    // into the render frustum, so these are the fields a per-eye projection
    // writes. Expected to be all-zero on a stock desktop frame; a non-zero
    // baseline would be a genuine surprise worth investigating before we write.
    float asymLeft = 0.0f;
    float asymRight = 0.0f;
    float asymBottom = 0.0f;
    float asymTop = 0.0f;
};

// CRenderCamera, the derived block the renderer consumes. fW* are frustum
// tangents -- the same parameterisation as OpenXR's XrFovf.
struct RenderCameraView {
    Vec3 axisX{};
    Vec3 axisY{};
    Vec3 axisZ{};
    Vec3 origin{};
    float frustumLeft = 0.0f;
    float frustumRight = 0.0f;
    float frustumBottom = 0.0f;
    float frustumTop = 0.0f;
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
};

std::optional<CameraView> DecodeCamera(std::span<const std::uint8_t> bytes);
std::optional<RenderCameraView> DecodeRenderCamera(std::span<const std::uint8_t> bytes);

// Acceptance predicates. A live capture should produce a verdict, not just
// numbers -- otherwise reading it is another round-trip.
bool IsPlausible(const CameraView& camera);
bool IsPlausible(const RenderCameraView& camera);

// Does the render camera agree with the source camera it was derived from?
// CRenderView::SetCamera computes fWL = asymL - t*ratio and fWT = t + asymT
// where t = tan(fov/2), so this recomputes that and compares. Disagreement
// means either our layout is wrong or the view was built from a different
// camera -- both worth knowing before writing anything.
bool RenderCameraMatchesSource(
    const CameraView& source,
    const RenderCameraView& derived,
    float tolerance = 0.001f);

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

// Supplied by the DLL side, which guards the read. Must return false rather
// than throw or fault on an unreadable address.
using Reader = bool (*)(std::uintptr_t address, std::span<std::uint8_t> out, void* context);

struct PointerFact {
    std::uintptr_t value = 0;
    bool read = false;      // the read itself succeeded
    bool plausible = false; // non-null and correctly aligned
};

struct Snapshot {
    std::uintptr_t moduleBase = 0;

    // Root objects, each resolved from the one before it.
    std::uintptr_t globalEnvironment = 0; // moduleBase + 0x224D980 (R-044)
    PointerFact system;                   // gEnv->pSystem   (+0xE0)
    PointerFact renderer;                 // gEnv->pRenderer (+0x120)
    PointerFact threeDEngine;             // gEnv->p3DEngine (+0x08)
    PointerFact rendererSingleton;        // moduleBase + 0x2B3E8E0 (R-005)

    // Identity checks: does each object's vtable pointer match the RVA we
    // resolved statically? These turn "we read some bytes" into "we are looking
    // at CSystem".
    bool systemVtableMatches = false;
    bool threeDEngineVtableMatches = false;
    bool renderWorldSlotMatches = false;

    // gEnv->pRenderer and the CD3D9Renderer singleton should be the same
    // object. If they are not, our renderer model is wrong.
    bool rendererSingletonAgrees = false;

    // CSystem::m_ViewCamera at +0x788 (R-040). The camera that H-008 is about.
    std::optional<CameraView> viewCamera;
    bool viewCameraPlausible = false;

    // Filled in only when every prior step succeeded. A partial snapshot is
    // still worth logging -- it localises the first failure.
    bool complete = false;
};

Snapshot Capture(std::uintptr_t moduleBase, Reader read, void* context);

// A stable, greppable one-line-per-fact rendering for the smoke log. Callers
// pass a sink so this stays free of I/O and testable.
using LineSink = void (*)(std::string_view line, void* context);
void Report(const Snapshot& snapshot, LineSink sink, void* context);

} // namespace preyvr::snapshot
