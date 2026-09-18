#pragma once

#include <array>
#include <optional>

// Mapping Prey's inventory from its own 3D units into headset metres.
//
// RE-INVENTORY-DEPTH-2026-09-11 established, from a live authorized probe of
// PID 18188, that the inventory is not a flat picture: 403 unique matrices per
// Display frame resolve, after the shared tilt is projected out, onto eight
// planes at these relative offsets in native UI units --
//
//   0, -1000, -2000, -2500, -5000, -7000, -9000, -15000
//
// -- identically in Display frames 1, 61 and 121, with a genuine 3D camera
// transform at Z of about -36882.86 native UI units. A tilted-plane control
// confirmed that translating within the panel does not manufacture a false
// extra plane.
//
// **These units are not metres and there is no recorded conversion**, which is
// exactly what this header supplies. The one relation available is the camera:
// the native camera sits `cameraUnits` from the zero plane and views the same
// content we place `panelMetres` away, so one native unit is
// `panelMetres / |cameraUnits|` metres. Everything else follows from that.
//
// Deliberately free of engine and OpenXR types so the arithmetic is decided and
// tested headless, the same way ConsolePolicy is. Nothing here reads or writes
// game memory; it converts numbers a caller has already obtained.
namespace preyvr::inventory {

// The values RE-INVENTORY-DEPTH-2026-09-11 actually observed. Named rather than
// inlined at the call site so a later probe that disagrees changes one place,
// and so a reader can tell a measurement from an assumption.
inline constexpr float kObservedCameraUnits = -36882.86f;
inline constexpr std::array<float, 8> kObservedPlaneOffsets{
    0.f, -1000.f, -2000.f, -2500.f, -5000.f, -7000.f, -9000.f, -15000.f};

struct DepthMapping {
    // One native UI unit in metres. Always positive.
    float metresPerUnit = 0;
    // Magnitude of eye displacement expressed in native units. This is NOT
    // directly a SetView3D translation: inverse-view signs and a zero-plane
    // projection correction must also be accounted for. The runtime prototype
    // uses ApplyStereoParallax after the native matrix calculation instead.
    float halfSeparationUnits = 0;
    // The distance the zero plane is presented at, in metres. Equal to the
    // caller's panelMetres; carried so a consumer needs only this struct.
    float panelMetres = 0;
};

// `cameraUnits`  the movie's 3D camera Z, native UI units. Sign is ignored;
//                magnitude must exceed 1 or the mapping is degenerate.
// `panelMetres`  where the zero plane is placed, 0.5..10.
// `ipdMetres`    the wearer's interpupillary distance, 0.04..0.09. Taken from
//                the runtime's eye poses rather than assumed.
// `depthScale`   0..1. **A comfort control, not a correctness one.** 1 is the
//                geometrically faithful separation; the most negative plane
//                sits about 0.8 m in front of a 2 m panel, and that much
//                parallax on a UI a wearer reads for minutes is a comfort
//                question no amount of maths settles. 0 collapses to mono.
//
// Returns nothing when any input is outside its range or non-finite. Refused
// rather than clamped: every one of these comes from a measurement, so a value
// out of range means the measurement is wrong and silently substituting a
// plausible number would bury that.
std::optional<DepthMapping> MapDepth(float cameraUnits, float panelMetres,
                                     float ipdMetres, float depthScale);

// Axial plane distance for the observed unrotated native basis: view Z is
// -world Z-cameraUnits and clip W=-view Z. Negative world Z is CLOSER.
// This does not undo the movie's common tilt or identify individual widgets.
float PlaneMetres(const DepthMapping& mapping, float offsetUnits);

// Post-transform stereo correction for the Steam CalcTransMat3D output:
// row-major Matrix44, column vectors, row 3 contains homogeneous clip W.
// eyeOnPanel is the signed eye displacement in panel metres (left negative).
// Xclip += 2*eyeOnPanel/panelWidth * depthScale * (W-cameraUnits).
// This leaves the zero-depth plane fixed and preserves native clipping/masks.
// It avoids SetPerspective3D: Steam explicitly zeros its horizontal shifts.
// Returns false without modifying output on invalid input. Binocular panel
// prototype only; this is not a head-motion reprojection or a cylinder mapping.
bool ApplyStereoParallax(std::array<float,16>& clip, float cameraUnits,
                         float panelWidth, float eyeOnPanel, float depthScale);

} // namespace preyvr::inventory
