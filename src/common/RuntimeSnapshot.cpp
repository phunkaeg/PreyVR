#include "preyvr/RuntimeSnapshot.h"

#include "preyvr/EngineMap.h"

#include <array>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace preyvr::snapshot {
namespace {

using engine::CameraLayout;
using engine::GlobalEnvironmentLayout;
using engine::RenderCameraLayout;
using engine::RenderViewLayout;
using engine::RendererLayout;
using engine::SystemLayout;

// CRenderCamera offsets are recorded absolute within CRenderView; decoding a
// standalone 0x48 blob needs them relative to the block's own start.
constexpr std::uintptr_t kRcBase = RenderViewLayout::renderCamera;
constexpr std::uintptr_t kRcAxisX = RenderCameraLayout::axisX - kRcBase;
constexpr std::uintptr_t kRcAxisY = RenderCameraLayout::axisY - kRcBase;
constexpr std::uintptr_t kRcAxisZ = RenderCameraLayout::axisZ - kRcBase;
constexpr std::uintptr_t kRcOrigin = RenderCameraLayout::origin - kRcBase;
constexpr std::uintptr_t kRcLeft = RenderCameraLayout::frustumLeft - kRcBase;
constexpr std::uintptr_t kRcRight = RenderCameraLayout::frustumRight - kRcBase;
constexpr std::uintptr_t kRcBottom = RenderCameraLayout::frustumBottom - kRcBase;
constexpr std::uintptr_t kRcTop = RenderCameraLayout::frustumTop - kRcBase;
constexpr std::uintptr_t kRcNear = RenderCameraLayout::nearPlane - kRcBase;
constexpr std::uintptr_t kRcFar = RenderCameraLayout::farPlane - kRcBase;

static_assert(kRcAxisX == 0x00, "CRenderCamera starts at its first member");
static_assert(kRcFar + sizeof(float) == kRenderCameraSize, "fFar closes the block");

float LoadFloat(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::int32_t LoadInt(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    std::int32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

Vec3 LoadVec3(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return Vec3{
        LoadFloat(bytes, offset),
        LoadFloat(bytes, offset + 4),
        LoadFloat(bytes, offset + 8)};
}

bool Finite(float value)
{
    return std::isfinite(value);
}

bool Finite(Vec3 value)
{
    return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

float Length(Vec3 value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

// A live object pointer must be non-null and at least 8-byte aligned. This is
// deliberately weak: the strong check is the vtable comparison that follows.
bool PlausiblePointer(std::uintptr_t value)
{
    return value != 0 && (value % 8) == 0;
}

bool ReadPointer(Reader read, void* context, std::uintptr_t address, PointerFact& out)
{
    std::array<std::uint8_t, sizeof(std::uintptr_t)> bytes{};
    out.read = read(address, bytes, context);
    if (!out.read) {
        return false;
    }
    std::memcpy(&out.value, bytes.data(), sizeof(out.value));
    out.plausible = PlausiblePointer(out.value);
    return out.plausible;
}

// Reads the qword at `object` (its vtable pointer) and compares against the
// module-relative RVA we resolved statically.
bool VtableMatches(
    Reader read,
    void* context,
    std::uintptr_t object,
    std::uintptr_t moduleBase,
    std::uintptr_t expectedRva)
{
    PointerFact vtable{};
    if (!ReadPointer(read, context, object, vtable)) {
        return false;
    }
    return vtable.value == moduleBase + expectedRva;
}

} // namespace

std::optional<CameraView> DecodeCamera(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kCameraSize) {
        return std::nullopt;
    }

    CameraView camera{};
    for (std::size_t i = 0; i < camera.matrix.size(); ++i) {
        camera.matrix[i] = LoadFloat(bytes, CameraLayout::matrix + i * sizeof(float));
    }
    camera.fov = LoadFloat(bytes, CameraLayout::fov);
    camera.width = LoadInt(bytes, CameraLayout::width);
    camera.height = LoadInt(bytes, CameraLayout::height);
    camera.projectionRatio = LoadFloat(bytes, CameraLayout::projectionRatio);
    // GetNearPlane() is m_edge_nlt.y and GetFarPlane() is m_edge_flt.y (R-039).
    camera.nearPlane = LoadFloat(bytes, CameraLayout::edgeNearLeftTop + 4);
    camera.farPlane = LoadFloat(bytes, CameraLayout::edgeFarLeftTop + 4);
    camera.asymLeft = LoadFloat(bytes, CameraLayout::asymLeft);
    camera.asymRight = LoadFloat(bytes, CameraLayout::asymRight);
    camera.asymBottom = LoadFloat(bytes, CameraLayout::asymBottom);
    camera.asymTop = LoadFloat(bytes, CameraLayout::asymTop);
    return camera;
}

std::optional<RenderCameraView> DecodeRenderCamera(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kRenderCameraSize) {
        return std::nullopt;
    }

    RenderCameraView camera{};
    camera.axisX = LoadVec3(bytes, kRcAxisX);
    camera.axisY = LoadVec3(bytes, kRcAxisY);
    camera.axisZ = LoadVec3(bytes, kRcAxisZ);
    camera.origin = LoadVec3(bytes, kRcOrigin);
    camera.frustumLeft = LoadFloat(bytes, kRcLeft);
    camera.frustumRight = LoadFloat(bytes, kRcRight);
    camera.frustumBottom = LoadFloat(bytes, kRcBottom);
    camera.frustumTop = LoadFloat(bytes, kRcTop);
    camera.nearPlane = LoadFloat(bytes, kRcNear);
    camera.farPlane = LoadFloat(bytes, kRcFar);
    return camera;
}

bool IsPlausible(const CameraView& camera)
{
    for (const float value : camera.matrix) {
        if (!Finite(value)) {
            return false;
        }
    }
    const bool scalarsFinite = Finite(camera.fov) && Finite(camera.projectionRatio) &&
        Finite(camera.nearPlane) && Finite(camera.farPlane) && Finite(camera.asymLeft) &&
        Finite(camera.asymRight) && Finite(camera.asymBottom) && Finite(camera.asymTop);
    if (!scalarsFinite) {
        return false;
    }
    // Vertical FoV is stored in radians; anything outside (0, pi) is not a camera.
    if (camera.fov <= 0.0f || camera.fov >= 3.14159274f) {
        return false;
    }
    if (camera.width <= 0 || camera.height <= 0) {
        return false;
    }
    if (camera.projectionRatio <= 0.0f) {
        return false;
    }
    return camera.nearPlane > 0.0f && camera.farPlane > camera.nearPlane;
}

bool IsPlausible(const RenderCameraView& camera)
{
    if (!Finite(camera.axisX) || !Finite(camera.axisY) || !Finite(camera.axisZ) ||
        !Finite(camera.origin)) {
        return false;
    }
    if (!Finite(camera.frustumLeft) || !Finite(camera.frustumRight) ||
        !Finite(camera.frustumBottom) || !Finite(camera.frustumTop) ||
        !Finite(camera.nearPlane) || !Finite(camera.farPlane)) {
        return false;
    }
    // SetCamera normalises the basis before storing it.
    for (const Vec3 axis : {camera.axisX, camera.axisY, camera.axisZ}) {
        const float length = Length(axis);
        if (length < 0.99f || length > 1.01f) {
            return false;
        }
    }
    if (camera.frustumLeft >= camera.frustumRight) {
        return false;
    }
    if (camera.frustumBottom >= camera.frustumTop) {
        return false;
    }
    return camera.nearPlane > 0.0f && camera.farPlane > camera.nearPlane;
}

float RenderCameraResidual(const CameraView& source, const RenderCameraView& derived)
{
    const float tangent = std::tan(source.fov * 0.5f);
    if (!Finite(tangent)) {
        return std::numeric_limits<float>::infinity();
    }
    const float horizontal = tangent * source.projectionRatio;

    const float terms[] = {
        derived.frustumLeft - (source.asymLeft - horizontal),
        derived.frustumRight - (horizontal + source.asymRight),
        derived.frustumBottom - (source.asymBottom - tangent),
        derived.frustumTop - (tangent + source.asymTop),
        derived.nearPlane - source.nearPlane,
        derived.farPlane - source.farPlane,
    };

    float worst = 0.0f;
    for (const float term : terms) {
        if (!Finite(term)) {
            return std::numeric_limits<float>::infinity();
        }
        worst = std::max(worst, std::fabs(term));
    }
    return worst;
}

bool RenderCameraMatchesSource(
    const CameraView& source,
    const RenderCameraView& derived,
    float tolerance)
{
    return RenderCameraResidual(source, derived) <= tolerance;
}

EyeAsymmetry AsymmetryFromFovTangents(
    float tanLeft,
    float tanRight,
    float tanDown,
    float tanUp,
    float fov,
    float projectionRatio)
{
    const float tangent = std::tan(fov * 0.5f);
    const float horizontal = tangent * projectionRatio;

    EyeAsymmetry shifts{};
    shifts.left = tanLeft + horizontal;
    shifts.right = tanRight - horizontal;
    shifts.bottom = tanDown + tangent;
    shifts.top = tanUp - tangent;
    return shifts;
}

Snapshot Capture(std::uintptr_t moduleBase, Reader read, void* context)
{
    Snapshot snapshot{};
    snapshot.moduleBase = moduleBase;
    if (moduleBase == 0 || read == nullptr) {
        return snapshot;
    }

    snapshot.globalEnvironment = moduleBase + GlobalEnvironmentLayout::baseRva;

    const bool systemOk = ReadPointer(
        read, context, snapshot.globalEnvironment + GlobalEnvironmentLayout::system,
        snapshot.system);
    ReadPointer(
        read, context, snapshot.globalEnvironment + GlobalEnvironmentLayout::renderer,
        snapshot.renderer);
    const bool engineOk = ReadPointer(
        read, context, snapshot.globalEnvironment + GlobalEnvironmentLayout::threeDEngine,
        snapshot.threeDEngine);
    ReadPointer(
        read, context, moduleBase + RendererLayout::singletonPointerRva,
        snapshot.rendererSingleton);

    snapshot.rendererSingletonAgrees = snapshot.renderer.plausible &&
        snapshot.rendererSingleton.plausible &&
        snapshot.renderer.value == snapshot.rendererSingleton.value;

    if (systemOk) {
        snapshot.systemVtableMatches =
            VtableMatches(read, context, snapshot.system.value, moduleBase, kSystemVtableRva);
    }

    if (engineOk) {
        snapshot.threeDEngineVtableMatches = VtableMatches(
            read, context, snapshot.threeDEngine.value, moduleBase, kThreeDEngineVtableRva);
        if (snapshot.threeDEngineVtableMatches) {
            PointerFact renderWorld{};
            const std::uintptr_t slot =
                moduleBase + kThreeDEngineVtableRva + kThreeDEngineRenderWorldSlot;
            if (ReadPointer(read, context, slot, renderWorld)) {
                snapshot.renderWorldSlotMatches =
                    renderWorld.value == moduleBase + kRenderWorldRva;
            }
        }
    }

    if (systemOk) {
        std::array<std::uint8_t, kCameraSize> bytes{};
        if (read(snapshot.system.value + SystemLayout::viewCamera, bytes, context)) {
            snapshot.viewCamera = DecodeCamera(bytes);
            snapshot.viewCameraPlausible =
                snapshot.viewCamera.has_value() && IsPlausible(*snapshot.viewCamera);
        }
    }

    snapshot.complete = snapshot.system.plausible && snapshot.renderer.plausible &&
        snapshot.threeDEngine.plausible && snapshot.systemVtableMatches &&
        snapshot.threeDEngineVtableMatches && snapshot.renderWorldSlotMatches &&
        snapshot.viewCameraPlausible;
    return snapshot;
}

namespace {

void Emit(LineSink sink, void* context, const char* text)
{
    if (sink != nullptr) {
        sink(std::string_view{text}, context);
    }
}

} // namespace

void Report(const Snapshot& snapshot, LineSink sink, void* context)
{
    if (sink == nullptr) {
        return;
    }

    char line[256];
    const auto flag = [](bool value) { return value ? "yes" : "no"; };

    std::snprintf(
        line, sizeof(line), "preyvr_snapshot module_base=0x%llX gEnv=0x%llX",
        static_cast<unsigned long long>(snapshot.moduleBase),
        static_cast<unsigned long long>(snapshot.globalEnvironment));
    Emit(sink, context, line);

    const auto pointer = [&](const char* id, const PointerFact& fact) {
        std::snprintf(
            line, sizeof(line), "preyvr_snapshot ptr=%s value=0x%llX read=%s plausible=%s", id,
            static_cast<unsigned long long>(fact.value), flag(fact.read), flag(fact.plausible));
        Emit(sink, context, line);
    };
    pointer("gEnv.pSystem", snapshot.system);
    pointer("gEnv.pRenderer", snapshot.renderer);
    pointer("gEnv.p3DEngine", snapshot.threeDEngine);
    pointer("renderer.singleton", snapshot.rendererSingleton);

    std::snprintf(
        line, sizeof(line),
        "preyvr_snapshot identity system_vtable=%s engine_vtable=%s render_world_slot=%s "
        "renderer_agrees=%s",
        flag(snapshot.systemVtableMatches), flag(snapshot.threeDEngineVtableMatches),
        flag(snapshot.renderWorldSlotMatches), flag(snapshot.rendererSingletonAgrees));
    Emit(sink, context, line);

    if (snapshot.viewCamera.has_value()) {
        const CameraView& camera = *snapshot.viewCamera;
        std::snprintf(
            line, sizeof(line),
            "preyvr_snapshot view_camera pos=%.4f,%.4f,%.4f fov=%.6f res=%dx%d ratio=%.6f "
            "near=%.4f far=%.2f",
            camera.matrix[3], camera.matrix[7], camera.matrix[11], camera.fov, camera.width,
            camera.height, camera.projectionRatio, camera.nearPlane, camera.farPlane);
        Emit(sink, context, line);
        std::snprintf(
            line, sizeof(line),
            "preyvr_snapshot view_camera_asym l=%.6f r=%.6f b=%.6f t=%.6f plausible=%s",
            camera.asymLeft, camera.asymRight, camera.asymBottom, camera.asymTop,
            flag(snapshot.viewCameraPlausible));
        Emit(sink, context, line);
    } else {
        Emit(sink, context, "preyvr_snapshot view_camera unavailable");
    }

    std::snprintf(
        line, sizeof(line), "preyvr_snapshot result=%s", snapshot.complete ? "complete" : "partial");
    Emit(sink, context, line);
}

} // namespace preyvr::snapshot
