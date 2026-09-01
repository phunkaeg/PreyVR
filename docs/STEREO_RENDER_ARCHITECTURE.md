# Native stereo: the render architecture, and what A4 got wrong

Prior art from `D:\Dev Debug\ss2vr-work\docs\UEVR_STEREO_LESSONS.md`, which
reviews **FEAR VR** (Lithtech Jupiter EX, D3D9, source available) and **FC2VR**
(Dunia, D3D9) -- both *injected* native-stereo mods. `INFERENCE` for Prey under
the cross-engine rule, but the architectural principle is engine-independent and
two projects reached it separately.

Found via the §3c recipe: ripgrep across the sibling repos, then `coder-next`
ranked the hit list. The box's ranking was count-driven and partly wrong -- it
put an OpenGL file top for a D3D11 question -- but the grep found the file, which
is the part that matters.

---

## The principle, which A4 already follows

> Find the seam **below all client updates and above the pure world render**.
> The smallest safe hook is that single engine call: repeat **only** it per eye,
> and simulation, input, audio and the interface all still run once per frame.

For Lithtech that call is `ILTRenderer::RenderCamera`. **For Prey it is
`RenderWorld` (R-054)** -- reached through `CSystem::m_pProcess` vtable `+0x18`,
sitting below `CSystem::Render`'s per-frame work and above the HUD.

This independently validates the move from A3 to A4. A3 re-entered
`CSystem::Render`, which is *above* the seam and drags viewport setup and the
pre/post calls along with it; A4 calls `RenderWorld` directly, which is the seam
itself. Two projects arrived at the same rule from different engines.

## Where A4 is wrong: the frame flow

Their spec, which is worth adopting close to verbatim:

1. Read the current XR render request immediately before rendering.
2. Save the original camera pose **and FOV**.
3. Apply the left eye pose.
4. Render **only** the 3D world for left; capture.
5. Apply the right eye pose.
6. Render **only** the 3D world for right; capture.
7. Restore the camera.
8. Render HUD and menus **exactly once**.
9. Present **exactly once**.

**A4 does not do this.** It lets the original `CSystem::Render` run in full --
world *and* HUD *and* present -- and then issues one extra `RenderWorld`
afterwards. So our second eye lands after the HUD has already been drawn and
after the frame has been presented.

Three consequences, all of which A4 must be reshaped to avoid:

- The second eye is composited **over** a finished frame rather than into a
  cleared target. Their loop clears the render target at the top of each eye
  pass; A4 clears nothing.
- The HUD is drawn once, but into the *first* eye only, and then the second eye
  is drawn over it. That is worse than the disparity problem A2b already found.
- Present happens between the eyes rather than after both.

The correct shape is both eye passes *inside* the hook, before the original's
HUD and present -- which means A4 cannot simply call the original and append. It
has to interpose.

## The validity gate we have not built

> Prove, from source **and** a runtime test, that the second world-render call
> does **not** double-execute simulation, AI, sound events, **particle aging**,
> or input. Particle aging and sound events are the subtle ones; a stereo path
> that silently double-ticks them looks fine for a minute and then
> desynchronises.

We have no such gate. A4's acceptance is currently "the engine survives", which
would pass a build that double-ticks particles and desynchronises after a minute
in a headset. `t_Scale 0` masks exactly this class during a frozen-scene test,
so our existing protocol cannot see it either.

**This is the make-or-break question for A4 and it should be answered before any
headset time is spent on it.**

## Things to steal directly

**A step breadcrumb.** They set a string before *every* sub-step
(`set_left_transform`, `clear_right_target`, `render_left_eye`, ...), so a crash
or hang names the exact sub-step in the log. **F-013 is the argument for this:**
when A3 wedged we knew only that four frames completed, not which step of the
fifth it died in. One pointer store per step.

**Re-entrancy guard plus a separately-scoped restore.** Their
`StereoRenderGuard` is a re-entrancy flag, and the camera restore is separate and
SEH-wrapped: *"if an eye render throws and you do not restore, you have corrupted
the flat game too."* A4 never writes the game's camera so it is less exposed, but
the double-render path was, and any future interposing shape will be.

**Per-eye extras drawn inside the eye pass.** Muzzle flash, aim guide, weapon
box and wrist HUD get correct per-eye parallax and depth for free, rather than
being pushed to OpenXR quad layers against the 16-layer budget. Relevant to our
viewmodel problem (R-069).

**Never derive a vtable slot from header declaration order.** They found slot 17
was a one-argument alias forwarding to the real implementation in slot 19, and
pinned it three ways including verifying the 15-byte forwarding stub before
patching. Our slots came from decompiled call sites rather than headers, which is
the right method -- and `pass.create_general` is byte-gated, which is their third
pin.

## Where we are ahead

**Asymmetric per-eye frusta.** Their loop sets a **symmetric** FOV as a
*"pragmatic compromise when the engine camera cannot express an off-centre
projection -- costs some FOV, avoids a hard problem."* Prey's `CCamera` carries
real `m_asymL/R/B/T` fields and `ProjectionFromTangents` already writes them, so
we do not need to pay that cost.

**Adapter LUID agreement.** Their acceptance bar lists D3D9/OpenXR adapter LUIDs
verified to match, flagged in SS2VR's notes as *"a real gotcha we do not
currently check"*. We check it (R-052) and have exercised the mismatch path in
the real game.

## Their fallback ladder, if the world render cannot be called twice

1. Official render-target / camera APIs from the SDK.
2. A small, **version-checked** hook around the engine camera render call.
3. Depth + image reprojection **only as a clearly-labelled compatibility mode**.

Worth recording that this ranks depth-warp reprojection as a **last resort, not a
goal**. If A4 fails, alternating-eye -- which A2b already proved and which
witcher3-vr ships -- sits above depth-warp on this ladder.

## Their acceptance bar

~24,900 complete stereo frames over ~11.5 minutes, multiple resolution and device
resets, repeated stereo toggling, **zero stereo-render exceptions, and no mono
fallback during continuous world render**. That is a concrete bar for A4 and it
is far beyond "survived one frame".
