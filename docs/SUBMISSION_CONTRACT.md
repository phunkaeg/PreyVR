# What PreyVR must tell the OpenXR runtime

Sourced from the `openxr-submission` skill and the playbook at
`D:\Dev Debug\VR Modding\docs`. **This file records only what applies to *this*
target and what it changes about our plan** -- the skill is the retrieval path
and the playbook is the record, and duplicating them here would just create a
third copy to drift.

Everything below is `INFERENCE` for Prey until confirmed against these bytes,
under the same rule as any cross-project finding.

---

## The correction this forces, and it inverts an acceptance criterion

**We have been reading `submitted_fov_matches_located` backwards.**

That check compares the FOV we declare in the projection layer against the FOV
`xrLocateViews` reported. It passed at a maximum difference of **0 rad** across
400 frames on a Quest 3, and across every xr-sim run, and it was reported here
three times as evidence the submission path is correct.

For `preyvr_xr_session_probe` that reading is honest: the probe **owns its
rendering**. It clears two slices through whatever frustum the runtime asked for,
so declaring that frustum back is a true statement about the pixels.

**PreyVR is not that.** It is an *injected* mod: it cannot change Prey's
projection, so its pixels come from the game's frustum. The moment we submit
Prey's frames, declaring the runtime's FOV becomes a lie about the image, and
`submitted_fov_matches_located` passing would mean we told that lie
successfully.

> **Expect that check to FAIL once the mod is correct, and expect the failure to
> be large** -- around 20 degrees is normal.

The probe's green result stays valid *for the probe*. It says nothing about the
mod, and it must not be carried over as though it did. The generalisation is
worth keeping: a check comparing what we told the runtime against what the
runtime said tests **plumbing honesty, not correctness** -- it cannot see pixels.

## What we must declare instead

The frustum we actually rendered, derived from the game's own projection as
**tangent half-extents** rather than angles:

```
tanH = 1/m00     angleRight = atan(tanH)     angleLeft = -angleRight
tanV = 1/m11     angleUp    = atan(tanV)     angleDown  = -angleUp
```

If Prey renders one symmetric view for both eyes, **both eyes declare the same
fov**, and that is correct rather than a shortcut.

Work in tangents throughout. This project has already paid for that lesson once
in a different currency: F-011's shear estimate was 45% wrong because it was
computed in degrees-space, and the error looked entirely plausible until it was
checked against a measurement.

## Declare the WORLD camera, not the viewmodel's

The skill's central selection trap is one we have already measured. Prey draws
the world at **88 degrees** and the viewmodel at **`r_DrawNearFoV` 54**, latched
once per frame into `CD3D9Renderer+0x95B4` (R-069). Both are legitimate
player-camera constructions, so any "is this the player's projection?" test
passes for both and whichever arrives last silently wins.

Sibling values, for calibration only -- note the ordering is **not** consistent,
so nothing can be assumed from ours being the wider one:

| game | world | viewmodel |
| --- | --- | --- |
| BioShock (UE2.5) | 75 | 60 |
| SWAT 4 + SEF (UE2.5) | 85 | 120 |
| **Prey (CryEngine)** | **88** | **54** |

Selection rule that needs no per-build offset: the world is drawn before the
foreground, so take the **first** player-camera projection of each pass -- then
count and log how many were seen and which was taken, so the rule is falsifiable
rather than assumed.

The named signature if we get it wrong: the in-game FOV slider moves the world
and the zoom does not respond.

## The answer to our current blocker

A4 leaves both eyes in one backbuffer, and the open question was how to get a
separable pair. The rule is:

> **Copy each eye's pixels at its own present.** The game reuses that backbuffer
> for the next pass, and in stereo that next pass is the other eye. The fence
> wait is load-bearing, not incidental.

So the second pass does not need its own render target to be usable -- it needs
its copy taken before the next pass overwrites it. That is a much smaller problem
than retargeting the renderer, and it fits the capture path we already have.

## Rules to build in now rather than discover

- **Never submit a layer carrying a pose the runtime did not vouch for.** Check
  `POSITION_VALID_BIT` and `ORIENTATION_VALID_BIT`; on failure submit **zero
  layers**. A dropped frame is recoverable; a frame pinned to a bogus pose is
  what makes people ill.
- **Never publish half a pair.** One eye in a projection layer is that image
  stretched across both, which in a headset is indistinguishable from an IPD
  fault. Relevant immediately: A4 currently presents the second eye over the
  first, so it must not submit anything yet.
- **A run configured for stereo must not publish mono frames**, even before the
  second eye is armed. Withhold and say so once.
- **One `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame` per GAME frame**, even though
  the engine will present twice. Open on the first eye, close on the second.
- **Layer budget is 16.** Exceeding it returns `XR_ERROR_LAYER_LIMIT_EXCEEDED`
  every frame and freezes the display while the flat game keeps running -- not a
  crash, so easy to misread.

## World scale is measured, never guessed

The eye offset is `1/2 x IPD x unitsPerMetre` along the camera's **right**
vector, as a **translation** -- never a yaw. Yawing converges the frusta and adds
vertical disparity that grows toward the edges, read by a person as eye strain
rather than as an obvious fault.

`BuildSyntheticEye` already does a pure translation along the camera's right
axis, so that is correct. **But it assumes `unitsPerMetre = 1`**: A2b passed
0.064 straight through as metres. CryEngine is nominally metres-based so this is
plausible, and A2b's disparities came out consistent with the scene, but it has
never been measured for Prey. Per the rule it should return **unavailable**
rather than silently assuming 1.0 -- a wrong scale reads as hyperstereo or
dollhouse, and both are easily mistaken for a frustum error.

Make scale and IPD **live-tunable through the console seam we already have**.
These are the two numbers a desk cannot settle, and one rebuild per candidate
value is an afternoon of someone's headset time.

## Order of attack when stereo looks wrong

1. **Frustum** -- near object vs horizon. Equal separation means frustum; near
   separating more means parallax and the frustum is fine. Fix this before
   touching anything else.
2. **Eye order / sign** -- and only by negating the **offset**, never the label.
   A flag that flips the label and then derives the offset from the flipped label
   cancels exactly and ships identical geometry both ways.
3. **World scale.**
4. **Comfort.**

**Never accept a scale, sharpness or comfort report while a frustum error is
outstanding.** One frustum error presents as three unrelated bugs -- "zoomed in",
"near objects separate too much", and "the resolution is terrible" -- and any
world-scale reading taken under one is unusable.

---

## Prior art: witcher3-vr, and what transfers

From `D:\Dev Debug\other VR mods\witcher3-vr`. The closest analogue available: an
**injected** mod for a modern engine it does not own, with a DXGI proxy, an
OpenXR eye-geometry module and its own scheduler. `INFERENCE` for Prey under the
cross-engine rule -- a lead and a vocabulary, not a result.

**It ships both stereo strategies, which settles a question we had open.** Its
status table lists *Same-tick geometry stereo* as working alongside an
alternate-eye path (`aer_scheduler`, and the `AER + AFW` mode combinations). So
alternate-eye is a **shipping strategy in a mature mod**, not merely the stopgap
this project had it filed as. That materially raises the value of the
alternating-eye mode A2b already proved, if A4 does not pan out.

**Eye identity comes from the present ordinal, not from a tag or a guess:**

> REDengine builds one real frame between consecutive Present calls. The render
> ordinal therefore owns the eye identity; both adjacent ordinals share one
> nonzero stereo pair id. Present count is incremented before the just-rendered
> backbuffer is captured.

Two things follow. It independently corroborates the skill's *copy each eye's
pixels at its own present*. And it corroborates, from a different engine, the
conclusion this project reached the expensive way: per-frame **tagging** of an
eye across a thread boundary does not work, which is why `SetStereoEyeLock`
replaced it after two unusable A2 runs. Their answer is better than ours for
production -- identity derived from an ordinal that is authoritative on the
render thread, plus an explicit pair id, rather than holding one eye still.

Their render-target join carries the same discipline in a stricter form: *"this
latch never invents an identity from timing"* -- identity is recovered from the
camera that created the pass, and timing is never allowed to stand in for it.
The surrounding machinery is D3D12/RTX command-list plumbing and does **not**
transfer to Prey's D3D11; the principle does.

**They work in tangent space.** `AsymmetricProjectionDescriptor` carries
`horizontal_tangent_span` and `vertical_tangent_span` alongside the engine's
native fields, which is the skill's rule implemented rather than merely stated.

**Both of our open non-stereo problems are solved there**, which is evidence they
are tractable rather than fundamental:

| our blocker | their status line |
| --- | --- |
| HUD picks up per-eye disparity | "Headset-aware HUD convergence -- derived automatically from OpenXR eye geometry for parallel and canted displays" |
| viewmodel does not follow the per-eye camera (R-069) | "Near-camera view -- Working" |

**One difference in our favour.** REDengine expresses a view as a symmetric
vertical FOV plus a *pixel-space projection-centre offset*, so that mod has to
transport asymmetry through a centre-offset. Prey's `CCamera` carries genuine
asymmetry fields (`m_asymL/R/B/T`), which `ProjectionFromTangents` already
writes. We can express a canted per-eye frustum directly, where they must
encode it.
