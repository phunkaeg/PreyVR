#pragma once
#include "preyvr/VrMath.h"
#include <array>
#include <optional>

namespace preyvr::ui {
struct Eye { Pose pose{}; float left=0, right=0, up=0, down=0; };
struct Panel { Pose pose{}; float width=0, height=0; };
// Fits all four corners inside BOTH eye frusta, including asymmetric/canted views.
// The runtime FOV is an optical bound, not a visibility-mask guarantee; the
// tangent margin and angular caps deliberately reserve room around the panel.
// `scale` shrinks the fit below the angular caps. It exists because the caps
// are 60 deg horizontal / 40 vertical and the corner test only shrinks until the
// corners fit the frustum the RUNTIME REPORTS -- which on a Quest 3 is wider
// than the lenses actually show, so a panel that passes the test is still
// cropped at the edges by the optics. A wearer is the only instrument that can
// settle where the real edge is, so this is a dial rather than a constant.
// Clamped to [0.2, 2.0]. Above 1 exceeds the default caps deliberately: they are
// a comfort choice, not a limit, and the corner test still refuses a panel whose
// corners leave the frustum. At a portrait render aspect the vertical cap binds
// and forces the panel narrow, which is precisely when a wearer needs more.
//
// `margin` is the fraction of the reported frustum a panel must stay inside,
// and it is the REAL ceiling on size -- not `scale`. Both fit loops shrink
// until every sampled point sits within it, so once the margin binds, raising
// `scale` changes nothing at all: a wearer at 2688x2880 found the panel stopped
// growing at about 130 percent and reasonably read that as the maximum.
// Default 0.72, clamped to [0.3, 1.0]. Raising it trades the safety comment
// above for size, which is the wearer's trade to make and is reversible.
inline constexpr float kDefaultFitMargin=.72f;
std::optional<Panel> FitPanel(const Pose& head, const std::array<Eye,2>& eyes,
                             float aspect, float distance=2.0f, float scale=1.0f,
                             float margin=kDefaultFitMargin);
bool CornersVisible(const Panel& panel, const std::array<Eye,2>& eyes,
                    float margin=kDefaultFitMargin);
// angle=0 is a plane; otherwise width is arc length. The panel pose always
// denotes the visible surface centre, while OpenXR's cylinder pose is its axis.
struct Surface { Panel panel{}; float angle=0; };
struct RayHit { float u=0,v=0,distance=0; Vec3 point{}; bool inside=false; };
Pose CylinderAxis(const Surface& surface);
Pose SurfacePoint(const Surface& surface,float u,float v);
std::optional<RayHit> Intersect(const Surface& surface,const Pose& aim);
bool SurfaceVisible(const Surface& surface,const std::array<Eye,2>& eyes,
                    float margin=kDefaultFitMargin);
// Neutral on entry; losing the target releases outside it, never clicks the
// last hovered item. A captured drag can travel beyond the screen boundary.
struct PointerButtons {
    bool armed=false,down=false;
    int Update(bool active,bool inside,bool pressed); // -1 release, +1 press
};
// Unclipped canvas position of a head-relative ray on a front-facing HUD plane.
std::optional<std::array<float,2>> PanelFraction(float tanX,float tanY,float width,float height,float distance);
}
