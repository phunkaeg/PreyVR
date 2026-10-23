#pragma once

#include "preyvr/VrMath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The pure half of the in-game debug overlay (`dbg.draw`).
//
// **What a tester needs to see, drawn where it is.** Rays from each hand, the
// body slots the holsters and the medkit measure against, the wrist card's
// visibility test, the foregrip region. Every primitive is described in the
// app's OpenXR space -- the space the controllers, the head and the submitted
// eye poses already share -- and is projected with the exact pose and frustum
// that rendered the eye image it is drawn into. Drawn that way an overlay point
// lands on the physical point it describes in the headset, in stereo, and in
// any capture of the submitted images, without knowing anything about the
// engine's own cameras.
//
// Projection and tessellation live here so they can be tested without a GPU;
// the D3D11 half only uploads the vertices this produces.
namespace preyvr::debugdraw {

struct Color {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
};
inline constexpr Color WithAlpha(Color c, std::uint8_t a) { return {c.r, c.g, c.b, a}; }
inline std::uint32_t Pack(Color c)
{
    return std::uint32_t{c.r} | std::uint32_t{c.g} << 8 | std::uint32_t{c.b} << 16 | std::uint32_t{c.a} << 24;
}

// A segment. `width` is physical (metres), so a ray thins with distance and
// reads at the right depth; it never drops below `minPixels`. `dash` > 0 draws
// dashes of that length (metres) separated by equal gaps.
struct Line {
    Vec3 a{}, b{};
    Color color{};
    float width = .004f;
    float minPixels = 1.5f;
    float dash = 0.0f;
};
// A sphere: its exact silhouette (the circle where the eye's lines of sight
// graze it, filled and outlined) plus its horizontal great circle, which is
// what makes it read as a ball rather than a coin.
struct Sphere {
    Vec3 centre{};
    float radius = .1f;
    Color outline{};
    Color fill{};
    float outlinePixels = 2.5f;
};
// A dot or ring that faces the eye, sized in metres with a pixel floor.
struct Marker {
    Vec3 centre{};
    float radius = .02f;
    Color color{};
    bool filled = true;
    float minPixels = 5.0f;
    float ringPixels = 2.5f;  // ring thickness when not filled
};
// Text anchored at a world point. Height is physical with a pixel clamp, so it
// is readable both at arm's length and across a room. Offsets are in units of
// the glyph height, applied in screen space after projection.
struct Label {
    Vec3 anchor{};
    std::string text;
    Color color{};
    float height = .022f;
    float minPixels = 20.0f, maxPixels = 46.0f;
    float offsetX = 0.0f, offsetY = 0.0f;
    bool centred = false;
    bool panel = true;  // dark plate behind the text
    bool keepInside = true;  // slide the block into the image while its anchor is visible
};
struct Scene {
    std::vector<Line> lines;
    std::vector<Sphere> spheres;
    std::vector<Marker> markers;
    std::vector<Label> labels;
    void Clear() { lines.clear(); spheres.clear(); markers.clear(); labels.clear(); }
};

// One rendered eye: its pose in OpenXR space and the tangents of its frustum
// (left/down negative), exactly as submitted, and the image size in pixels.
struct EyeView {
    Pose pose{};
    float left = -1, right = 1, up = 1, down = -1;
    float width = 0, height = 0;
    // The part of the image the headset actually shows (the runtime's own
    // frustum, in pixels). Labels are kept inside it. Empty = the whole image.
    float shownX0 = 0, shownY0 = 0, shownX1 = 0, shownY1 = 0;
};
// The pixel rectangle of `eye`'s image that a display frustum with these
// tangents covers, ignoring the small rotation between render and display.
void SetShownFrustum(EyeView& eye, float left, float right, float up, float down);

struct Projected {
    float x = 0, y = 0;   // pixels, origin top-left
    float depth = 0;      // metres along the view axis
};
inline constexpr float kNearMetres = .03f;
std::optional<Projected> Project(const EyeView& eye, Vec3 world, float nearMetres = kNearMetres);
// Pixels per metre at one metre of depth (the focal length in pixels).
float FocalPixels(const EyeView& eye);
// Clips a world segment to the part in front of the eye's near plane. False
// when nothing remains.
bool ClipToNear(const EyeView& eye, Vec3& a, Vec3& b, float nearMetres = kNearMetres);

// The glyph atlas: printable ASCII 32..127 in a 16 x 6 grid of equal cells.
// The D3D half rasterises a monospace font into exactly this layout.
inline constexpr int kAtlasColumns = 16, kAtlasRows = 6;
inline constexpr int kCellWidth = 22, kCellHeight = 44;
inline constexpr int kAtlasWidth = kAtlasColumns * kCellWidth, kAtlasHeight = kAtlasRows * kCellHeight;
// Monospace: every glyph advances one cell, so a cell's aspect is the advance.
inline constexpr float kGlyphAdvance = static_cast<float>(kCellWidth) / static_cast<float>(kCellHeight);
inline constexpr float kLineSpacing = 1.08f;  // line pitch / glyph height

enum class Kind : std::uint32_t { Edge = 0, Glyph = 1, Disc = 2 };
// Pixel-space vertex. Edge: (u,v) = (across, 0) in [-1,1], `extent` = half
// width in pixels including the 1 px feather. Glyph: (u,v) atlas coordinates.
// Disc: (u,v) in [-1,1], `extent` = radius in pixels, `inner` = hole radius as
// a fraction of the radius (0 = filled).
struct Vertex {
    float x = 0, y = 0;
    float u = 0, v = 0;
    std::uint32_t color = 0;
    float kind = 0, extent = 0, inner = 0;
};
static_assert(sizeof(Vertex) == 32);

struct TessellateStats {
    std::size_t lines = 0, spheres = 0, markers = 0, labels = 0;
    std::size_t culled = 0;      // primitives entirely behind the eye or off-image
    bool truncated = false;      // hit the vertex budget
};
// Appends triangles (three vertices each) for the whole scene, lines first and
// labels last so text sits on top. Never exceeds `maxVertices`.
TessellateStats Tessellate(const Scene& scene, const EyeView& eye, std::vector<Vertex>& out,
                           std::size_t maxVertices);

// Geometry helpers shared by the scene builders.
Vec3 Add(Vec3 a, Vec3 b);
Vec3 Sub(Vec3 a, Vec3 b);
Vec3 Scale(Vec3 a, float s);
float Dot(Vec3 a, Vec3 b);
float Length(Vec3 a);
Vec3 Cross(Vec3 a, Vec3 b);
// Any unit vector perpendicular to the unit vector `axis`.
Vec3 Perpendicular(Vec3 axis);
Quaternion Conjugate(Quaternion q);
// The silhouette of a sphere seen from `eye`: the circle of tangency, as its
// centre, radius and plane normal (towards the eye). False when the eye is
// inside or on the sphere.
bool SphereSilhouette(Vec3 eye, Vec3 centre, float radius, Vec3& circleCentre, float& circleRadius, Vec3& normal);
// A circle in world space as `segments` lines in the plane through `centre`
// spanned by unit vectors `u` and `v`.
void AppendCircle(Scene& scene, Vec3 centre, Vec3 u, Vec3 v, float radius, Color color, float width,
                  int segments = 48, float dash = 0.0f);
// Three short axes (red X, green Y, blue Z) of a pose.
void AppendAxes(Scene& scene, const Pose& pose, float length, float width = .003f);

}  // namespace preyvr::debugdraw
