#pragma once

#include "preyvr/VrMath.h"

// The status hologram over the back of the left forearm: where it goes, when
// it shows, and what may hide it. Pure, so every rule is tested offline.
//
// **Placed on the drawn arm, not on the controller.** The card is positioned
// from the game's own left forearm as skinned in the frame being submitted
// (wrist and elbow joints, the hand's palm normal), so it moves with the
// sleeve the wearer sees instead of sliding over it when the arm moves fast.
//
// **Shown only when looked at.** A watch check: the back of the forearm
// turned towards the eyes AND the card near the centre of the gaze, held for a
// moment, then a short fade and unfold. Leaving either condition fades it out;
// anything that would make it draw over the scene wrongly (the arm in a wall,
// the other hand or its weapon in front) hides it quickly.
namespace preyvr::wrist {

// The drawn left forearm, all in the app's OpenXR space.
struct Arm {
    Vec3 wrist{};    // wrist joint
    Vec3 elbow{};    // forearm joint (the elbow)
    Vec3 palm{};     // the drawn hand's palm normal, out of the palm (any length)
};

struct Placement {
    float along = 0.075f;     // from the wrist joint towards the elbow, metres
    float hover = 0.042f;     // from the forearm's axis, out through its back, metres
    float width = 0.125f;     // card width at 100 % (metres)
    float maxTiltDeg = 40.0f; // turned about the forearm towards the eye, at most
    // The text's reading direction: 0 along the forearm (towards the hand);
    // 1 along the forearm while that is near the viewer's horizontal, turning
    // towards it beyond (uprightFrom..uprightTo deg apart); 2 always level.
    int upright = 1;
    float uprightFromDeg = 25.0f, uprightToDeg = 60.0f;
};

struct Card {
    bool valid = false;
    Pose pose{};          // OpenXR quad pose: +X the text's reading direction, +Z the face
    Vec3 axis{};          // unit, elbow -> wrist
    Vec3 dorsal{};        // unit, out of the back of the forearm (before the tilt)
    float tiltDeg = 0.0f; // applied tilt towards the eye
    float rollDeg = 0.0f; // the text turned away from the forearm, in the card's plane
};

// `head`: the centre eye and its orientation (the tilt faces its position, the
// text levels with its horizontal). False (invalid card) if the arm is degenerate.
Card PlaceCard(const Arm& arm, const Pose& head, const Placement& placement);

// How the card is being looked at.
struct View {
    float distance = 0.0f; // eye -> card centre, metres
    float facing = 0.0f;   // cos: back of the forearm vs the direction to the eye
    float viewing = 0.0f;  // cos: gaze vs the direction to the card
};
View Measure(const Card& card, Pose head);

struct Thresholds {
    float enterFacing = 0.50f, exitFacing = 0.22f;   // ~60 deg in, ~77 deg out
    float enterViewing = 0.82f, exitViewing = 0.62f; // ~35 deg in, ~52 deg out
    float minDistance = 0.12f, maxDistance = 0.80f;
    float dwell = 0.08f;        // s the look must hold before it shows
    float maxEnterSpeed = 1.2f; // m/s: a hand sweeping past the gaze does not open it
    float fadeIn = 0.14f, fadeOut = 0.20f, fastOut = 0.07f; // s
};

// Per XR frame inputs beyond the view.
struct Conditions {
    bool eligible = false;  // gameplay, tracking, no menu, not aiming two-handed, arm known
    bool occluded = false;  // something is between the eye and the card
    float handSpeed = 0.0f; // m/s, the left hand
};

// The fade state. Update once per submitted frame.
class Presenter {
public:
    void Update(const View& view, const Conditions& conditions, float dt, const Thresholds& t = {});
    float Alpha() const { return alpha_; }
    // 0.9 -> 1 as it fades in (an unfold); 1 while shown or fading out.
    float Scale() const;
    bool Showing() const { return target_; }
    // The looser condition the card keeps while shown (for the occlusion query:
    // it only needs to run while this holds).
    bool Candidate() const { return candidate_; }
    void Reset() { *this = Presenter{}; }

private:
    float alpha_ = 0.0f;
    float held_ = 0.0f;
    bool target_ = false;
    bool rising_ = false;
    bool candidate_ = false;
};

// Occlusion by something we know the shape of (the other hand, its weapon).
bool SegmentNearPoint(Vec3 a, Vec3 b, Vec3 point, float radius);
bool SegmentNearSegment(Vec3 a, Vec3 b, Vec3 p, Vec3 q, float radius);

}  // namespace preyvr::wrist
