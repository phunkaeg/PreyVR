# Stereo production route

The artifact `BN-STE-001` names as its exit proof.

**Rung 1b has been opened by the CryEngine source and is not yet priced.** See
`CRYENGINE_SOURCE_FINDINGS.md`: `CD3DStereoRenderer::RenderScene` traverses the
world **once** and submits the resulting render view **twice**, switching the eye
between. Every rung-1 attempt here doubled the *traversal*, which is what
allocates -- so F-016's leak was the engine indicating the wrong seam rather than
an obstacle to route around. Rung 1b costs no traversal and therefore cannot leak
the same way. It is blocked only on locating `CD3D9Renderer::RT_RenderScene`.

**Rung 3, alternate-eye, remains selected as of 2026-09-01** and is the thing to
build while 1b is priced, since their downstream is identical. Rung 1 was
priced first, as the fast_test requires, across four experiments (A3, A4, A5, A6)
and is parked rather than refuted -- see below and F-013/F-014/F-015.

Playbook: `D:\Dev Debug\VR Modding\docs` -- `bottleneck-map.md`, then
`bottlenecks.yml` classes `BN-STE-001` (gate STEREO-ARCH) and `BN-SFX-001` (gate
STEREO-SAFETY).

## The ladder, and where the fleet actually landed

`BN-STE-001`'s fast_test is to price and test the highest viable rung in order.

| rung | route | PreyVR | who shipped it in-house |
| --- | --- | --- | --- |
| 1a | native scene re-entry, **doubling the traversal** | **refuted: leaks by construction (F-016)** | *nobody* |
| 1b | native stereo, **doubling the submission** | **NEW -- Crytek's own shape, untried** | Crytek ships it |
| 2 | per-draw replay | untried | BioshockVR, FarCry2-vr |
| 3 | alternate-eye / AFR | **SELECTED -- proven working (A2b)** | ss2vr-work, SOMAVR |
| 4 | draw-stream reconstruction | untried | -- |

**Three of the four shipped in-house projects chose a rung below the one we are
testing, and none shipped rung 1.** FarCry2-vr still lists native-stereo
viability as "the higher-rung investigation". That is not proof rung 1 is
unreachable in Prey -- it is evidence about its cost, and it is the fact this
project should have had before building A4.

Rung 3 is not a consolation. It is proven here (A2b passed against predictions
written beforehand) and it is what two shipped projects run.

## A5 result, 2026-09-01: the control failed

One zero-camera-delta pass ran, reported `frameIdMoved=0`, self-disarmed cleanly,
and the **render thread crashed about a second later with nothing armed** --
reading `0xFFFFFFFFFFFFFFFF` inside `FUN_180ED82D0`, which walks the renderer's
per-frame pooled chunk chain indexed at `+0x499C`. See F-014.

**Reproduced 2/2 on 2026-09-01, the second run on a quiet machine with no games
and nothing submitting to the headset.** Identical crash RVA `0xED835D` on
different module bases and a different render slot. The contention hypothesis is
refuted and the cause is ours.

The breadcrumb read `second_pass:done`, so `RenderWorld` returned normally and
our pass completes in full. **The fault is on a later frame, in state the pass
left behind** -- which is a much narrower question than A3's wedge.

*(Superseded paragraph, kept for the record:)* **This run does not move rung 1 either way.** Other games were running under
other agents at the time -- unquantified concurrent GPU and VRAM load -- so a
single crash cannot be attributed to our pass. No display-driver reset, TDR or
other application crash appears in the event log for that window, which removes
the two most plausible external causes as *observed* events without clearing
contention that fails inside Prey silently.

An earlier version of this section asserted rung 1's odds were now worse. That
promoted an ambiguous n=1 outcome to a verdict, which is precisely what the
playbook's agent protocol forbids, and it is withdrawn.

**The next step is repetition, not redesign.** Re-run the same single zero-delta
pass on a quiet machine, several times, with a liveness check that waits and
re-reads rather than sampling immediately. Only then is there a result to reason
from. What survives regardless is the localisation: the faulting function, the
`+0x499C` frame-slot index and the chunk-chain walk are facts from the dump and
do not depend on what caused the dump.

Two method faults this exposed, both recorded in F-014: a liveness check read
immediately after the event it judges is not a liveness check, and a breadcrumb
readable on only one failure path is not a breadcrumb. Both are fixed.

## Rung 1: what is established and what is not

**Established.**

- The seam is `RenderWorld` (R-054), below `CSystem::Render`'s per-frame work and
  above the HUD. Two other injected mods independently reached the same rule --
  see `STEREO_RENDER_ARCHITECTURE.md`.
- The pass is a **value type** built into a caller-supplied 64-byte buffer by
  `CreateGeneralPassRenderingInfo` (R-071, byte-gated as `pass.create_general`),
  so a second pass can own its own.
- The recursive render view is a **distinct object** on both thread slots
  (R-072, confirmed live), so the two passes need not share per-frame view state.

**Not established, and each blocks the rung.**

- *Live viability.* A4 has never run. A3 -- the same idea at the wrong layer --
  wedged the engine (F-013).
- *Side-effect safety.* `BN-SFX-001` is open. No evidence the second pass leaves
  simulation, particles, audio or queries untouched.
- *Frame placement.* A4 appends its pass after the original's HUD and present,
  which is not the correct frame flow. It is an architectural probe, not a
  stereo path, and is marked as such in its own header.
- *Cost.* Unmeasured.

## Camera delivery

Not a matrix patch. The camera enters through `SRenderingPassInfo+0x18`, and
`CreateGeneralPassRenderingInfo` registers it through
`p3DEngine->vtable[0x608]`. The eye camera is built in our own memory from the
game's live camera and the game's camera is **never written** on this path --
which is why A4 needs no restore point, unlike A1/A2b.

## Rollback

`SetSecondPassStereo(0, 0, 0, false)` disarms. Independently, a wall-clock
watchdog on a non-render thread clears the mode on expiry, because F-013 proved a
frame-counted budget cannot bound a failure that stops frames. **The watchdog
bounds exposure, not damage:** a render thread already wedged inside the engine
is not freed by clearing a flag, and this would not have saved the A3 session.

## The exit test — A5, the side-effect gate

`BN-STE-001` requires an exit test that **distinguishes a camera change from
repeated side effects**. `BN-SFX-001` names its shape, and it is now instrumented.

Stated before the run, per the playbook's agent protocol:

- **Hypothesis.** A second `RenderWorld` with its own recursive view does not
  advance once-per-frame side effects.
- **Control.** Zero camera delta -- `zeroCameraDelta = 1`. Identical pass,
  identical recursive view, identical dispatch, zero eye offset, so the second
  image should equal the first.
- **Test variable.** The eye offset, 0 against 64 mm.
- **Decision rule.**
  - control at the noise floor **and** `frameIdMoved == 0` -> no side effect
    detected; rung 1 stays open and frame placement becomes the next problem.
  - control above the noise floor **or** `frameIdMoved > 0` -> side effects
    confirmed; **rung 1 is rejected** and rung 3, already proven, becomes the
    route.
  - anything else -> ambiguous, and recorded as ambiguous. The playbook is
    explicit that ambiguous outcomes must not be promoted.

**Run the control before the eye-delta test.** A "the picture changed" result
from the eye-delta test alone cannot distinguish stereo from side effects, which
is the whole reason the control exists -- the same mistake F-011 recorded when
two effects were measured in one image.

### The counter, and its limits

`PreyVR_GetSecondPassFrameIdMovedCount()` counts second passes that advanced the
renderer's own frame ids, read either side of the call through
`pRenderer->vtable[0x548]` -- the accessor `CreateGeneralPassRenderingInfo` itself
uses.

**A non-zero count is a positive detection and fails the gate. A zero count is
necessary but not sufficient**: it sees renderer bookkeeping only, and says
nothing about particle aging, audio events or AI. `BN-SFX-001`'s full ledger
wants simulation, audio, particle, query and allocation counters; this is the
first of them and the only one currently reachable.

### A scene freeze would hide the thing being measured

`t_Scale 0` is mandatory in A1 and A2b because it removes frame-to-frame noise.
**It must not be used for the eye-delta half of A5.** Accumulating side effects
-- double-aged particles, double-fired audio -- are exactly what a frozen
simulation suppresses, so freezing would produce a clean pass that means nothing.
The counter is readable either way; the image comparison is not.

## Breadcrumb

`gStereoStep` names the sub-step in flight and the watchdog prints it on expiry.
F-013 is the argument: when A3 wedged we knew four frames had completed and
nothing about which step of the fifth killed the engine.


## Rung 3 with native projection -- run 2026-09-02, PASSED

The first test of `SetNativeProjection`: build each eye by translation alone and
leave Prey's own projection untouched. Predictions were written before the run.

| comparison | mean abs | reading |
| --- | --- | --- |
| left vs left, adjacent (**control**) | **0.0123** | below A1's 0.0167 noise floor |
| left vs left, **12,000 frames apart** | **0.0152** | still noise -- frozen and deterministic |
| left vs right (**test**) | **10.287** | parallax |
| native vs synthetic, same eye (**flag control**) | **25.399** | the flag really changes the image |

The test result reproduced against two independent left-eye baselines, agreeing
to four decimal places (10.287439 and 10.286634).

`no_single_shift_explains_it` holds on the parallax test with only a 1.04x
improvement at the best shift. That is the result that matters: a uniform 2D
image shift would be explained by a single offset, and this is not. Near geometry
moved further than far geometry, which is what stereo is.

### The control had to be redesigned mid-run, and the reason is worth keeping

The obvious control -- zero IPD -- **is not a control here.**
`SetSyntheticStereo` treats `ipdMetres == 0` as *disarm*. Two captures at zero IPD
would be identical because the stereo path was never armed, and that would have
read as a clean control while proving nothing.

The control used instead is **the same eye captured twice at full IPD**:
everything identical including the stereo path being live, so the difference is
noise and nothing else. It is the stronger control regardless.

### A check that looked like confirmation and was not

The first attempt to prove the projection was *inherited* read the declared FOV
with each eye locked, expecting both to report Prey's frustum. They did -- and so
did the negative control with the flag **off**, which should have shown the
synthetic 50-degree half-FOV instead.

`ReadDeclaredFovPtr` reads `CSystem::m_ViewCamera`, which the engine rewrites from
its own camera every frame. An asynchronous read therefore always sees the
*unedited* camera and can never observe the hook's edit. **The check cannot
distinguish the two states and confirms nothing.** It was discarded rather than
reported.

What replaced it is the flag control in the table above: same eye, same frozen
scene, only the flag changed, 72% of pixels different. That is a direct
observation of the flag changing the rendered image rather than an inference from
a value that turned out not to be readable.

This is the third time on this project that a self-consistent check has produced
a confident wrong answer -- after the asymmetry test and the JS FOV
re-derivation. **The negative control is what caught it each time**, and running
one is not optional.

### State restored

Stereo disarmed, eye lock returned to alternating, native projection off,
`t_Scale 1`, `r_AntialiasingMode 3`, `r_MotionBlur 2`, observer off. Camera edit
status back to `ready` with **zero restore failures**.

### What this does not establish

Only that the image is correct *relative to itself*. Whether 120 degrees declared
to OpenXR actually reads as correct depth is a headset question and is untested.


## Eye handoff verified offline -- run 2026-09-02, PASSED

The dwell was replaced by carrying the eye with the frame. Verified on a monitor,
before a headset, because the failure mode is swapped eyes and that is miserable
to diagnose from inside one.

| check | result | |
| --- | --- | --- |
| publishes per rendered frame | **1.0019** over 514 frames | no double-publish; ordering assumption holds |
| eye histogram, 400 samples | **193 / 207 / 0 other**, 259 transitions | genuinely alternating, not stuck |
| starved / dropped | **0 / 0** | |
| scene guard, unfrozen | 5.534 | the scene is live |
| control, same eye frozen | **0.0226** | noise floor |
| test, locked left vs right | **4.637** | parallax, `no_single_shift_explains_it` |

Control-to-test separation is **205:1**.

### The decisive result came from an instrument failure

Six consecutive captures all landed on a frame stride of exactly 28. Even stride,
same parity, same eye -- six identical images and no alternation visible. The
capture request-to-completion round trip is quantised, so it **cannot** sample
both eyes at its natural cadence.

Re-running with deliberately staggered delays turned that into the experiment:

| stride | parity | mean abs |
| --- | --- | --- |
| 30 | even | 0.031 |
| 32 | even | 0.038 |
| **33** | **odd** | **4.672** |
| 34 | even | 0.036 |

Frame-stride parity predicts the eye change, four for four. That is exactly what
per-frame alternation implies and nothing else explains it. The 4.672 also agrees
with the independently measured locked left-vs-right value of 4.637 on the same
scene, to within 1%.

**Absolute parallax is not comparable between scenes.** This scene gives 4.6 where
an earlier one gave 10.3; the magnitude depends on how much near geometry is in
frame. The control-to-test ratio is the comparable quantity, not the number.

### Twice frozen on a menu

Two runs were wasted freezing while Prey sat on its menu background, which the
game renders blurred and dimmed by design. That produces near-zero diffs and a
tiny cross-eye difference -- a blurred, depthless backdrop has almost no parallax
to show -- and it reads convincingly as a broken stereo path.

The protocol now opens with a **scene-content guard**: two unfrozen captures a
moment apart, which must differ before anything is frozen. A live scene animates
and a menu backdrop does not, so the guard costs one capture and removes the
whole failure class.

### Not yet established

The **consume** side has not run against a live session. Publishing is verified at
1:1 and alternation is verified, but lag stability with an actual consumer
attached -- the render thread keeping up under submission load -- needs a headset
and is the next thing to watch. `PreyVR_GetEyeHandoffLag` is the instrument.
