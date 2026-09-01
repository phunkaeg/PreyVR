# Stereo production route

The artifact `BN-STE-001` names as its exit proof. **It does not yet close that
bottleneck**: no rung has been selected on evidence. This records the ladder, the
state of each rung, and the test that decides.

Playbook: `D:\Dev Debug\VR Modding\docs` -- `bottleneck-map.md`, then
`bottlenecks.yml` classes `BN-STE-001` (gate STEREO-ARCH) and `BN-SFX-001` (gate
STEREO-SAFETY).

## The ladder, and where the fleet actually landed

`BN-STE-001`'s fast_test is to price and test the highest viable rung in order.

| rung | route | PreyVR | who shipped it in-house |
| --- | --- | --- | --- |
| 1 | native scene re-entry | **blocked: reproduced crash 2/2 (F-014)** | *nobody* |
| 2 | per-draw replay | untried | BioshockVR, FarCry2-vr |
| 3 | alternate-eye / AFR | **proven working (A2b)** | ss2vr-work, SOMAVR |
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
