#include "preyvr/NearParticles.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace preyvr;
using namespace preyvr::nearfx;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

void TestBuilders()
{
    Require(BuilderFromReturnRva(0x1AEA76) == Builder::Sprites, "sprite vertex context");
    Require(BuilderFromReturnRva(0x1B1EFC) == Builder::Geometry, "geometry vertex context");
    Require(BuilderFromReturnRva(0x1B0C88) == Builder::Other, "third vertex context");
    Require(BuilderFromReturnRva(0x1AEA71) == Builder::Unknown, "a call site is not a return address");
    Require(IsSpriteVertexCaller(0x1B2D69) && IsSpriteVertexCaller(0x1B279E), "sprite vertex callers");
    Require(!IsSpriteVertexCaller(0x1B1650) && !IsSpriteVertexCaller(0x1AE399),
            "geometry and culling callers of GetRenderMatrix are not moved");
}

// A sprite's world vertex moved by the eye offset, minus the eye camera (what the
// renderer does), equals world minus cyclops: the geometry path's result.
void TestSpriteOffset()
{
    const Vec3 centre{-2.f, 5.f, 1.5f};
    const Vec3 right{0.f, -1.f, 0.f};
    const Vec3 world{-1.6f, 5.1f, 1.3f};
    for (int eye = 0; eye < 2; ++eye) {
        const Vec3 eyeCamera = Add(centre, Scale(right, eye == 0 ? -.031f : .031f));
        Vec3 offset{};
        Require(EyeOffset(eyeCamera, {eyeCamera, centre}, offset), "eye offset resolves");
        const Vec3 rendered = Sub(Add(world, offset), eyeCamera);
        const Vec3 wanted = Sub(world, centre);
        Require(Near(rendered.x, wanted.x) && Near(rendered.y, wanted.y) && Near(rendered.z, wanted.z),
                "sprite ends camera-relative to the cyclops");
    }
    Vec3 offset{1, 1, 1};
    Require(!EyeOffset(centre, {Add(centre, {.03f, 0, 0}), centre}, offset), "not an eye camera: no move");
}

void TestOrigin()
{
    const Vec3 centre{10.f, -4.f, 1.7f};
    const Vec3 right{0.6f, 0.8f, 0.f};
    const EyeCamera left{Sub(centre, Scale(right, .032f)), centre};
    Vec3 out{};
    Require(CameraSpaceOrigin(left.position, left, out) && Near(out.x, centre.x) && Near(out.y, centre.y) &&
                Near(out.z, centre.z),
            "an eye's camera maps to its cyclops");
    Require(!CameraSpaceOrigin(Add(left.position, {.001f, 0, 0}), left, out), "another camera is not that eye");
    Require(!CameraSpaceOrigin(centre, left, out), "the cyclops itself is not the eye");
    const EyeCamera far{Sub(centre, Scale(right, .5f)), centre};
    Require(!CameraSpaceOrigin(far.position, far, out), "a record half a metre off is not a stereo eye");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Require(!CameraSpaceOrigin({nan, 0, 0}, left, out), "a NaN camera is refused");
}

// The near pass, as measured: camera-space content is drawn at its camera-space
// position minus NearViewStereo's eye offset (eye - cyclops). A DrawNear particle
// arrives in camera space as world - context camera.
void TestNearPassModel()
{
    const Vec3 centre{3.f, 7.f, 1.6f};
    const Vec3 right{1.f, 0.f, 0.f};
    const float half = .032f;
    const Vec3 coilLocal{.05f, .5f, -.2f};  // 0.5 m in front, a little right, below the eye
    const Vec3 coilWorld = Add(centre, coilLocal);  // where the game puts the effect's emitter
    const float f = 587.f;                         // mock compositor focal length, px
    float weaponX[2]{}, nativeX[2]{}, fixedX[2]{};
    for (int eye = 0; eye < 2; ++eye) {
        const Vec3 shift = Scale(right, eye == 0 ? -half : half);
        const Vec3 eyeCamera = Add(centre, shift);
        // The weapon: camera-space content, offset by the near-pass stereo.
        const Vec3 weapon = Sub(coilLocal, shift);
        // The glow, native: camera-relative to the EYE, then offset again.
        const Vec3 native = Sub(Sub(coilWorld, eyeCamera), shift);
        // The glow, fixed: camera-relative to the eye's cyclops, then offset.
        Vec3 origin{};
        Require(CameraSpaceOrigin(eyeCamera, {eyeCamera, centre}, origin), "eye resolves");
        const Vec3 fixed = Sub(Sub(coilWorld, origin), shift);
        weaponX[eye] = f * weapon.x / weapon.y;
        nativeX[eye] = f * native.x / native.y;
        fixedX[eye] = f * fixed.x / fixed.y;
        Require(Near(fixed.x, weapon.x) && Near(fixed.y, weapon.y) && Near(fixed.z, weapon.z),
                "the fixed glow sits exactly on the coil in each eye");
    }
    const float weaponDisparity = weaponX[0] - weaponX[1];
    const float nativeDisparity = nativeX[0] - nativeX[1];
    const float fixedDisparity = fixedX[0] - fixedX[1];
    // Crossed disparity of an object at 0.5 m with a 64 mm IPD: ~75 px.
    Require(Near(weaponDisparity, f * 2 * half / .5f, .01f), "the weapon has the parallax of its depth");
    Require(Near(nativeDisparity, 2 * weaponDisparity, .01f), "native: the glow has twice the coil's parallax");
    Require(Near(nativeDisparity - weaponDisparity, 75.1f, .2f), "native: ~75 px extra at 0.5 m, i.e. a quarter metre from the eye");
    Require(Near(fixedDisparity, weaponDisparity, .001f), "fixed: the glow has the coil's parallax");
    // Seen depth of the native glow: disparity doubles, depth halves.
    Require(Near(f * 2 * half / nativeDisparity, .25f, 1e-4f), "native glow appears at half the coil's distance");
}

// The Q-Beam's inner beam (PlayerWeapons.xml InnerBeam_00, PositionOffset
// y=-0.4): authored behind the muzzle along the barrel, mirrored to run ahead.
void TestBridgeShift()
{
    Vec3 barrel{0.05f, 0.f, -1.f};
    barrel = Scale(barrel, 1.f / std::sqrt(barrel.x * barrel.x + barrel.z * barrel.z));
    const Vec3 muzzle{0.1f, 1.2f, -1.2f};
    Vec3 shift{};
    Require(BridgeShift({0, -0.4f, -0.01f}, barrel, shift), "the inner beam is the bridge");
    // The native centre 0.4 m behind the muzzle lands 0.4 m ahead of it.
    const Vec3 native = Add(muzzle, Scale(barrel, -0.4f));
    const Vec3 moved = Add(native, shift);
    const Vec3 ahead = Add(muzzle, Scale(barrel, 0.4f));
    Require(Near(moved.x, ahead.x, 1e-5f) && Near(moved.y, ahead.y, 1e-5f) && Near(moved.z, ahead.z, 1e-5f),
            "mirrored through the muzzle along the barrel");
    Require(BridgeShift({0, -0.6f, -0.01f}, barrel, shift) && Near(std::sqrt(shift.x * shift.x + shift.z * shift.z), 1.2f, 1e-5f),
            "InnerBeamStart_00 (0.6 m back) moves 1.2 m");
    Require(!BridgeShift({0, -0.1f, 0}, barrel, shift), "a muzzle beam sprite 10 cm back is the weapon's own");
    Require(!BridgeShift({0, 0.3f, 0}, barrel, shift), "forward offsets (screen effects) are left");
    Require(!BridgeShift({0.0625f, -0.06f, 0.015f}, barrel, shift), "coil arcs are left");
    Require(!BridgeShift({0, -0.4f, 0}, Vec3{0, 0, -2.f}, shift), "a non-unit axis refuses");
    Require(!BridgeShift({0, std::nanf(""), 0}, barrel, shift), "nonfinite refuses");
}

} // namespace

int main()
{
    TestBuilders();
    TestOrigin();
    TestSpriteOffset();
    TestBridgeShift();
    TestNearPassModel();
    std::cout << "near_particles: all tests passed\n";
    return 0;
}
