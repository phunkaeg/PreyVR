#include "preyvr/RuntimeSnapshot.h"

#include "preyvr/EngineMap.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace preyvr;
using namespace preyvr::snapshot;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Byte-addressable fake address space. A read succeeds only when every byte in
// the range has been placed, so unmapped memory behaves like unmapped memory
// rather than returning zeroes -- which is what makes the negative tests mean
// something.
struct FakeMemory {
    std::unordered_map<std::uintptr_t, std::uint8_t> bytes;

    void Place(std::uintptr_t address, const void* data, std::size_t size)
    {
        const auto* source = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            bytes[address + i] = source[i];
        }
    }

    void PlacePointer(std::uintptr_t address, std::uintptr_t value)
    {
        Place(address, &value, sizeof(value));
    }
};

bool FakeRead(std::uintptr_t address, std::span<std::uint8_t> out, void* context)
{
    auto& memory = *static_cast<FakeMemory*>(context);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto found = memory.bytes.find(address + i);
        if (found == memory.bytes.end()) {
            return false;
        }
        out[i] = found->second;
    }
    return true;
}

std::vector<std::uint8_t> EncodeCamera(const CameraView& camera)
{
    using preyvr::engine::CameraLayout;
    std::vector<std::uint8_t> blob(kCameraSize, 0);
    const auto put = [&blob](std::size_t offset, float value) {
        std::memcpy(blob.data() + offset, &value, sizeof(value));
    };
    for (std::size_t i = 0; i < camera.matrix.size(); ++i) {
        put(CameraLayout::matrix + i * sizeof(float), camera.matrix[i]);
    }
    put(CameraLayout::fov, camera.fov);
    std::memcpy(blob.data() + CameraLayout::width, &camera.width, sizeof(camera.width));
    std::memcpy(blob.data() + CameraLayout::height, &camera.height, sizeof(camera.height));
    put(CameraLayout::projectionRatio, camera.projectionRatio);
    put(CameraLayout::edgeNearLeftTop + 4, camera.nearPlane);
    put(CameraLayout::edgeFarLeftTop + 4, camera.farPlane);
    put(CameraLayout::asymLeft, camera.asymLeft);
    put(CameraLayout::asymRight, camera.asymRight);
    put(CameraLayout::asymBottom, camera.asymBottom);
    put(CameraLayout::asymTop, camera.asymTop);
    return blob;
}

CameraView SampleCamera()
{
    CameraView camera{};
    // Identity rotation with a translation in column 3.
    camera.matrix = {1.0f, 0.0f, 0.0f, 12.5f, 0.0f, 1.0f, 0.0f, -3.25f, 0.0f, 0.0f, 1.0f, 1.75f};
    camera.fov = 1.3962634f; // 80 degrees
    camera.width = 2560;
    camera.height = 1440;
    camera.projectionRatio = 2560.0f / 1440.0f;
    camera.nearPlane = 0.25f;
    camera.farPlane = 8192.0f;
    return camera;
}

constexpr std::uintptr_t kModuleBase = 0x180000000ULL;
constexpr std::uintptr_t kSystemObject = 0x00007FF9'1000'0000ULL;
constexpr std::uintptr_t kRendererObject = 0x00007FF9'2000'0000ULL;
constexpr std::uintptr_t kEngineObject = 0x00007FF9'3000'0000ULL;

// Builds a fake address space that a correct capture must accept. Every
// negative test below starts from this and breaks exactly one thing.
FakeMemory BuildSupportedEngine(const CameraView& camera)
{
    using namespace preyvr::engine;
    FakeMemory memory;
    const std::uintptr_t gEnv = kModuleBase + GlobalEnvironmentLayout::baseRva;
    memory.PlacePointer(gEnv + GlobalEnvironmentLayout::system, kSystemObject);
    memory.PlacePointer(gEnv + GlobalEnvironmentLayout::renderer, kRendererObject);
    memory.PlacePointer(gEnv + GlobalEnvironmentLayout::threeDEngine, kEngineObject);
    memory.PlacePointer(kModuleBase + RendererLayout::singletonPointerRva, kRendererObject);

    memory.PlacePointer(kSystemObject, kModuleBase + kSystemVtableRva);
    memory.PlacePointer(kEngineObject, kModuleBase + kThreeDEngineVtableRva);
    memory.PlacePointer(
        kModuleBase + kThreeDEngineVtableRva + kThreeDEngineRenderWorldSlot,
        kModuleBase + kRenderWorldRva);

    const std::vector<std::uint8_t> blob = EncodeCamera(camera);
    memory.Place(kSystemObject + SystemLayout::viewCamera, blob.data(), blob.size());
    return memory;
}

} // namespace

int main()
{
    // --- decode round-trip -------------------------------------------------
    {
        const CameraView source = SampleCamera();
        const std::vector<std::uint8_t> blob = EncodeCamera(source);
        Require(blob.size() == kCameraSize, "a camera blob is exactly sizeof(CCamera)");
        Require(!DecodeCamera(std::span{blob}.first(kCameraSize - 1)).has_value(),
            "a short blob is refused rather than read past the end");

        const auto decoded = DecodeCamera(blob);
        Require(decoded.has_value(), "a well-formed camera decodes");
        Require(decoded->fov == source.fov, "fov survives the round trip");
        Require(decoded->width == source.width && decoded->height == source.height,
            "resolution survives the round trip");
        Require(decoded->nearPlane == source.nearPlane && decoded->farPlane == source.farPlane,
            "near and far are read from the edge vertices, not adjacent fields");
        Require(decoded->matrix[3] == 12.5f && decoded->matrix[7] == -3.25f &&
                decoded->matrix[11] == 1.75f,
            "translation is column 3 of the Matrix34");
        Require(IsPlausible(*decoded), "a stock camera passes the plausibility check");
    }

    // --- plausibility rejects what a wrong offset would produce ------------
    {
        CameraView camera = SampleCamera();
        camera.fov = 0.0f;
        Require(!IsPlausible(camera), "a zero FoV is rejected");
        camera = SampleCamera();
        camera.farPlane = camera.nearPlane;
        Require(!IsPlausible(camera), "a far plane at the near plane is rejected");
        camera = SampleCamera();
        camera.width = 0;
        Require(!IsPlausible(camera), "a zero width is rejected");
        camera = SampleCamera();
        camera.nearPlane = std::nanf("");
        Require(!IsPlausible(camera), "a non-finite near plane is rejected");
    }

    // --- the render camera must agree with the camera it came from ---------
    {
        const CameraView source = SampleCamera();
        const float tangent = std::tan(source.fov * 0.5f);
        const float horizontal = tangent * source.projectionRatio;

        RenderCameraView derived{};
        derived.axisX = Vec3{1.0f, 0.0f, 0.0f};
        derived.axisY = Vec3{0.0f, 1.0f, 0.0f};
        derived.axisZ = Vec3{0.0f, 0.0f, 1.0f};
        derived.origin = Vec3{12.5f, -3.25f, 1.75f};
        derived.frustumLeft = source.asymLeft - horizontal;
        derived.frustumRight = horizontal + source.asymRight;
        derived.frustumBottom = source.asymBottom - tangent;
        derived.frustumTop = tangent + source.asymTop;
        derived.nearPlane = source.nearPlane;
        derived.farPlane = source.farPlane;

        Require(IsPlausible(derived), "a derived render camera is plausible");
        Require(RenderCameraMatchesSource(source, derived),
            "the SetCamera frustum formula reproduces the render camera");

        // The whole point of the asymmetry fields: a per-eye shift must move the
        // derived frustum, or our model of SetCamera is wrong.
        CameraView shifted = source;
        shifted.asymLeft = 0.05f;
        Require(!RenderCameraMatchesSource(shifted, derived),
            "an asymmetry shift changes the derived frustum");

        RenderCameraView shiftedDerived = derived;
        shiftedDerived.frustumLeft = shifted.asymLeft - horizontal;
        Require(RenderCameraMatchesSource(shifted, shiftedDerived),
            "and the shifted frustum matches the shifted source");
    }

    // --- capture against a synthetic supported engine ----------------------
    {
        const CameraView camera = SampleCamera();
        FakeMemory memory = BuildSupportedEngine(camera);
        const Snapshot snapshot = Capture(kModuleBase, &FakeRead, &memory);

        Require(snapshot.complete, "a correct engine image yields a complete snapshot");
        Require(snapshot.globalEnvironment ==
                kModuleBase + preyvr::engine::GlobalEnvironmentLayout::baseRva,
            "gEnv is resolved from the module base");
        Require(snapshot.system.value == kSystemObject, "gEnv->pSystem is followed");
        Require(snapshot.threeDEngine.value == kEngineObject, "gEnv->p3DEngine is followed");
        Require(snapshot.systemVtableMatches, "the CSystem vtable identifies the object");
        Require(snapshot.threeDEngineVtableMatches, "the C3DEngine vtable identifies the object");
        Require(snapshot.renderWorldSlotMatches, "IProcess slot 3 resolves to RenderWorld");
        Require(snapshot.rendererSingletonAgrees,
            "gEnv->pRenderer and the CD3D9Renderer singleton are the same object");
        Require(snapshot.viewCamera.has_value() && snapshot.viewCameraPlausible,
            "the system view camera is read and plausible");
        Require(snapshot.viewCamera->width == camera.width,
            "the captured camera is the one that was placed");
    }

    // --- each negative breaks exactly one thing ----------------------------
    {
        const CameraView camera = SampleCamera();

        FakeMemory wrongVtable = BuildSupportedEngine(camera);
        wrongVtable.PlacePointer(kSystemObject, kModuleBase + 0x1234);
        const Snapshot a = Capture(kModuleBase, &FakeRead, &wrongVtable);
        Require(!a.systemVtableMatches && !a.complete,
            "a wrong CSystem vtable fails the identity check");
        Require(a.threeDEngineVtableMatches,
            "and does not contaminate the unrelated 3D-engine check");

        FakeMemory nullSystem = BuildSupportedEngine(camera);
        nullSystem.PlacePointer(
            kModuleBase + preyvr::engine::GlobalEnvironmentLayout::baseRva +
                preyvr::engine::GlobalEnvironmentLayout::system,
            0);
        const Snapshot b = Capture(kModuleBase, &FakeRead, &nullSystem);
        Require(b.system.read && !b.system.plausible,
            "a null pSystem reads successfully but is not plausible");
        Require(!b.complete, "and the snapshot is not complete");

        FakeMemory splitRenderer = BuildSupportedEngine(camera);
        splitRenderer.PlacePointer(
            kModuleBase + preyvr::engine::RendererLayout::singletonPointerRva, kEngineObject);
        const Snapshot c = Capture(kModuleBase, &FakeRead, &splitRenderer);
        Require(!c.rendererSingletonAgrees,
            "a singleton that disagrees with gEnv->pRenderer is reported");

        FakeMemory unmapped;
        const Snapshot d = Capture(kModuleBase, &FakeRead, &unmapped);
        Require(!d.system.read && !d.complete,
            "an entirely unmapped image fails closed instead of faulting");

        FakeMemory garbageCamera = BuildSupportedEngine(camera);
        const std::vector<std::uint8_t> zeros(kCameraSize, 0);
        garbageCamera.Place(
            kSystemObject + preyvr::engine::SystemLayout::viewCamera, zeros.data(), zeros.size());
        const Snapshot e = Capture(kModuleBase, &FakeRead, &garbageCamera);
        Require(!e.viewCameraPlausible && !e.complete,
            "an all-zero camera is decoded but reported as implausible");
    }

    // --- the report is emitted and greppable -------------------------------
    {
        const CameraView camera = SampleCamera();
        FakeMemory memory = BuildSupportedEngine(camera);
        const Snapshot snapshot = Capture(kModuleBase, &FakeRead, &memory);

        std::vector<std::string> lines;
        Report(
            snapshot,
            [](std::string_view line, void* context) {
                static_cast<std::vector<std::string>*>(context)->emplace_back(line);
            },
            &lines);
        Require(!lines.empty(), "the report emits lines");
        bool sawResult = false;
        bool sawAsym = false;
        for (const std::string& line : lines) {
            Require(line.rfind("preyvr_snapshot", 0) == 0, "every line carries the grep prefix");
            if (line.find("result=complete") != std::string::npos) {
                sawResult = true;
            }
            if (line.find("view_camera_asym") != std::string::npos) {
                sawAsym = true;
            }
        }
        Require(sawResult, "the report states the overall verdict");
        Require(sawAsym, "the report records the asymmetry baseline we will need later");
    }

    std::cout << "PreyVR runtime-snapshot tests passed\n";
    return 0;
}
