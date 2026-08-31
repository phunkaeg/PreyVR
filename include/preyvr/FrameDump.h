#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// A frame dump is a backbuffer readback written to disk so that a *rendered
// result* becomes something we can assert on.
//
// The motivation is specific. The first render-path write test asks "does
// modifying CSystem::m_ViewCamera change the image?", and until now the only
// instrument for that question was a human looking at the monitor. That is not
// a measurement: it cannot be automated, it cannot be compared against a prior
// run, and it cannot distinguish "the view moved" from "something moved".
//
// This header owns the *format and the metric* only. The D3D11 readback lives
// in src/dll because it needs a device; everything here is pure so the format
// can be tested headless and the metric can be reasoned about without a GPU.
namespace preyvr::framedump {

// Deliberately self-describing. A dump that cannot be validated from its own
// bytes is a dump that gets silently misread three weeks later when nobody
// remembers what resolution the capture ran at.
inline constexpr char kMagic[8] = {'P', 'V', 'R', 'F', 'R', 'A', 'M', 'E'};
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 48;

// Bounds exist so a corrupt header cannot make a reader allocate wildly. The
// live swapchain is 2560x1440 (captured 2026-08-29); the ceiling is generous
// enough for a 4K/8K host without being unbounded.
inline constexpr std::uint32_t kMaxDimension = 16384;
inline constexpr std::uint64_t kMaxPayloadBytes = 1ull << 32; // 4 GiB

// DXGI_FORMAT_R8G8B8A8_UNORM. Recorded rather than assumed: the live swapchain
// is this format with SampleDesc.Count == 1, so readback needs no MSAA resolve.
// A dump arriving in another format is a signal that the capture path changed.
inline constexpr std::uint32_t kFormatR8G8B8A8Unorm = 28;

struct Header {
    std::uint32_t version = kVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t dxgiFormat = kFormatR8G8B8A8Unorm;
    std::uint32_t rowPitch = 0; // bytes per row as staged; >= width * 4
    std::uint32_t tag = 0;      // caller label: eye index, A/B marker, whatever
    std::uint64_t frameIndex = 0;
    std::uint64_t reserved = 0;
};

// Writes exactly kHeaderSize bytes. Returns false if `out` is too small.
bool EncodeHeader(const Header& header, std::span<std::uint8_t> out);

// Fail-closed: rejects a bad magic, an unknown version, zero or oversized
// dimensions, a rowPitch that cannot hold a row, and a payload whose length
// does not match height * rowPitch exactly. A truncated file is the common
// case -- a capture interrupted by a crash -- and must not decode.
std::optional<Header> DecodeHeader(std::span<const std::uint8_t> bytes);

// Total file size for a given header, or 0 if the header is not self-consistent.
std::uint64_t ExpectedFileSize(const Header& header);

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

// The numbers, not a verdict.
//
// This follows the same decision as RenderCameraResidual: a threshold is a
// property of a *metric*, and you cannot tell whether a limit is sane without
// first seeing the separation it is supposed to sit inside. Returning a bool
// here would make "the image did not change" and "we compared the wrong two
// files" indistinguishable, which is exactly the failure that cost us a day on
// the near-plane factor.
struct Difference {
    // True only if both dumps share dimensions and format. When false, every
    // number below is meaningless and must not be interpreted.
    bool comparable = false;

    double meanAbsolute = 0.0;      // mean per-channel |a-b|, 0..255
    std::uint32_t maxAbsolute = 0;  // largest single-channel difference
    double changedPixelRatio = 0.0; // fraction of pixels over perPixelEpsilon
    std::uint64_t pixelsCompared = 0;
};

// Alpha is ignored: the backbuffer's alpha channel is not meaningful for a
// presented image and including it only adds a constant to every comparison.
//
// `perPixelEpsilon` is the per-channel difference at which a pixel counts as
// changed for changedPixelRatio. It is a knob on the metric, not a pass/fail
// threshold -- see the note below.
Difference Compare(
    const Header& headerA,
    std::span<const std::uint8_t> payloadA,
    const Header& headerB,
    std::span<const std::uint8_t> payloadB,
    std::uint32_t perPixelEpsilon = 8);

// ---------------------------------------------------------------------------
// Horizontal shift scan
// ---------------------------------------------------------------------------

// Distinguishes a *shear* from real *parallax*, which `Compare` cannot.
//
// Two very different things both make a stereo pair "differ a lot":
//
//   * an asymmetric per-eye frustum shifts the whole image sideways by a
//     constant amount, so **one** horizontal shift re-aligns nearly everything;
//   * a real eye separation moves near things more than far things, so **no**
//     single shift aligns the image -- the residual stays high at every offset.
//
// Told apart, those are a passing projection and a passing IPD. Told together
// they are just "the images differ", which is what the first A2 run reported and
// why it could not be judged. Measured live 2026-08-31: a synthetic 10-degree
// frustum asymmetry produced ~256 px of shear that completely masked a 64 mm IPD.
//
// `residualAtZero / residualAtBest` is the number to read. Near 1 means no single
// shift helped, so the difference is parallax or noise. Much greater than 1 means
// a uniform shift explains most of it, and `bestShift` is how many pixels.
struct ShiftScan {
    bool comparable = false;
    int bestShift = 0;            // pixels; positive means B is shifted right of A
    double residualAtBest = 0.0;
    double residualAtZero = 0.0;
    double improvementRatio = 1.0;
    int shiftsTried = 0;
};

// Subsamples by `step` in both axes: this is a search over many candidate
// offsets, and a shear large enough to matter is visible at any sane sampling.
// `maxShift` bounds the search; beyond a few hundred pixels the overlap is too
// small for the residual to mean anything.
ShiftScan FindHorizontalShift(
    const Header& headerA,
    std::span<const std::uint8_t> payloadA,
    const Header& headerB,
    std::span<const std::uint8_t> payloadB,
    int maxShift = 320,
    int step = 4);

// **No acceptance threshold is defined here, deliberately.**
//
// Two effects will move these numbers on a live host even with an unmodified
// camera: temporal antialiasing (r_AntialiasingMode = 3) and motion blur
// (r_MotionBlur = 2) are both active, so consecutive frames of a *static* scene
// still differ. Until we have measured
//
//   - two dumps of the same frozen frame,
//   - two consecutive frozen frames (TAA jitter only),
//   - a frozen frame against a small yaw offset,
//   - a frozen frame against a large yaw offset,
//
// any constant written here would be invented rather than derived, and a gate
// built on an invented constant is worse than no gate because it looks like
// evidence. Build that table first -- the same way the render-camera limit was
// built -- then add the threshold with the table beside it.
//
// This is why `t_Scale 0` matters: freezing the simulation is what makes the
// TAA-only row measurable in isolation.

} // namespace preyvr::framedump
