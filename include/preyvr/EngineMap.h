#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace preyvr::engine {

inline constexpr std::string_view kSupportedPreyDllSha256 =
    "7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7";

struct ArkPlayerLayout {
    static constexpr std::uintptr_t interaction = 0xAC8;
    static constexpr std::uintptr_t camera = 0x12A0;
    static constexpr std::uintptr_t cachedReticleOrigin = 0x17D4;
    static constexpr std::uintptr_t cachedReticleDirection = 0x17E0;
    static constexpr std::uintptr_t reticleScreenPosition = 0x17EC;
};

struct ArkPlayerInteractionLayout {
    static constexpr std::uintptr_t interactionInfo = 0x128;
    static constexpr std::uintptr_t targetSelector = 0x190;
    static constexpr std::uintptr_t usableEntityId = 0x46C;
};

struct ArkPlayerTargetSelectorLayout {
    static constexpr std::uintptr_t interactDistance = 0x0C;
    static constexpr std::uintptr_t forceSelectEntityId = 0x1C;
    static constexpr std::uintptr_t candidatesBegin = 0x20;
    static constexpr std::uintptr_t candidatesEnd = 0x28;
    static constexpr std::size_t candidateRecordSize = 0x70;
    static constexpr std::uintptr_t candidateEntityId = 0x68;
};

struct RendererLayout {
    static constexpr std::uintptr_t singletonPointerRva = 0x2B3E8E0;
    static constexpr std::uintptr_t swapchain = 0xAE88;
    static constexpr std::uintptr_t device = 0xAF28;

    // R-033: SRenderPipeline::m_pRenderViews[2][2], four CRenderView* at
    // +0x6F38 + (nThreadID*2 + bRecursive)*8. The indexing arithmetic is
    // confirmed by emulating R-032; the base offset is static-only and its
    // registry entry names its own acceptance test -- all four non-null, and
    // [t][1] distinct from [t][0].
    static constexpr std::uintptr_t renderViewPool = 0x6F38;
    static constexpr std::size_t renderViewPoolCount = 4;

    // Vtable slots used by CreateGeneralPassRenderingInfo (R-071), read out of
    // its decompilation. `getRenderViewForThread` is the virtual entry to the
    // pool above; its concrete target is the already-gated landmark
    // `view.get_for_thread` at RVA 0xFE5620, which disassembles to a plain
    // indexed load and a ret -- no allocation, so calling it has no side effect.
    static constexpr std::uintptr_t vtableGetRenderViewForThread = 0x198;
    static constexpr std::uintptr_t vtableQuery = 0x888;
    // Selector the engine passes to the query above to obtain the fill slot.
    static constexpr int queryRenderThreadList = 6;
    // IRenderView::EViewType, per the pool's [nThreadID][bRecursive] indexing.
    static constexpr int viewTypeDefault = 0;
    static constexpr int viewTypeRecursive = 1;

    // R-026: per-frame render view block, live-verified by ReGenny probe.
    // Two slots of stride 0x328 selected by the index at +0x499C, with a
    // CRenderCamera at +0x240 within each block.
    static constexpr std::uintptr_t frameBlock = 0x4A08;
    static constexpr std::uintptr_t frameBlockStride = 0x328;
    static constexpr std::uintptr_t frameBlockRenderCamera = 0x240;
    static constexpr std::uintptr_t frameSlotIndex = 0x499C;
    static constexpr std::size_t frameBlockCount = 2;
};

// R-053: the CRenderView vtable. Reading a pooled entry's first qword and
// comparing against this is what turns "a plausible pointer" into "a
// CRenderView" -- the same identity discipline Capture A uses for CSystem.
struct RenderViewIdentity {
    static constexpr std::uintptr_t vtableRva = 0x1DCAE00;
    static constexpr std::uintptr_t vtableSetCameraSlot = 0x40;
};

// Offsets below are verified against the installed PreyDll.dll, not merely read
// out of the Chairloader PDB headers. See docs/CCAMERA_LAYOUT.md and R-040..R-051.

// CCamera, sizeof 0x240. Confirmed by 11 offsets observed in the binary landing
// on member boundaries and by the size at which CCamera::operator= stops copying.
struct CameraLayout {
    static constexpr std::size_t size = 0x240;
    static constexpr std::uintptr_t matrix = 0x00;   // Matrix34, translation in column 3
    static constexpr std::uintptr_t fov = 0x30;      // vertical, radians
    static constexpr std::uintptr_t width = 0x38;
    static constexpr std::uintptr_t height = 0x3C;
    static constexpr std::uintptr_t projectionRatio = 0x40;
    static constexpr std::uintptr_t edgeNearLeftTop = 0x48;  // GetNearPlane() is .y at 0x4C
    static constexpr std::uintptr_t edgeFarLeftTop = 0x60;   // GetFarPlane()  is .y at 0x64
    // Asymmetric frustum shifts. CRenderView::SetCamera reads all four, so an
    // OpenXR per-eye projection is four float writes here. The engine header
    // marks them "not used for culling atm" -- expect render/cull divergence at
    // wide asymmetry until that is tested live.
    static constexpr std::uintptr_t asymLeft = 0x6C;
    static constexpr std::uintptr_t asymRight = 0x70;
    static constexpr std::uintptr_t asymBottom = 0x74;
    static constexpr std::uintptr_t asymTop = 0x78;
    // Derived state cached inside the object: writing matrix or fov directly
    // does NOT refresh these.
    static constexpr std::uintptr_t frustumPlanes = 0x10C;  // Plane[6], 16B each
    static constexpr std::uintptr_t zRangeMin = 0x1FC;
    static constexpr std::uintptr_t zRangeMax = 0x200;
};

// CSystem. GetViewCamera() is `LEA RAX,[RCX+0x788]; RET`, so the global view
// camera is a member by value rather than a pointer.
// R-061: SRenderingPassInfo, the argument RenderWorld (R-054) takes its camera
// from. Built by CreateGeneralPassRenderingInfo (R-071) into a **caller-supplied
// 64-byte buffer** -- `undefined1 local_58[64]` on CSystem::Render's stack -- so
// it is a value type constructed per call rather than a shared singleton.
//
// **That is what makes native stereo possible.** A second pass gets its own
// buffer, its own camera at +0x18 and its own render view at +0x20, so the two
// eyes never share the per-frame state whose reuse wedged the engine in F-013.
struct PassInfoLayout {
    static constexpr std::size_t size = 0x40;
    static constexpr std::uintptr_t threadSlot = 0x00;       // byte
    static constexpr std::uintptr_t flags = 0x04;
    static constexpr std::uintptr_t zoom = 0x08;
    static constexpr std::uintptr_t frameId = 0x0C;
    static constexpr std::uintptr_t previousFrameId = 0x10;
    static constexpr std::uintptr_t camera = 0x18;           // const CCamera*
    static constexpr std::uintptr_t renderView = 0x20;       // CRenderView*
    static constexpr std::uintptr_t renderViewDerived = 0x38;

    // The constant CSystem::Render passes as nRenderingFlags, and the one
    // RenderWorld is called with alongside it.
    static constexpr std::uint32_t generalPassFlags = 0x2E5DF;
    static constexpr int renderWorldFlags = 0xF;
};

struct SystemLayout {
    static constexpr std::uintptr_t gEnvPointer = 0x28;
    static constexpr std::uintptr_t viewCamera = 0x788;
    // R-059: CSystem::m_pProcess, the IProcess whose vtable slot 3 (+0x18) is
    // RenderWorld. Live-read equal to gEnv->p3DEngine, so IProcess is
    // C3DEngine's primary base and needs no pointer adjustment.
    static constexpr std::uintptr_t processPointer = 0xAB0;
    static constexpr std::uintptr_t vtableRenderWorld = 0x18;
    static constexpr std::uintptr_t vtableRva = 0x1D9B9C8;
    static constexpr std::uintptr_t pointerRva = 0x224DA60;  // gEnv->pSystem
    // ISystem vtable slots; full table in docs/SYSTEM_VTABLE.md.
    static constexpr std::uintptr_t vtableSetViewCamera = 0x380;
    static constexpr std::uintptr_t vtableGetViewCamera = 0x388;
};

// SSystemGlobalEnvironment. Layout confirmed by 24 ISystem accessors that relay
// through it, each landing on its own correctly named member.
struct GlobalEnvironmentLayout {
    static constexpr std::uintptr_t baseRva = 0x224D980;
    static constexpr std::uintptr_t threeDEngine = 0x08;
    static constexpr std::uintptr_t console = 0xC0;
    static constexpr std::uintptr_t system = 0xE0;
    static constexpr std::uintptr_t renderer = 0x120;
};

// CRenderView. SetCamera copies the camera BY VALUE into m_camera, so a camera
// written through that seam cannot alias CSystem::m_ViewCamera.
struct RenderViewLayout {
    static constexpr std::uintptr_t camera = 0x11A0;        // CCamera, by value
    static constexpr std::uintptr_t renderCamera = 0x1620;  // CRenderCamera
};

// CRenderCamera, the derived block the renderer consumes. fWL/fWR/fWB/fWT are
// frustum tangents -- the same parameterisation as OpenXR's XrFovf, so an eye
// FOV maps on by taking tan of each angle. Offsets are absolute in CRenderView.
struct RenderCameraLayout {
    static constexpr std::uintptr_t axisX = 0x1620;
    static constexpr std::uintptr_t axisY = 0x162C;
    static constexpr std::uintptr_t axisZ = 0x1638;
    static constexpr std::uintptr_t origin = 0x1644;
    static constexpr std::uintptr_t frustumLeft = 0x1650;
    static constexpr std::uintptr_t frustumRight = 0x1654;
    static constexpr std::uintptr_t frustumBottom = 0x1658;
    static constexpr std::uintptr_t frustumTop = 0x165C;
    static constexpr std::uintptr_t nearPlane = 0x1660;
    static constexpr std::uintptr_t farPlane = 0x1664;
};

struct Landmark {
    std::string_view id;
    std::string_view name;
    std::uintptr_t rva;
    std::span<const std::uint8_t> expected;
};

enum class LandmarkStatus {
    match,
    outOfRange,
    mismatch,
};

struct LandmarkValidation {
    const Landmark* landmark = nullptr;
    LandmarkStatus status = LandmarkStatus::outOfRange;
    std::size_t mismatchOffset = 0;
};

std::span<const Landmark> Landmarks();
LandmarkValidation ValidateLandmark(
    std::span<const std::uint8_t> mappedImage,
    const Landmark& landmark);
std::vector<LandmarkValidation> ValidateLandmarks(std::span<const std::uint8_t> mappedImage);
bool AllLandmarksMatch(std::span<const LandmarkValidation> results);
std::string_view ToString(LandmarkStatus status);

} // namespace preyvr::engine
