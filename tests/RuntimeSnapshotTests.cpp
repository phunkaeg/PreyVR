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

    // --- the residual must SEPARATE, or no threshold saves it ---------------
    //
    // Imported from the cross-engine playbook (A3.4): a residual limit is a
    // property of a metric, not of a problem. FarCry2-VR imported another
    // project's threshold and got a gate looser than no gate at all, because
    // every wrong case still scored under it. So build the wrong-input table
    // first, prove the metric separates, and only then keep the number.
    {
        const CameraView source = SampleCamera();

        const auto derive = [](const CameraView& c) {
            const float tan = std::tan(c.fov * 0.5f);
            const float horiz = tan * c.projectionRatio;
            RenderCameraView d{};
            d.axisX = Vec3{1.0f, 0.0f, 0.0f};
            d.axisY = Vec3{0.0f, 1.0f, 0.0f};
            d.axisZ = Vec3{0.0f, 0.0f, 1.0f};
            d.frustumLeft = c.asymLeft - horiz;
            d.frustumRight = horiz + c.asymRight;
            d.frustumBottom = c.asymBottom - tan;
            d.frustumTop = tan + c.asymTop;
            d.nearPlane = c.nearPlane;
            d.farPlane = c.farPlane;
            return d;
        };

        const RenderCameraView truth = derive(source);
        const float correct = RenderCameraResidual(source, truth);
        Require(correct < 1.0e-6f, "a correctly derived render camera reproduces to ~zero");

        // Each wrong case perturbs exactly one input and must score far above.
        CameraView wrongFov = source;
        wrongFov.fov = source.fov * 1.05f; // 5% FoV error
        CameraView wrongRatio = source;
        wrongRatio.projectionRatio = 4.0f / 3.0f;
        CameraView wrongNear = source;
        wrongNear.nearPlane = source.nearPlane * 4.0f;
        CameraView symmetrised = source; // the classic: asymmetry silently dropped
        symmetrised.asymLeft = 0.0f;
        symmetrised.asymRight = 0.0f;

        // A deliberately SMALL asymmetry error -- the tightest wrong case, and
        // the one that decides whether the threshold is defensible.
        CameraView tinyAsym = source;
        tinyAsym.asymTop = 0.004f;

        const CameraView asymSource = [&] {
            CameraView c = source;
            c.asymLeft = 0.06f;
            c.asymRight = -0.02f;
            return c;
        }();
        const RenderCameraView asymTruth = derive(asymSource);

        struct WrongCase { const char* name; float residual; };
        const WrongCase wrong[] = {
            {"5% FoV error", RenderCameraResidual(wrongFov, truth)},
            {"wrong aspect (4:3)", RenderCameraResidual(wrongRatio, truth)},
            {"wrong near plane", RenderCameraResidual(wrongNear, truth)},
            {"asymmetry symmetrised away", RenderCameraResidual(symmetrised, asymTruth)},
            {"tiny 0.004 asymmetry error", RenderCameraResidual(tinyAsym, truth)},
        };

        float tightest = 1.0e9f;
        for (const WrongCase& w : wrong) {
            Require(w.residual > kRenderCameraResidualLimit, w.name);
            tightest = std::min(tightest, w.residual);
        }
        // The gap must be real, not nominal: the limit sits well inside it.
        Require(correct < kRenderCameraResidualLimit * 0.1f,
            "correct scores an order of magnitude below the limit");
        Require(tightest > kRenderCameraResidualLimit * 10.0f,
            "the tightest wrong case scores an order of magnitude above the limit");
        Require(RenderCameraMatchesSource(asymSource, asymTruth),
            "a genuinely asymmetric frustum still passes");
    }

    // --- OpenXR per-eye asymmetry solves back to the requested frustum ------
    {
        const CameraView base = SampleCamera();
        const float t = std::tan(base.fov * 0.5f);
        const float h = t * base.projectionRatio;

        // A symmetric FOV must produce ZERO shift. A sign error survives every
        // other check while quietly symmetrising the eye, so this is the guard.
        const EyeAsymmetry none =
            AsymmetryFromFovTangents(-h, h, -t, t, base.fov, base.projectionRatio);
        Require(std::fabs(none.left) < 1e-6f && std::fabs(none.right) < 1e-6f &&
                std::fabs(none.bottom) < 1e-6f && std::fabs(none.top) < 1e-6f,
            "a symmetric FoV yields zero asymmetry shift");

        // A realistic asymmetric eye: inner edge narrower than outer.
        const float tanL = -1.10f, tanR = 0.95f, tanD = -0.98f, tanU = 1.02f;
        const EyeAsymmetry eye =
            AsymmetryFromFovTangents(tanL, tanR, tanD, tanU, base.fov, base.projectionRatio);

        // Round-trip: write the shifts onto the camera, push it through the
        // SetCamera formula, and confirm the frustum we asked for comes back.
        CameraView eyeCamera = base;
        eyeCamera.asymLeft = eye.left;
        eyeCamera.asymRight = eye.right;
        eyeCamera.asymBottom = eye.bottom;
        eyeCamera.asymTop = eye.top;

        RenderCameraView produced{};
        produced.axisX = Vec3{1.0f, 0.0f, 0.0f};
        produced.axisY = Vec3{0.0f, 1.0f, 0.0f};
        produced.axisZ = Vec3{0.0f, 0.0f, 1.0f};
        produced.frustumLeft = eyeCamera.asymLeft - h;
        produced.frustumRight = h + eyeCamera.asymRight;
        produced.frustumBottom = eyeCamera.asymBottom - t;
        produced.frustumTop = t + eyeCamera.asymTop;
        produced.nearPlane = eyeCamera.nearPlane;
        produced.farPlane = eyeCamera.farPlane;

        Require(std::fabs(produced.frustumLeft - tanL) < 1e-5f, "left tangent round-trips");
        Require(std::fabs(produced.frustumRight - tanR) < 1e-5f, "right tangent round-trips");
        Require(std::fabs(produced.frustumBottom - tanD) < 1e-5f, "bottom tangent round-trips");
        Require(std::fabs(produced.frustumTop - tanU) < 1e-5f, "top tangent round-trips");
        Require(IsPlausible(produced), "the per-eye frustum is plausible");
        Require(RenderCameraMatchesSource(eyeCamera, produced),
            "and agrees with the camera that produced it");

        // The asymmetry must be genuinely off-centre, or we have symmetrised.
        Require(std::fabs(produced.frustumLeft) - std::fabs(produced.frustumRight) > 0.1f,
            "the eye frustum is actually asymmetric, not a symmetric one in disguise");
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

    // --- Capture B: the pooled render views (R-033) -------------------------
    {
        using namespace preyvr::engine;
        constexpr std::uintptr_t kView0 = 0x00007FF9'4000'0000ULL;
        constexpr std::uintptr_t kSwapChain = 0x00007FF9'5000'0000ULL;
        constexpr std::uintptr_t kDevice = 0x00007FF9'6000'0000ULL;

        const CameraView camera = SampleCamera();
        const float t = std::tan(camera.fov * 0.5f);
        const float h = t * camera.projectionRatio;

        std::vector<std::uint8_t> rcBlob(kRenderCameraSize, 0);
        const auto putf = [&rcBlob](std::size_t off, float v) {
            std::memcpy(rcBlob.data() + off, &v, sizeof(v));
        };
        const std::uintptr_t rcBase = RenderViewLayout::renderCamera;
        putf(RenderCameraLayout::axisX - rcBase, 1.0f);
        putf(RenderCameraLayout::axisY - rcBase + 4, 1.0f);
        putf(RenderCameraLayout::axisZ - rcBase + 8, 1.0f);
        putf(RenderCameraLayout::frustumLeft - rcBase, camera.asymLeft - h);
        putf(RenderCameraLayout::frustumRight - rcBase, h + camera.asymRight);
        putf(RenderCameraLayout::frustumBottom - rcBase, camera.asymBottom - t);
        putf(RenderCameraLayout::frustumTop - rcBase, t + camera.asymTop);
        putf(RenderCameraLayout::nearPlane - rcBase, camera.nearPlane);
        putf(RenderCameraLayout::farPlane - rcBase, camera.farPlane);

        const std::vector<std::uint8_t> camBlob = EncodeCamera(camera);

        const auto buildPool = [&](FakeMemory& m, bool distinctRecursive) {
            m.PlacePointer(kModuleBase + RendererLayout::singletonPointerRva, kRendererObject);
            m.PlacePointer(kRendererObject + RendererLayout::swapchain, kSwapChain);
            m.PlacePointer(kRendererObject + RendererLayout::device, kDevice);
            const std::int32_t slot = 0;
            m.Place(kRendererObject + RendererLayout::frameSlotIndex, &slot, sizeof(slot));
            m.Place(kRendererObject + RendererLayout::frameBlock +
                    RendererLayout::frameBlockRenderCamera, rcBlob.data(), rcBlob.size());
            for (std::size_t i = 0; i < 4; ++i) {
                const std::uintptr_t view =
                    kView0 + (distinctRecursive ? i : (i / 2) * 2) * 0x10000ULL;
                m.PlacePointer(kRendererObject + RendererLayout::renderViewPool + i * 8, view);
                m.PlacePointer(view, kModuleBase + RenderViewIdentity::vtableRva);
                m.Place(view + RenderViewLayout::camera, camBlob.data(), camBlob.size());
                m.Place(view + RenderViewLayout::renderCamera, rcBlob.data(), rcBlob.size());
            }
        };

        FakeMemory good;
        buildPool(good, true);
        const RenderViewCapture ok = CaptureRenderViews(kModuleBase, &FakeRead, &good);
        Require(ok.complete, "a well-formed render-view pool yields a complete capture");
        Require(ok.allFourNonNull, "all four pool entries are non-null");
        Require(ok.recursiveDistinctFromDefault,
            "R-033's own test: the recursive view is a distinct object");
        Require(ok.swapChain.value == kSwapChain && ok.device.value == kDevice,
            "the swapchain and device pointer VALUES are captured (no COM call)");
        for (const RenderViewFact& f : ok.views) {
            Require(f.vtableMatches, "each pooled entry identifies as a CRenderView");
            Require(f.camera.has_value() && f.derived.has_value(), "both cameras decode");
            Require(f.residual >= 0.0f && f.residualWithinLimit,
                "the derived frustum agrees with the camera it came from");
        }
        Require(ok.frameSlotRead && ok.frameBlockCamera.has_value(),
            "the R-026 per-frame block is captured as an independent cross-check");

        // R-033 warns the pool may not be what we think. Each negative proves the
        // capture reports that rather than decoding garbage.
        FakeMemory sharedRecursive;
        buildPool(sharedRecursive, false);
        const RenderViewCapture shared =
            CaptureRenderViews(kModuleBase, &FakeRead, &sharedRecursive);
        Require(!shared.recursiveDistinctFromDefault && !shared.complete,
            "a pool whose recursive slot aliases the default fails R-033's test");

        FakeMemory wrongVtable;
        buildPool(wrongVtable, true);
        wrongVtable.PlacePointer(kView0, kModuleBase + 0x1234);
        const RenderViewCapture bad = CaptureRenderViews(kModuleBase, &FakeRead, &wrongVtable);
        Require(!bad.views[0].vtableMatches && !bad.complete,
            "an entry that is not a CRenderView is reported, not decoded");
        Require(bad.views[1].vtableMatches,
            "and one bad entry does not contaminate the others");

        FakeMemory nullSlot;
        buildPool(nullSlot, true);
        nullSlot.PlacePointer(kRendererObject + RendererLayout::renderViewPool + 3 * 8, 0);
        const RenderViewCapture holed = CaptureRenderViews(kModuleBase, &FakeRead, &nullSlot);
        Require(!holed.allFourNonNull && !holed.complete, "a null pool entry fails the test");

        FakeMemory empty;
        const RenderViewCapture none = CaptureRenderViews(kModuleBase, &FakeRead, &empty);
        Require(!none.rendererPlausible && !none.complete,
            "an unmapped renderer fails closed rather than faulting");

        std::vector<std::string> lines;
        Report(ok, [](std::string_view l, void* c) {
            static_cast<std::vector<std::string>*>(c)->emplace_back(l); }, &lines);
        bool sawResidualValue = false, sawR033 = false;
        for (const std::string& l : lines) {
            Require(l.rfind("preyvr_renderview", 0) == 0, "every line carries the grep prefix");
            if (l.find("residual") != std::string::npos && l.find("value=") != std::string::npos)
                sawResidualValue = true;
            if (l.find("r033") != std::string::npos) sawR033 = true;
        }
        Require(sawResidualValue, "the residual is logged as a number, not just a verdict");
        Require(sawR033, "R-033's acceptance verdict is reported on its own line");
    }

    std::cout << "PreyVR runtime-snapshot tests passed\n";
    return 0;
}
