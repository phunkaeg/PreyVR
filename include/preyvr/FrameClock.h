#pragma once

#include <cstdint>

// Three clocks, and the eye that falls out of them.
//
// **The current lane picks its eye from a free-running counter**
// (`gEyeCounter.fetch_add(1) & 1`), which is inherited ban I-08: inferring the
// eye from parity of something that is not the engine's own frame phase. It is
// right exactly while nothing is ever skipped, and this project measured two
// decoupled threads -- game 44208, render 54600 (R-064) -- so something will be.
//
// The playbook's 2026-09-10 comparison names the alternative: an alternate-frame
// mod keeps **engine**, **render** and **present** frames in lockstep, takes eye
// cadence from the presenter's parity, and recovers drift by skipping a present
// rather than by renumbering.
//
// This is the pure half of that: no hooks, no globals, just the arithmetic and
// the refusals, so the decision can be tested without a game.
namespace preyvr::frameclock {

struct Timeline {
    // The game's simulation frame.
    std::uint64_t engine = 0;
    // The render thread's frame. Legitimately trails `engine`.
    std::uint64_t render = 0;
    // The presented frame. Eye cadence derives from THIS, because it is the one
    // that advances once per image the wearer actually sees.
    std::uint64_t present = 0;
};

struct EyeDecision {
    // 0 or 1, or -1 meaning refuse. A refusal must hold the previous pair rather
    // than guess: a wrong eye label puts one eye's pixels in the other's slot,
    // which reads in a headset as depth inverted rather than as a dropped frame.
    int eye = -1;
    // render - present, signed. Positive means the render thread is ahead.
    std::int64_t drift = 0;
    // Drift exceeded the tolerance, so `eye` is -1 and a present should be
    // skipped to let the clocks re-converge.
    bool drifted = false;
    // A clock went backwards or the timeline is unusable.
    bool invalid = false;
};

// `tolerance` is how many frames of render-ahead-of-present is normal for this
// engine; beyond it the parity can no longer be trusted to name the eye.
EyeDecision DecideEye(const Timeline& previous, const Timeline& current,
                      std::uint64_t tolerance);

// **Per-eye temporal history.**
//
// R-064: `CRenderView::SetPreviousFrameCamera` is called after every `SetCamera`
// with a different camera, and is load-bearing for motion vectors and temporal
// reprojection. With one eye rendered per frame, the "previous frame" the engine
// hands eye 0 is eye 1's -- a different viewpoint -- so every temporal effect
// resolves against the wrong history. That is the mechanism behind the displaced
// weapon ghost this project saw under temporal AA.
//
// The fix is one slot per eye rather than one shared slot. This holds the
// history and answers what the engine should be told for the eye about to
// render, so the policy is testable separately from the hook that applies it.
template <typename Camera>
class EyeHistory {
public:
    // Returns false when this eye has no history yet -- the first frame of each
    // eye. The caller must then leave the engine's own value alone rather than
    // substitute the other eye's, which is the whole defect.
    bool Previous(int eye, Camera& out) const
    {
        if (eye < 0 || eye > 1 || !mValid[eye]) { return false; }
        out = mCamera[eye];
        return true;
    }

    void Record(int eye, const Camera& camera)
    {
        if (eye < 0 || eye > 1) { return; }
        mCamera[eye] = camera;
        mValid[eye] = true;
    }

    // Cleared on anything that makes an old viewpoint meaningless: recentre, a
    // reference change, a level load. Keeping a stale history across one of
    // those reprojects against a viewpoint that no longer exists.
    void Reset()
    {
        mValid[0] = mValid[1] = false;
        mCamera[0] = Camera{};
        mCamera[1] = Camera{};
    }

    bool Has(int eye) const { return eye >= 0 && eye <= 1 && mValid[eye]; }

private:
    Camera mCamera[2]{};
    bool mValid[2]{false, false};
};

} // namespace preyvr::frameclock
