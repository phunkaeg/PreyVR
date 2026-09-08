// Execute production projection, field writes, reporting and dispatch with a
// synthetic CCamera and a HUD boundary stub. No game module or runtime is used.
#include "../../../src/dll/ReticleFollow.cpp"
#include <iostream>
#include <vector>

std::vector<std::pair<std::string, float>> calls;
namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
DWORD EnsureRenderHookInstalled() { return 0; }
DWORD CallHudOneFloat(const char* name, float value) {
    calls.emplace_back(name, value);
    return 0;
}
}
int failures = 0;
void Check(bool ok, const std::string& name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
bool Near(float a, float b) { return std::fabs(a-b) < 2e-6f; }

int main() {
    using namespace preyvr;
    using namespace preyvr::dll;
    std::array<std::uint8_t, engine::CameraLayout::size> camera{};
    std::array<std::uint8_t, 0x1900> player{};
    const auto field = [&](std::size_t offset, float value) {
        std::memcpy(camera.data() + offset, &value, sizeof(value));
    };
    const float half = std::sqrt(3.0f); // 120-degree symmetric horizontal FOV
    field(engine::CameraLayout::fov, 2 * std::atan(half));
    field(engine::CameraLayout::projectionRatio, 1);
    field(engine::CameraLayout::edgeNearLeftTop + sizeof(float), 0.1f);
    ReticleAimContext context{};
    context.trackingSequence = 71;
    context.trackingEpoch = 5;
    context.referenceGeneration = 3;
    SetReticleFollowEnabled(1);
    SetReticleConvergenceMillimetres(10000);
    for (int degrees = -30; degrees <= 30; degrees += 5) {
        const float yaw = degrees * 0.0174532925199433f;
        context.head.orientation = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
        stereo::WriteMatrix(camera, stereo::MatrixFromPose({stereo::YawQuaternion(yaw), {}}));
        calls.clear();
        Check(WriteReticleForCamera(player.data(), {}, {0, 1, 0}, camera, &context),
              "projection accepted, yaw=" + std::to_string(degrees));
        // Independent analytic pinhole result, not another projection helper.
        const float expectedX = 0.5f + std::tan(yaw) / (2 * half);
        float written[2]{};
        std::memcpy(written, player.data() + engine::ArkPlayerLayout::reticleScreenPosition, sizeof(written));
        Check(Near(written[0], expectedX) && Near(written[1], 0.5f), "world-fixed ray obeys perspective");
        Check(calls.size() == 2 && calls[0].first == "reticleXOffset" &&
              calls[1].first == "reticleYOffset" && Near(calls[0].second, expectedX) &&
              Near(calls[1].second, 0.5f), "field write and native dispatch agree");
        std::cout << ReticleProjectionReport() << '\n';
    }
    // Asymmetric eye and finite origin/depth: direct ratio of known components.
    stereo::WriteMatrix(camera, stereo::MatrixFromPose({{}, {0.032f, 0, 0}}));
    field(engine::CameraLayout::asymLeft, 0.02f);
    field(engine::CameraLayout::asymRight, 0.02f);
    context.controller.orientation = {0.1f, 0.2f, 0.3f, -0.92736185f};
    Check(WriteReticleForCamera(player.data(), {0.2f, 0, 0}, {0, 1, 0}, camera, &context),
          "finite origin and asymmetric eye accepted");
    ProjectionRecord snapshot{};
    Check(gProjection.TryRead(snapshot) &&
          Near(snapshot.x, (0.0168f + half - 0.2f) / (2 * half)),
          "eye displacement, convergence and asymmetry survive projection");
    context.controller.orientation.w = 0; // publisher must have copied input
    const auto report = ReticleProjectionReport();
    Check(report.find("reticleProjection=fresh") != std::string::npos &&
          report.find("rpSeq=71") != std::string::npos &&
          report.find("rpEpoch=5") != std::string::npos &&
          snapshot.context.controller.orientation.w < -0.9f &&
          report.find("rpRawQ=0.1") != std::string::npos,
          "report owns one full-precision context including quaternion W");
    Check(WriteReticleForCamera(player.data(), {}, {1, 0.1f, 0}, camera, &context) &&
          gProjection.TryRead(snapshot) && snapshot.clamped && snapshot.rawX > 1 && snapshot.x == 1,
          "clamp retains raw coordinate for diagnosis");
    SetReticleDispatchEnabled(0);
    calls.clear();
    Check(WriteReticleForCamera(player.data(), {}, {0, 1, 0}, camera, &context) &&
          calls.empty() && gProjection.TryRead(snapshot) && !snapshot.dispatch,
          "dispatch disabled is distinct from a successful call");
    Check(!WriteReticleForCamera(player.data(), {}, {0, -1, 0}, camera, &context),
          "behind-camera ray refuses");
    SetReticleFollowEnabled(0);
    Check(ReticleProjectionReport() == " reticleProjection=unavailable", "disable clears diagnostic");
    return failures ? 1 : 0;
}
