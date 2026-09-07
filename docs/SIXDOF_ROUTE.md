# The 6DoF route — rescoped 2026-09-03

**Supersedes the staging in `HEAD_TRACKING_SCOPE.md`.** That document was written
against the wrong destination: it treated mouse and keyboard as fixtures to design
around, and staged the work as "add head tracking to a flat game". The stated end
state is the opposite — the mouse is scaffolding to be removed.

> The intended end state is native, engine-owned 6DoF world rendering and motion
> controls; a flat OpenXR bridge is useful only as a reversible early milestone.
> — `README.md`, first paragraph

Stated concretely by the project owner:

- the **HMD** controls all view manipulation, in 6DoF
- a **motion-controller stick** moves the player capsule through the level
- a **motion controller** owns aim, in 6DoF

Nothing in that list involves a mouse. Everything below is staged toward it.

## What the destination changes about decisions already made

**Aim following the camera is not a hazard to avoid, it is the defect to remove.**
Several earlier notes treated the cached aim ray tracking the camera as a risk —
something to measure and stay clear of. In this architecture aim belongs to the
controller, so the camera dragging it is exactly what has to be fixed. That
reverses the priority: it moves from "watch out for" to "build early".

**Mouse pitch should not reach anything.** The composition question that was put to
the owner -- whether mouse pitch should affect the view -- was the wrong question.
It should not exist. `ComposeHeadOntoGameRotation` was built to preserve it and is
therefore the wrong shape; it stays tested and unused rather than deleted, in case
some external pitch source is ever wanted.

**Yaw from the game's camera is a shim with a known removal date.** The view seam
currently reads the game camera's yaw so mouse turning still works while building.
That is scaffolding, and M5 removes it.

## Status, 2026-09-07

M1 and M3 are done and confirmed in a headset. M2 has its direction lane working
and its origin/barrel lane open. **The hands were not a stage in this list and
turned out to be the hardest part of it** -- they are done as of R-104: the
controller drives the wrist through Prey's own animation-driven IK, the engine
solves the arm, and the weapon follows because the write lands before bone
attachments sample the pose.

## Stages

Ordered so each one is testable alone, and so the pieces that *unblock judging the
others* come first.

### M1 — the view seam. **DONE — head rotation and 6DoF confirmed in a headset.**

`ArkPlayerCamera::UpdateView` (R-009) is the seam, because it is where the game
computes the camera and therefore upstream of every consumer at once. Three
earlier seams each reached a different subset: `CRenderView::SetCamera` missed
culling entirely; `CSystem::Render` matched the pass camera to zero millidegrees
while culling still followed aim, because the occlusion job is spawned earlier
from `GetViewCamera()`.

Orientation written is play-space yaw composed with the head. `SViewParams` layout
confirmed live as R-075.

**Exit:** view and culling both follow the headset.

### M2 — detach aim onto the controller. **Direction done; origin/barrel alignment open.**

Was going to be late work. It belongs here because until aim is detached, every
camera change drags the weapon with it -- which corrupts the thing being judged and
makes the view feel wrong for a reason that has nothing to do with the view.

The lane is already proven. H-004 is reproduced: a fixed-camera reticle probe
changed the cached ray, and the A0b wrench test moved a native wall contact
`0.127165` units laterally on the same collider. `ArkPlayer+0x17D4`/`+0x17E0` is
the seam, written after R-011 rebuilds it during `OnPreRender`.

**Exit:** the weapon points where the controller points, and does not move when
the head does.

### M3 — positional head tracking. **DONE — `view.position`, confirmed in a headset.**

The translation half of 6DoF, which M1 deliberately left out.

- **`unitsPerMetre` must be measured.** Still assumed to be 1 and never verified.
  It scales position exactly and rotation not at all, which is why no test so far
  could have caught an error in it.
- **The head layer of collision** -- the playbook's "second layer people forget":
  native movement gives capsule collision for the body and does nothing to stop
  the head leaning through a wall.

**Exit:** leaning produces correct parallax and degrades gracefully into geometry.

### M4 — locomotion on the stick. **Written and unit-tested; unwired.**

The controller stick drives the capsule. `MotionController.cpp` already exists and
already takes a `ReferenceFrame`, so this is wiring rather than new design.

**Exit:** the level is traversable without a keyboard.

### M5 — own the turn, and drop the mouse. **Open.**

Play-space yaw becomes a value this project owns, driven by stick turn, replacing
the game camera's yaw that M1 borrows. That removes the last mouse dependency and
completes the stated end state.

**Exit:** the game is playable entirely from HMD and controllers.

## Correction, same day: far more is already built than this document first said

Queried the project's own knowledge graph -- `graphify/graphify-out/graph.json`,
current as of 2026-09-03 -- at the owner's suggestion, after repeated greps had
missed things sitting in plain sight. It surfaced in one query what several
searches had not.

**The pure layer for three of the five stages already exists, with tests.**
`MotionController.h` carries:

| symbol | stage | note |
| --- | --- | --- |
| `ControllerPoseInWorld` | M2 | uses the same `ReferenceFrame` as the eyes, so hand and view cannot drift |
| `AimFromController` | M2 | tracking-gated, and documented to leave Prey's own reticle ray alone when unusable |
| `WeaponPoseFromController`, `GripTransform` | H-005 | weapon placement relative to the hand |
| `TwoHandedWeaponPose` | H-005 | forward runs rear hand to front hand, refuses below a minimum separation |
| `SnapTurn`, `SmoothTurn`, `WrapAngle` | M5 | `SnapTurn` returns **the new reference yaw** -- exactly the owned-turn value M5 needs |

So the remaining work in M2, M4 and M5 is **wiring, not design**. The maths is
written, tested, and already agrees with the reference frame the view seam uses.

**The one genuine unknown is narrower than "weapon models".** `GripTransform`'s own
comment states it plainly: *"Prey's values are unknown -- the transform writer has
not been located yet -- so this type exists to hold them once measured, not to
imply we have them."* That is the whole of H-005's open part: **where the
late-frame weapon transform is written.** Everything either side of it exists.

That also sharpens what the earlier prototype's "impossible" claim is up against.
Aim is reproduced with a measured number. Weapon *placement* has its maths built
and a named native limb-IK facility identified -- `IKLIMB_LEFTHAND` /
`IKLIMB_RIGHTHAND`, a two-bone solver, `CreateIKLimb`. What is missing is one
address.

**Method note.** The graph should be queried before grepping documentation. Three
times in one session the answer was already in this repository and repeated
searches did not surface it; one graph query did.

## What is already built and carries over

| piece | state |
| --- | --- |
| stereo submission, per-eye images | working, tuned in headset |
| eye handoff, ~45 Hz per eye | verified, lag 0-1 over 30k frames |
| sRGB swapchain (FAIL-STR-033) | fixed, confirmed by eye |
| declared frustum, 120 x 88.507 | triple-confirmed against the FOV slider |
| pose path: locate, seqlock, recenter | working, ~4 ms handoff |
| recenter yaw, roll/pitch immune | tested, FAIL-CAM-019 pinned |
| `SViewParams` layout | R-075, confirmed live |
| `MotionController.cpp` | present, 149 lines, tested, unwired |
| aim seam `+0x17D4` | H-004 reproduced |

## The instrumentation lesson this rescope should carry

Every counter built so far measures **whether a write happened**, not **whether
the value was right**. Applied counts climbed at frame rate, refusals stayed at
zero, restore failures stayed at zero and the pass-camera probe agreed to zero
millidegrees -- while the rendered camera was discarding mouse-look entirely. A
person looking at the screen caught it in seconds.

For the stages above that means: a counter is necessary and never sufficient, and
any stage whose exit criterion can only be judged by eye should say so rather than
substitute a number that happens to be green.
