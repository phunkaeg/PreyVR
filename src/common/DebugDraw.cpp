#include "preyvr/DebugDraw.h"

#include <algorithm>
#include <cmath>

namespace preyvr::debugdraw {

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 Perpendicular(Vec3 axis)
{
    const Vec3 seed = std::fabs(axis.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 p = Cross(axis, seed);
    const float n = Length(p);
    return n > 1e-6f ? Scale(p, 1 / n) : Vec3{1, 0, 0};
}
Quaternion Conjugate(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }

bool SphereSilhouette(Vec3 eye, Vec3 centre, float radius, Vec3& circleCentre, float& circleRadius, Vec3& normal)
{
    const Vec3 toEye = Sub(eye, centre);
    const float d = Length(toEye);
    if (!std::isfinite(d) || !(radius > 0) || d <= radius * 1.001f) { return false; }
    // Lines of sight graze the sphere where (T - C).(T - E) = 0: a circle in
    // the plane R^2/d from the centre towards the eye, of radius R sqrt(1 - R^2/d^2).
    normal = Scale(toEye, 1 / d);
    circleCentre = Add(centre, Scale(normal, radius * radius / d));
    circleRadius = radius * std::sqrt(1 - (radius / d) * (radius / d));
    return true;
}

namespace {

bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// View space: OpenXR convention, -Z forward, +Y up, +X right.
Vec3 ToView(const EyeView& eye, Vec3 world)
{
    return Rotate(Conjugate(Normalize(eye.pose.orientation)), Sub(world, eye.pose.position));
}

Projected FromView(const EyeView& eye, Vec3 view)
{
    const float depth = -view.z;
    const float tx = view.x / depth, ty = view.y / depth;
    return {(tx - eye.left) / (eye.right - eye.left) * eye.width,
            (eye.up - ty) / (eye.up - eye.down) * eye.height, depth};
}

bool ValidEye(const EyeView& eye)
{
    return eye.width > 0 && eye.height > 0 && eye.right > eye.left && eye.up > eye.down &&
           std::isfinite(eye.left) && std::isfinite(eye.right) && std::isfinite(eye.up) &&
           std::isfinite(eye.down) && Finite(eye.pose.position);
}

float FocalY(const EyeView& eye) { return eye.height / (eye.up - eye.down); }

struct Out {
    std::vector<Vertex>& v;
    std::size_t max;
    TessellateStats& stats;
    bool Room(std::size_t n)
    {
        if (v.size() + n > max) { stats.truncated = true; return false; }
        return true;
    }
    void Quad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d)
    {
        v.push_back(a); v.push_back(b); v.push_back(c);
        v.push_back(c); v.push_back(b); v.push_back(d);
    }
    void Tri(const Vertex& a, const Vertex& b, const Vertex& c) { v.push_back(a); v.push_back(b); v.push_back(c); }
};

Vertex Make(float x, float y, float u, float v, std::uint32_t color, Kind kind, float extent, float inner = 0)
{
    return {x, y, u, v, color, static_cast<float>(static_cast<std::uint32_t>(kind)), extent, inner};
}

// True when a pixel-space box lies wholly outside the image (with a margin).
bool OffImage(const EyeView& eye, float x0, float y0, float x1, float y1, float margin)
{
    return std::max(x0, x1) < -margin || std::min(x0, x1) > eye.width + margin ||
           std::max(y0, y1) < -margin || std::min(y0, y1) > eye.height + margin;
}

void Segment(Out& out, const EyeView& eye, Vec3 a, Vec3 b, const Line& line)
{
    if (!ClipToNear(eye, a, b)) { return; }
    const Projected p0 = FromView(eye, ToView(eye, a));
    const Projected p1 = FromView(eye, ToView(eye, b));
    const float focal = FocalPixels(eye);
    const float h0 = std::max(line.width / p0.depth * focal, line.minPixels) * .5f;
    const float h1 = std::max(line.width / p1.depth * focal, line.minPixels) * .5f;
    if (OffImage(eye, p0.x, p0.y, p1.x, p1.y, std::max(h0, h1) + 2)) { return; }
    const float dx = p1.x - p0.x, dy = p1.y - p0.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length < 1e-3f || !out.Room(6)) { return; }
    // Perpendicular in pixels; each end is widened by its own half width plus
    // a one pixel feather the pixel shader fades across.
    const float nx = -dy / length, ny = dx / length;
    const float e0 = h0 + 1, e1 = h1 + 1;
    const auto c = Pack(line.color);
    out.Quad(Make(p0.x + nx * e0, p0.y + ny * e0, 1, 0, c, Kind::Edge, e0),
             Make(p0.x - nx * e0, p0.y - ny * e0, -1, 0, c, Kind::Edge, e0),
             Make(p1.x + nx * e1, p1.y + ny * e1, 1, 0, c, Kind::Edge, e1),
             Make(p1.x - nx * e1, p1.y - ny * e1, -1, 0, c, Kind::Edge, e1));
}

void LineGeometry(Out& out, const EyeView& eye, const Line& line)
{
    if (!Finite(line.a) || !Finite(line.b) || !(line.width >= 0)) { return; }
    if (line.dash <= 0) { Segment(out, eye, line.a, line.b, line); return; }
    const Vec3 d = Sub(line.b, line.a);
    const float total = Length(d);
    if (!(total > 0)) { return; }
    const Vec3 unit = Scale(d, 1 / total);
    // Never more than 400 dashes: a dash length that would need more becomes
    // longer instead, so a 200 m ray cannot exhaust the vertex budget.
    const float dash = std::max(line.dash, total / 800.0f);
    for (float s = 0; s < total; s += 2 * dash) {
        Segment(out, eye, Add(line.a, Scale(unit, s)), Add(line.a, Scale(unit, std::min(s + dash, total))), line);
    }
}

void Disc(Out& out, float x, float y, float radius, float inner, Color color)
{
    if (!(radius > .25f) || !out.Room(6)) { return; }
    const float e = radius + 1;   // one pixel of feather outside the edge
    const float k = e / radius;
    const auto c = Pack(color);
    out.Quad(Make(x - e, y - e, -k, -k, c, Kind::Disc, radius, inner),
             Make(x + e, y - e, k, -k, c, Kind::Disc, radius, inner),
             Make(x - e, y + e, -k, k, c, Kind::Disc, radius, inner),
             Make(x + e, y + e, k, k, c, Kind::Disc, radius, inner));
}

void Rect(Out& out, float x0, float y0, float x1, float y1, Color color)
{
    if (!out.Room(6)) { return; }
    const auto c = Pack(color);
    out.Quad(Make(x0, y0, 0, 0, c, Kind::Edge, 1), Make(x1, y0, 0, 0, c, Kind::Edge, 1),
             Make(x0, y1, 0, 0, c, Kind::Edge, 1), Make(x1, y1, 0, 0, c, Kind::Edge, 1));
}

void Glyphs(Out& out, const std::string& text, float x, float y, float h, Color color)
{
    const float advance = h * kGlyphAdvance;
    const auto c = Pack(color);
    for (char raw : text) {
        unsigned code = static_cast<unsigned char>(raw);
        if (code < 32 || code > 127) { code = '?'; }
        if (code != ' ') {
            if (!out.Room(6)) { return; }
            const unsigned index = code - 32;
            const float u0 = static_cast<float>((index % kAtlasColumns) * kCellWidth) / kAtlasWidth;
            const float v0 = static_cast<float>((index / kAtlasColumns) * kCellHeight) / kAtlasHeight;
            const float u1 = u0 + static_cast<float>(kCellWidth) / kAtlasWidth;
            const float v1 = v0 + static_cast<float>(kCellHeight) / kAtlasHeight;
            out.Quad(Make(x, y, u0, v0, c, Kind::Glyph, 0), Make(x + advance, y, u1, v0, c, Kind::Glyph, 0),
                     Make(x, y + h, u0, v1, c, Kind::Glyph, 0), Make(x + advance, y + h, u1, v1, c, Kind::Glyph, 0));
        }
        x += advance;
    }
}

}  // namespace

std::optional<Projected> Project(const EyeView& eye, Vec3 world, float nearMetres)
{
    if (!ValidEye(eye) || !Finite(world)) { return std::nullopt; }
    const Vec3 view = ToView(eye, world);
    if (!(-view.z >= nearMetres)) { return std::nullopt; }
    return FromView(eye, view);
}

float FocalPixels(const EyeView& eye) { return eye.width / (eye.right - eye.left); }

void SetShownFrustum(EyeView& eye, float left, float right, float up, float down)
{
    if (!ValidEye(eye) || !(right > left) || !(up > down) || !std::isfinite(left) || !std::isfinite(right) ||
        !std::isfinite(up) || !std::isfinite(down)) {
        eye.shownX0 = eye.shownY0 = eye.shownX1 = eye.shownY1 = 0;
        return;
    }
    const auto px = [&](float t) { return std::clamp((t - eye.left) / (eye.right - eye.left) * eye.width, 0.f, eye.width); };
    const auto py = [&](float t) { return std::clamp((eye.up - t) / (eye.up - eye.down) * eye.height, 0.f, eye.height); };
    eye.shownX0 = px(left);
    eye.shownX1 = px(right);
    eye.shownY0 = py(up);
    eye.shownY1 = py(down);
}

bool ClipToNear(const EyeView& eye, Vec3& a, Vec3& b, float nearMetres)
{
    const float da = -ToView(eye, a).z, db = -ToView(eye, b).z;
    if (!std::isfinite(da) || !std::isfinite(db) || (da < nearMetres && db < nearMetres)) { return false; }
    if (da < nearMetres) { a = Add(a, Scale(Sub(b, a), (nearMetres - da) / (db - da))); }
    else if (db < nearMetres) { b = Add(b, Scale(Sub(a, b), (nearMetres - db) / (da - db))); }
    return true;
}

TessellateStats Tessellate(const Scene& scene, const EyeView& eye, std::vector<Vertex>& vertices,
                           std::size_t maxVertices)
{
    TessellateStats stats{};
    if (!ValidEye(eye)) { return stats; }
    Out out{vertices, maxVertices, stats};
    const float focal = FocalPixels(eye);

    // Sphere fills first so every line and ring of the scene stays readable on top.
    std::vector<Line> outlines;
    for (const auto& sphere : scene.spheres) {
        ++stats.spheres;
        Scene ring;
        AppendCircle(ring, sphere.centre, {1, 0, 0}, {0, 0, 1}, sphere.radius, WithAlpha(sphere.outline,
                     static_cast<std::uint8_t>(sphere.outline.a * 3 / 4)), .0025f, 40);
        for (auto& l : ring.lines) { l.minPixels = 1.2f; outlines.push_back(l); }
        Vec3 rimCentre{}, normal{};
        float rimRadius = 0;
        if (!SphereSilhouette(eye.pose.position, sphere.centre, sphere.radius, rimCentre, rimRadius, normal)) {
            ++stats.culled;
            continue;
        }
        // The exact outline: off-axis and close up it is an ellipse, wider
        // than any disc sized from the angular radius.
        constexpr int kRim = 48;
        const Vec3 u = Perpendicular(normal), w = Cross(normal, u);
        std::array<Vec3, kRim> rim{};
        std::array<std::optional<Projected>, kRim> projected{};
        bool whole = true;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int i = 0; i < kRim; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kRim;
            rim[i] = Add(rimCentre, Add(Scale(u, rimRadius * std::cos(a)), Scale(w, rimRadius * std::sin(a))));
            projected[i] = Project(eye, rim[i]);
            if (!projected[i]) { whole = false; continue; }
            x0 = std::min(x0, projected[i]->x); x1 = std::max(x1, projected[i]->x);
            y0 = std::min(y0, projected[i]->y); y1 = std::max(y1, projected[i]->y);
        }
        if (whole && OffImage(eye, x0, y0, x1, y1, 2)) { ++stats.culled; continue; }
        for (int i = 0; i < kRim; ++i) {
            Line edge{rim[i], rim[(i + 1) % kRim], sphere.outline, 0.0f, sphere.outlinePixels};
            outlines.push_back(edge);
        }
        const auto middle = Project(eye, rimCentre);
        if (whole && middle && sphere.fill.a && out.Room(3 * kRim)) {
            const auto c = Pack(sphere.fill);
            const Vertex hub = Make(middle->x, middle->y, 0, 0, c, Kind::Edge, 1);
            for (int i = 0; i < kRim; ++i) {
                const auto& a = *projected[i];
                const auto& b = *projected[(i + 1) % kRim];
                out.Tri(hub, Make(a.x, a.y, 0, 0, c, Kind::Edge, 1), Make(b.x, b.y, 0, 0, c, Kind::Edge, 1));
            }
        }
    }
    for (const auto& line : outlines) { LineGeometry(out, eye, line); }
    for (const auto& line : scene.lines) { ++stats.lines; LineGeometry(out, eye, line); }
    for (const auto& marker : scene.markers) {
        ++stats.markers;
        const auto p = Project(eye, marker.centre);
        if (!p) { ++stats.culled; continue; }
        const float radius = std::max(marker.radius / p->depth * focal, marker.minPixels);
        Disc(out, p->x, p->y, radius, marker.filled ? 0 : std::max(0.f, 1 - marker.ringPixels / radius), marker.color);
    }
    struct Box { float x0, y0, x1, y1; };
    std::vector<Box> placed;
    const bool hasShown = eye.shownX1 > eye.shownX0 && eye.shownY1 > eye.shownY0;
    const float shown[4] = {hasShown ? eye.shownX0 : 0, hasShown ? eye.shownY0 : 0,
                            hasShown ? eye.shownX1 : eye.width, hasShown ? eye.shownY1 : eye.height};
    for (const auto& label : scene.labels) {
        ++stats.labels;
        const auto p = Project(eye, label.anchor);
        if (!p || label.text.empty()) { ++stats.culled; continue; }
        const float h = std::clamp(label.height / p->depth * FocalY(eye), label.minPixels, label.maxPixels);
        std::vector<std::string> rows;
        std::size_t start = 0;
        for (;;) {
            const auto end = label.text.find('\n', start);
            rows.push_back(label.text.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) { break; }
            start = end + 1;
        }
        std::size_t longest = 0;
        for (const auto& row : rows) { longest = std::max(longest, row.size()); }
        const float width = static_cast<float>(longest) * h * kGlyphAdvance;
        const float height = static_cast<float>(rows.size()) * h * kLineSpacing;
        float x0 = p->x + label.offsetX * h - (label.centred ? width * .5f : 0);
        float y0 = p->y + label.offsetY * h;
        // A label whose anchor is in view slides inside what the headset shows
        // rather than being cut at its edge, and steps clear of labels already
        // placed; one whose anchor is out of view is not drawn.
        const bool anchorVisible = p->x >= shown[0] && p->x <= shown[2] && p->y >= shown[1] && p->y <= shown[3];
        const float margin = .5f * h;
        const auto keepIn = [&]() {
            if (width + 2 * margin < shown[2] - shown[0]) { x0 = std::clamp(x0, shown[0] + margin, shown[2] - margin - width); }
            if (height + 2 * margin < shown[3] - shown[1]) { y0 = std::clamp(y0, shown[1] + margin, shown[3] - margin - height); }
        };
        // Text about something out of view is a fragment at the edge, not information.
        if (label.keepInside && !anchorVisible) { ++stats.culled; continue; }
        if (label.keepInside) {
            keepIn();
            for (int attempt = 0; attempt < 8; ++attempt) {
                const Box* hit = nullptr;
                for (const auto& b : placed) {
                    if (x0 < b.x1 && x0 + width > b.x0 && y0 < b.y1 && y0 + height > b.y0) { hit = &b; break; }
                }
                if (!hit) { break; }
                // Below the obstacle, or above it when that is the shorter move.
                const float down = hit->y1 + .4f * h - y0, up = y0 + height + .4f * h - hit->y0;
                y0 += (down <= up || y0 - up < shown[1]) ? down : -up;
                keepIn();
            }
        }
        if (OffImage(eye, x0, y0, x0 + width, y0 + height, h)) { ++stats.culled; continue; }
        placed.push_back({x0 - .35f * h, y0 - .2f * h, x0 + width + .35f * h, y0 + height + .1f * h});
        if (label.panel) {
            Rect(out, x0 - .35f * h, y0 - .2f * h, x0 + width + .35f * h, y0 + height + .1f * h,
                 Color{8, 10, 14, static_cast<std::uint8_t>(label.color.a * 160 / 255)});
        }
        const float shadow = std::max(1.0f, h / 20);
        const Color dark{0, 0, 0, static_cast<std::uint8_t>(label.color.a * 220 / 255)};
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const float y = y0 + static_cast<float>(r) * h * kLineSpacing;
            Glyphs(out, rows[r], x0 + shadow, y + shadow, h, dark);
            Glyphs(out, rows[r], x0, y, h, label.color);
        }
    }
    return stats;
}

void AppendCircle(Scene& scene, Vec3 centre, Vec3 u, Vec3 v, float radius, Color color, float width,
                  int segments, float dash)
{
    segments = std::clamp(segments, 6, 256);
    Vec3 previous = Add(centre, Scale(u, radius));
    for (int i = 1; i <= segments; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        const Vec3 next = Add(centre, Add(Scale(u, radius * std::cos(angle)), Scale(v, radius * std::sin(angle))));
        Line line{previous, next, color, width};
        line.dash = dash;
        scene.lines.push_back(line);
        previous = next;
    }
}

void AppendAxes(Scene& scene, const Pose& pose, float length, float width)
{
    const Quaternion q = Normalize(pose.orientation);
    const Color colors[3] = {{255, 70, 70, 255}, {80, 235, 80, 255}, {80, 150, 255, 255}};
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i) {
        scene.lines.push_back({pose.position, Add(pose.position, Scale(Rotate(q, axes[i]), length)), colors[i], width});
    }
}

}  // namespace preyvr::debugdraw
