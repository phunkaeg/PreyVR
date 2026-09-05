# Handover — native near-selection, the camera origin, and the input event

**Written 2026-09-05**, after R-088 landed. Three questions, each with a specific
consumer already written or blocked. Prior context:
[`RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md`](RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md),
[`HANDOVER-H005-HAND-RIG-TAKEOVER.md`](HANDOVER-H005-HAND-RIG-TAKEOVER.md) (the
**rake list** — every entry was stepped on).

## Question 1 — can the near character be identified natively, at `RenderCHR` entry?

**This removes the last injector dependency from routine testing**, which is worth
more than it sounds: `frida-agent.dll` crashed the host **four times in one
session** with four different symptoms, and a file-driven command channel now
covers everything *except* this one observation.

Today the near instance is identified by attaching a debugger at `0x81D377` and
reading render-object flags for `FOB_NEAREST`. That is mid-function and after the
skinning dispatch, so it is both awkward and late (R-088).

`RenderFrame` now hooks `RenderCHR` (`0x81D0D0`) **at its own entry**, where RCX is
the character and R8 the matrix. What it cannot see is whether *this* draw is the
near one, because `object+0x40` is populated inside.

So: **is near-ness determinable from the arguments available at entry?**

* `SRendParams` is RDX. Does it carry the near/first-person intent, and at what
  offset? R-088 notes `SRendParams+0x48` is the instance token.
* The report mentions the entity slot's **render flags byte `+0xAC`, near bit
  `0x2`**, reachable through that token. Is that route reliable, and does it
  agree with the eventual `FOB_NEAREST` on the object?
* `0x974551` tests a slot-near condition inside `CEntityObject::Render`. Is the
  same predicate cheaply computable at `RenderCHR` entry?

A negative is useful: it means the selection marker has to stay mid-function, and
the honest fix is a hardware execute watch driven from the DLL rather than an
injector.

## Question 2 — which camera sample is `C`, and is it per-eye?

R-088 gives `Mnear = T(-C) * W`, with `C` sampled through `ISystem::GetViewCamera`
at `0x974735`, and warns: *"Do not substitute an arbitrary later per-eye camera or
apply half an IPD twice; the live lane already recorded that failure."*

That failure is `FAIL-HAND-037`, found here yesterday: anchoring a controller
conversion at the live view camera picked up the **half-IPD stereo offset**, baked
an alternating translation into a model-space bone, and produced a constant screen
offset at every depth that could not fuse. A wearer diagnosed it; no counter could.

This mod **writes** `CSystem::m_ViewCamera` per eye (synthetic stereo, native
projection, translation-only). So:

* **Does `0x974735` read the same `m_ViewCamera` this mod is offsetting per eye?**
  If yes, `Mnear` itself alternates by half an IPD every frame, and *any* absolute
  model-space placement inherits the bug rather than merely risking it.
* If so, **is there a cyclops sample available at that point** — an un-offset
  camera, or a stable origin captured before the eye edit — that a consumer could
  match instead?
* Ordering: does the near-branch camera read happen **before or after** our
  per-eye camera write within a frame?

The consumer is written and waiting: `RenderFrame` captures the matrix but the
hand lane still uses a yaw approximation, deliberately, until this is settled.
Switching frames while `C` alternates would trade a known approximation for a
subtler version of a bug we have already paid for once.

## Question 3 — the `SInputEvent` layout and the action vocabulary

The last unbuilt lane in the product is **locomotion**: a thumbstick moving the
player capsule. It is unblocked but unbuilt.

Established (R-080): `gEnv->pInput` is `gEnv+0x58`, and `IInput::PostInputEvent`
is vtable **slot 12, RVA `0x9D6D30`**, confirmed by two independent setter/getter
alignment pairs. R-070 established that Prey did not replace CryEngine's input
layer, so a synthesised event reaches every consumer a real button does.

What is missing is the **shape of the thing to post**:

* **`SInputEvent` field layout in Prey's fork** — device, key id, state, value,
  modifiers. The CE5 struct is an oracle, not the target; this project has been
  bitten by exactly that assumption twice (`rsi` decoding, the FOV struct offset).
* **Which key/action identifiers correspond to movement**, and whether an analogue
  axis is expressible or only digital presses. Named actions including `attack1`,
  `firemode`, `reload` and `Crouch` exist in the binary (R-070).
* **Whether `CActionMapManager` filters by device**, which would decide whether a
  synthetic event must claim to be a gamepad rather than a keyboard.
* Anything that would make an unscoped post affect **NPCs** — the playbook's
  warning about the shot-redirection evaluator applies to input too: *scope the
  hook to the player*, or every enemy fires from your muzzle.

The controller side already exists and is tested: `SnapTurn`, `SmoothTurn` and a
thumbstick value published by `XrInput`. Nothing consumes them.

## Standing constraints

* **Never modify the installed game.** All live work read-only unless a bounded,
  reversible written protocol exists.
* **A cvar existing proves nothing** — four animation cvars reach the engine and
  do nothing (H-012). The renderer `r_*` surface works; do not generalise from it.
* **Do not re-walk the producer side of the pose** (R-083), the animation cvars
  (H-012), or the near-render camera as a hand lever (rejected on product
  grounds).
