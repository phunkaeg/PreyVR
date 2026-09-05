# Handover — H-005: independent control of the first-person hand rig

**Written 2026-09-05.** Self-contained. The live lane is stuck; the remaining
question suits static analysis, which is why this is being handed over.

## The product requirement, stated precisely

Motion controllers drive **each hand independently**. The end state has no mouse
and no engine idle animation — controllers own the hands, and Prey's own weapon
animation is dead weight rather than something to preserve.

**Explicitly rejected:** moving the near-render camera. It transforms *everything
drawn close* — both arms and the body — so the whole torso swings with the gun.
That is a real technique other VR mods ship and it is **not** the product wanted
here. Per-hand articulation is the requirement.

## What is established, with addresses

`PreyDll.dll`, Steam, SHA-256
`7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`, base
`0x180000000`.

* **R-077** — a `QuatT` + blend is logged at `0x878744` (containing function
  `0x877B50`), guarded by a debug switch at `0x2257810`. Layout: `rot` at `+0x00`
  (unit quaternion, verified), `pos` at `+0x10`, stride `0x1C`.
* **R-078** — four live IK targets per capture, two skeletons, two limbs each.
  `rsi` carries the limb id: `0x4EC` is the trigger hand, `0x7E0` the support
  hand, matching rig names `Bip01 RHand2RiflePos_IKTarget` and
  `Bip01 LHand2Weapon_IKTarget`.
* **Limb identification independently confirmed 2026-09-05** by a weapon swap:
  target addresses persisted while values moved sharply, and the support-hand
  target swung down and out to the side (`[33,247,1501]` -> `[-325,-2,952]` mm)
  when the wrench freed that hand. The tester notes the wrench idle animates the
  left hand, so that was a live sample, not a parked one.
* **R-083** — **the targets are a per-cycle scratch destination, not the consumed
  value.** An out-of-module `memcpy` refreshes the destination at the start of
  every cycle from one of two sources; `rdx` at those traps names the source. One
  source is live and itself recomputed each frame; the other is static and is not
  consumed.

## What is ruled out — do not re-run these

1. **Racing the animator.** Six live bump attempts, two skeletons, write rates to
   643/s, amplitudes to 2.5 m square wave. Produces sub-frame glimpses at best.
2. **Writing after the last writer in the cycle.** Built and proven to *apply* —
   60 applies against 540 skips, animator confirmed live — and the value never
   moved (`+1500 mm` requested, memory stayed at `~1410 mm`) and nothing changed on
   screen. This is what showed the targets are scratch.
3. **The cvar wrench.** `ca_useADIKTargets 0`, `ca_NoAnim 1` and
   `ca_DebugADIKTargets 1` all reach the engine and **do nothing** — see H-012.
   Prey's release build keeps GameSDK/animation-debug cvar registrations whose
   implementations were stripped. Renderer `r_*` cvars *do* work; do not
   generalise from those.
4. **The near-render camera** — rejected on product grounds above, not technical.

## Two mistakes to inherit as cautions, not as facts

* **R-082 was measured at the wrong address.** `ArmIkProducerWatch` adds the `0x10`
  position offset itself; passing `base + 0x10` watches `base + 0x20`, which at a
  `0x1C` stride is the *next entry's rotation*. It replicated cleanly across two
  weapons, two limbs and two skeletons and was still measuring the wrong field.
  The correction also found **two alternating write paths**, whose steady-state one
  ends at `0x87BBA0`, not the `0x87BC36` originally recorded.
* **The skeleton mapping is suggestive, not established.** The wearer's reading is
  that one skeleton drives the visible headless first-person body and the other a
  full-body shadow proxy (the body mesh has no head; its shadow does). Supporting
  observations are all **single-frame glimpses**, which is this project's weakest
  evidence class. Do not build on it.

## The question worth answering

**What reads the finished pose to produce pixels?** The producer side is a
pipeline where every level is recomputed each frame — three levels were walked and
each had a producer above it. The consumer side has no such regress: there is one
consumer per frame and nothing overwrites after it.

Concretely: the skinning / joint matrices the renderer consumes for the
first-person character. Finding those would also settle the skeleton mapping for
free, since the two meshes must read different matrices.

A useful adjacent anchor: `CArkWeapon::AttachToHand` consumes the resolved
`IAttachment*` at weapon `+0x2B0`. And the tester's observation that **the gun's
shadow did not move when the hand shadows did** suggests the weapon is not
parented to the hands — consistent with the rig names, where the hand is driven
*to* the rifle. If that holds, weapon and hands may need separate treatment.

## Tools that exist and work

* `src/dll/DebugWatch.*` — hardware execute/data breakpoints, full register
  capture, and an **apply mode** that edits memory at a chosen instruction. Proven
  to land writes; see `RE-009` in the fleet playbook for the silent failure modes
  (`LEN` encoding, alignment, `CONTEXT_DEBUG_REGISTERS`).
* `tools/live/preyvr-harness.js` — `runIkCapture` harvests live targets through
  R-077's guarded log site and restores the guard.
* Console bridge with a fail-closed allowlist; renderer cvars work.

## The rakes — every one of these was stepped on

Grouped by what they look like when you hit them, because none of them announces
itself. **Every one produced a confident, plausible, wrong result.**

### Instrument returns a number that means nothing

| rake | how it presents | the check |
|---|---|---|
| A misaligned data breakpoint **never fires** | "nothing writes this" | refuse to arm unaligned; `RE-009`/`FAIL-RE-024` |
| `DR6` is sticky and handler edits are dropped without `CONTEXT_DEBUG_REGISTERS` | later traps attributed to slots that never fired | set the flag explicitly; `FAIL-RE-025` |
| A write watch reports `RIP` **after** the store retires | the "writing instruction" is the one *following* | scan back; publish which convention you report |
| `ArmIkProducerWatch` adds `0x10` itself | watching `base+0x20`, the **next entry's rotation** | pass the `QuatT` base, never a pre-offset address |
| The capture ring **saturates at `maxCaptures`** by design | a 14 s window returns byte-identical results to a 6 s one | check `captures` against the cap before reading absence as evidence |
| A watch **self-disarms** when its quota fills | an override works for 16 traps then stops, counters still green | exempt applying slots; already fixed in `DebugWatch` |
| `stackTop` is `[rsp]`, a hint and not a caller | one trap reported a "caller" of `0x3CE0903A62641692`, which is data | range-check against the module before calling it a caller |

### Measurement taken under conditions that cannot show the thing

| rake | how it presents | the check |
|---|---|---|
| **Animator frozen** — menu, pause, or game in the background | zero traps, frozen values, indistinguishable from a real negative | sample trap activity for a second and **refuse**; hit three times in one session |
| **Stationary player** | IK targets look static; R-078 originally concluded exactly this and was wrong | move, then sample |
| **Console `lastResult=0`** | means *our queue submitted the string*, not that the engine implemented it | name an observable before the run; see H-012 |
| **Unfair comparison between arms of a test** | one skeleton tested at 0.7 m sine, the other at 2.5 m square; "nothing on the first" was meaningless | identical stimulus, or it is not a comparison |
| **Single-frame glimpses by someone who knows the expected answer** | two "flickers" in twelve seconds, reported after a stated prediction | weakest class here; blinding is impossible because the tester sees the tool calls |

### Environment and tooling

| rake | how it presents | the check |
|---|---|---|
| **MinHook is initialised only inside the frame observer's enable path** | every other hook silently fails to install | `PreyVR_SetFrameObserverEnabled(1)` first, always |
| **The module pins itself** and refuses to hook if pinning fails | `FreeLibrary` cannot unload it; new code needs a **new process** | inject from a staged copy so the build output stays unlocked |
| **F-009**: a Frida `NativeFunction` abort can leave a control mutex held | every later control call returns `detail=busy`, unfixable without restart | use the `*Ptr` exports on a real thread |
| **`frida-agent.dll` crashed the host three times** with three different symptoms | wedged session, null deref, wild-pointer read | check the dump's **module list**; do not assume it was the mod |
| **Heap addresses are session-specific** | a cached target from a previous run is a wild write | re-capture every run; validate the quaternion is unit before writing |

### Reasoning

| rake | how it presents |
|---|---|
| **The producer side is a regress** — every level is recomputed each frame; three were walked before that was noticed |
| **A cvar existing proves nothing** — four registered, all inert (H-012) |
| **The answer may already be in the registry** — `gEnv->pInput` and the `renderer+0x95B4` latch were both already recorded when work was sent looking for them |

## Cautions from this project

* **Confirm the animator is running before believing any zero.** A menu, a pause or
  the game in the background freezes it and yields a confident zero
  indistinguishable from a real negative. This was hit three times in one session.
* **A counter reporting success may only mean the string was submitted** — see
  H-012. Name the observable before the run.
* `frida-agent.dll` crashed the host **three times** in one session with three
  different symptoms. If driving this live at volume, prefer a channel that does
  not need the injector.
