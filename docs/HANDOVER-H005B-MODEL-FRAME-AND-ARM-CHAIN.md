# Handover — the model frame, and Prey's own two-bone solver

**Static answer, 2026-09-05:** [full investigation and implementation contract](RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md).
The full render matrix is object `+0`; the near character uses camera-position-relative,
world-oriented coordinates. Native two-bone solver `0x871CA0` survives, without a
pole input and with writes to both pose arrays. Attachment `+0x48` is confirmed,
with bind/current-pose compensation required. Two timing hazards are now explicit:
`0x81D377` follows job dispatch, and the post-physics modifier loop repeats entry
zero. The verifier passes 28 static landmarks and 8 synthetic fixtures; no live
process access or runtime implementation was performed in this investigation.

**Written 2026-09-05, after independent per-hand control was reproduced in a
headset.** Two static questions now gate precision. Both suit static analysis; the
live lane has taken them as far as approximation allows.

Companion documents: [`HANDOVER-H005-HAND-RIG-TAKEOVER.md`](HANDOVER-H005-HAND-RIG-TAKEOVER.md)
carries the **rake list** — read it, every entry was actually stepped on — and
[`RE-H005-SKINNING-CONSUMER-2026-09-05.md`](RE-H005-SKINNING-CONSUMER-2026-09-05.md)
is the investigation that unblocked this one.

## Where the lane actually is

Working and confirmed in a headset:

* **`CCharInstance::SkinningTransformationsComputation`** (`0x82EE10`) is hooked;
  its pose input is substituted with a private copy. Nothing recomputes after it.
* **Joints 45 `r_hand_jnt` and 72 `l_hand_jnt`** (R-084), each with a disjoint
  21-joint subtree, resolved **by name** on the 101-joint `root_jnt` rig.
* **The instance is selected by `FOB_NEAREST`** (R-085): two instances share that
  rig, one drawn near (the hands) and one in the world (the shadow), 949 draws
  each. Editing the near one leaves the shadow still, which is the control.
* **Both hands follow motion controllers** (R-087), calibrated, depth correct.
* **The weapon is a separate lane**, driven through `IAttachment` and
  `SetAttAbsoluteDefault` — built, not yet tested.

## Question 1 — the character's true model frame

**This is the important one.** Absolute joint poses are in **model space**;
controller poses arrive in OpenXR/world space. The conversion between them is
currently **approximated** by a yaw-only body frame:

```
bodyYaw    = cameraYaw - (headYaw - recenterYaw)     // pitch and roll dropped
modelDelta = project(worldDelta onto that yaw basis)
```

That is good enough for *"does the hand follow the controller"* and it survived a
headset. It is **not** good enough to place a hand absolutely, to solve an arm
chain against real proportions, or to put a weapon where a controller is.

**The exact transform exists and this hook does not see it.** `RenderCHR`
(`0x81D0D0`) receives the character's render `Matrix34` in **R8** and copies it
into the `CRenderObject`. The skinning conversion runs per character and has no
argument carrying it.

What would close this:

1. **Where the render matrix is stored on the render object**, so it can be read
   at the same place `FOB_NEAREST` is read (`0x81D377`, `RBX` = object) rather
   than needing a second hook.
2. **Whether a first-person, near-flagged character's matrix is camera-relative.**
   The near pass draws through a translation-free view (`0xFB0B70` zeroes the
   row), so the model frame for *this* character may already be camera-relative —
   which would change the conversion entirely rather than merely refine it.
3. **Whether the two instances of the 101-joint rig share a frame.** If the
   near-drawn hands and the world-drawn body use different matrices, one
   conversion cannot serve both.

**Why it matters more than it looks:** every downstream refinement — wrist
rotation, arm-length calibration, elbow solve, weapon-in-hand placement — is
computed *in* this frame. Getting it right once removes an error term from all of
them; leaving it approximate means tuning each against the same unknown.

## Question 2 — is Prey's own two-bone solver reachable?

The fleet playbook is emphatic that this is worth checking before writing an IK
solver: *"You may not have to solve the elbow at all — find the game's own
two-bone solver."* The elbow is a singularity and the whole difficulty of a
two-bone arm.

Prey has the vocabulary: `AnimationPoseModifier_Ik2Segments` (the two-bone
descendant), `AnimationPoseModifier_LimbIk`, `CreateIKLimb`, `IKLIMB_LEFTHAND` /
`IKLIMB_RIGHTHAND`, `LimbIK_Definition`, and a serialised `CPoseModifierSetup`.

**But be warned by what already failed here.** Recorded as H-012: the *cvar*
control surface for this subsystem is **inert**. `ca_useADIKTargets 0`,
`ca_NoAnim 1` and `ca_DebugADIKTargets 1` all reach the engine and do nothing, as
do `i_offset_*` and `g_detachCamera`. Prey's release build keeps GameSDK and
animation-debug registrations whose implementations were stripped. Renderer `r_*`
cvars work normally, so do not generalise from those.

Also failed: the `*_IKTarget` transforms are a **per-cycle scratch buffer**
(R-083). Writing them reliably moved nothing.

So the question is narrower than "does the facility exist":

1. **Is `CreateIKLimb` reachable, and does its result get evaluated** — or is it
   another stripped registration?
2. **Does a `CPoseModifierSetup` stack actually run for the first-person
   character**, and can a modifier be appended or its parameters written?
3. **If a two-bone solve does run, does it take a pole/hint vector?** The playbook
   notes a projected fixed direction has *two* singularities where a cross product
   has one — worth knowing before adopting whatever is there.

A negative is worth as much as a positive: it means writing the solve ourselves,
and chapter 02 carries the elbow treatment to copy.

## Question 3 — does the weapon mount survive?

Smaller, and testable live, but a static answer would save a session.

CryEngine's own header says `SetAttRelativeDefault` is *"overridden by the
animation update"* and prescribes `SetAttAbsoluteDefault` as the workaround. That
is written for the CryEngine 5 tree. **Does the same hold in Prey's fork, and does
anything else write the attachment's absolute default per frame?** If so this lane
has the producer problem the IK targets had, and the seam must move.

The vtable is aligned: R-024 recorded slot `+0xD8` as the binding install, the
interface puts `AddBinding` at index 27, and `27 * 8 == 0xD8`.
`SetAttAbsoluteDefault` is index 9 (`+0x48`) on that alignment.

## What not to redo

* **Do not chase the producer side of the pose.** Three levels were walked and
  every one recomputes each frame; six live bump attempts confirmed a write
  upstream never survives. The consumer is the seam.
* **Do not try the animation cvars.** Four tried, four inert (H-012).
* **Do not move the near-render camera to move the hands.** It transforms
  everything drawn close — both arms and the body — and was rejected on product
  grounds, not technical ones.
* **Do not trust a joint index across skeletons.** Five are skinned at once; the
  188-joint rig puts `r_hand_jnt` at 48, and a 103-joint Breather face rig puts a
  face joint at 45. Resolve by name, per instance.

## Method notes that earned their place

* **A cvar existing proves nothing about whether anything consumes it.**
* **`lastResult=0` from the console bridge means the string was submitted**, not
  that the engine implemented anything.
* **Confirm the animator is running before believing a zero** — a menu, a pause or
  the game sitting in the background all freeze it and produce a confident wrong
  answer.
* **A counter sampled before its input exists** looks exactly like a broken
  mechanism. Both directions of this cost time in a single session.
