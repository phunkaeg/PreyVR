# H-021 static integration audit

Scope: source integration, deterministic tests, offline target-byte verification,
and read-only Ghidra analysis. No game launch, injection, process attachment,
XR session, graphics capture, or headset test. The runtime agent retains ownership.
Starting revision: `1605539`; changes are uncommitted. Existing AGENTS/CLAUDE edits
belong to the preceding guidance task.

## Fixed integration defects

| Defect | Change | Evidence limit |
| --- | --- | --- |
| Aim writes player `+0x17D4`; IK then treats that controller origin as its native anchor | Publish the clean producer origin before aim edits; both lanes use it. Restore our still-present origin edit before the next native producer call. | Source conflict proved; frequency/order of the old feedback in-game was not measured. |
| Separate head/left/right publications can mix XR times; old seqlocks copy ordinary C++ data concurrently | One `TrackingFrame` contains head, both grip/aim poses, predicted display time, sequence, epoch and timestamp. Mutex-protected payload; engine readers use try-lock. Existing head publication also uses the safe slot. | Coherent XR sample does not establish exact engine-animation-frame correspondence. |
| Stale tracking can survive focus loss | Clear on sync failure, not-focused result and destruction; increment epoch. Refuse old/future samples and an old recenter generation. | 200 ms remains the explicit maximum sample age, not a latency acceptance target. |
| Right calibration clears the request before left runs; left-only never clears it | Per-hand pending mask; commit only after a successful target/rotation/weight write. | Calibration preserves the animated wrist orientation at capture; it does not discover a barrel axis. |
| Shared skeleton asset mistaken for current rig ownership | Successful local-player AttachToHand captures weapon/item, attachment, binding, owning character and a new generation. Revalidate selected ID and pointer chain on every IK candidate. | Ordinary direct-selected-ID weapons supported. Alias/paired equipment is conservatively refused. |
| Old offsets survive re-equip/recenter; animation jobs can race calibration/cache state | Serialize IK state with a try-lock; owner generation, recenter and XR epoch invalidate completed and pending calibration. | Engine object lifetime still depends on the native job/attachment lifecycle; guarded reads are not object pinning. |
| Two hand lanes can be armed together | Reject hand.mode 2 versus ik.mode 2 in both setters. IK also refuses legacy weapon rotation/offset writes. | Modes are commanded through the existing channel; counters distinguish refusal from accepted writes. |
| Invalid indices, location or partial writes can appear successful | Bound target/weight/limb/pose indices; validate chain counts and finite normalized locations; require native ADIK gates and usable reach. On a write fault, attempt rollback and disarm. Count only completed writes. | A destroyed allocation may prevent rollback. Disarming does not claim a native solve succeeded. |

`ik.mode 1` now installs clean-origin and attachment observation. Re-equip after
installing it. Fixed-offset tests and observation do not require controller tracking;
controller drive requires a valid head and selected hand from the same XR publication.
The hook forwards to native ADIK on all normal refusals. Zero mode bypasses the audit
work immediately.

New report fields: `ikOwner`, `ikEquipGen`, `ikPoseSeq`, `ikNoOwner`, `ikBusy`.
Identity and sequence are the last accepted values; `ikNoOwner` counts missing
frame/ownership prerequisites, and `ikBusy` counts a contended IK state lock.
Use changes in counters, not their historical nonzero values, to assess a test.

## Native contracts checked

Target is Steam x64 `PreyDll.dll`, preferred base `0x180000000`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Ghidra program: `/Prey/PreyDll.dll`. No debugger APIs were used.

* `AttachToHand` RVA `0x16914F0` takes whole-weapon this and returns a bool in AL.
  The hook now uses that ABI and only captures a successful attach.
* Base weapon's secondary vtable `0x1E6C888 + 0x1D8` resolves to `0x10DFC10`:
  `8B 41 58 C3` (`mov eax,[rcx+58h]; ret`). `OnEquip` at `0x1699816`
  loads the vtable from weapon+8 and passes weapon+8 as RCX. Thus owner ID is
  whole-weapon `+0x60`. Runtime reads require that concrete getter target and
  owner `0x7777`, preventing NPC attaches from replacing the local capture.
* `OnEquip` uses whole-weapon `+0x38` as item ID and player `+0x14B8` as equipment.
  `IsEquipped` `0x1275500`, compare at `0x1275543`, reads equipment `+0x58`.
  Direct selected ID is therefore player `+0x1510`. Its other alias paths are
  not implemented by this conservative gate.
* Bone attachment vtable is `0x1D212B8`; binding `+0x20`, manager `+0x28`,
  manager's character `+0x18`. `ProjectAttachment` `0x7A2560` independently
  reads that owner chain. Relative default is **`+0xF8`**, absolute default
  `+0x114`, current model pose `+0x130`, extra quaternion `+0x14C`.
* Producer `0x1585320` conditionally writes origin only after successful
  unprojection. A failed inverse/homogeneous divide can retain the old origin.
  Restoring our own still-present edit before the producer closes that feedback
  case; another writer's value is preserved. The native origin is an unprojected
  reticle reference point, **not a separately proved anatomical/cyclops eye**.
* `GetFiringPosition` `0x1694BC0`: RCX weapon, RDX caller-owned result, R8D flags,
  R9 optional override entity; RAX returns the result pointer. Final stores at
  `0x1694FE6` place the fallback byte at result+0 and XYZ at +4/+8/+C.

## Muzzle versus controller: corrected contract and passive evidence

`aim.origin 1` is a **controller-aim-point diagnostic**. It is not a muzzle mode.
XR aim, XR grip, wrist, attachment mount and authored ammo helper are distinct
transforms. The old claim that moving the cached ray to the hand makes shots leave
the barrel along controller forward was too strong. Neither the attachment's
relative default nor wrist-preserving calibration supplies a proved grip-to-barrel
rotation. The original H-021 section 5 and the test script are corrected.

The native firing-position function queries the helper named at weapon+`0x2F0`
on weapon entity+`0x40`, slot 0, through helper-world-TM function `0x11A5CF0`.
It also performs obstruction tests and can choose a camera/adjusted-camera origin.
GLOO's `0x169F420` calls this before `GetReticleInfoForFiring` `0x1694890`, then
normalizes **reticle hit minus native firing origin**. Merely moving the reticle
origin to a different controller point leaves convergence/parallax.

Attachment observation now also installs a **passive** signature-gated hook on
the existing firing-position call. It forwards all arguments and the return
unchanged, makes no extra native queries, and copies the 16-byte result. Only
normal (no override entity), current local-weapon, usable-pose samples are accepted.

`report` exposes:

* `muzzleHooked`, `muzzleSamples`, `muzzleSampleOwnerCurrent`;
* `muzzleAgeMs`, `muzzleEquipGen`, `muzzlePoseSeq`, `muzzlePoseAgeMs`;
* `muzzleFallback`, `muzzleWorld`, `muzzleAimGapMm`, `muzzleGripGapMm`.

These are historical samples with explicit age/ownership. A zero sample count is
not evidence of zero separation. When fallback is set, the position is the native
fallback origin, not necessarily the authored muzzle. Gap magnitudes include
timing, IK reach limits and animation effects; do not turn a measured gap straight
into a grip calibration. This observer establishes no barrel direction or impact
acceptance. **Actual per-weapon barrel alignment remains a runtime/asset gate.**

## Bounded runtime protocol — for the runtime owner, not executed here

Hypothesis: one clean reference and one pose sample remove integration feedback;
current weapon ownership and independent calibration keep the rig stable through
re-equip. Control: native animation with IK disabled. Variable: one write lane,
then one hand/pose/origin setting at a time.

1. Record the built DLL hash, run ID and baseline report. Use the separate audit
   build only when the runtime owner elects to deploy it. Arm `ik.mode 1`, re-equip,
   then inspect `ikOwner`, `ikEquipGen`, rig indices, location, gates and native
   attachment joint. A zero match first requires ownership/capture coverage,
   not a guessed new joint-count signature.
2. Keep legacy hand and weapon writes off. Run the fixed +Z goal first. Both hand
   and weapon must move together; record rising written counters and native limb
   state. If only the hand moves, stop controller tuning and inspect attachment
   binding/update order. A counter alone is not acceptance.
3. Right-only, left-only, then both: arm drive, wait for usable tracking, calibrate.
   Each selected hand must complete once; an untracked hand stays pending. Restore
   tracking and confirm that hand completes without recalibrating the other.
4. Toggle `aim.origin 0/1` with fixed controller poses. IK goal must not gain a
   second hand offset. Compare `ikPoseSeq` and logs. Rotate the body and recenter.
   Recenter/focus reset requires a new calibration. The consumed pose sequence may
   be the previous animation frame: measure callback ordering before claiming
   exact engine-frame synchronization or acceptable latency.
5. Swap weapons, holster, reload and re-equip. Confirm selection gaps stop writes;
   a successful reattach changes generation, even with reused pointers. No stale
   rotation calibration may carry over. Recalibrate the newly equipped rig.
6. In a stationary, unobstructed GLOO test, fire and collect a fresh muzzle sample.
   Positive control: samples increase, owner generation matches, fallback=0.
   Compare muzzle/aim/grip gaps with the rendered weapon and real impacts at near
   and far targets. Then approach a wall and verify native fallback behavior.
   Repeat with a second weapon before generalizing; a muzzle point provides no
   forward-axis proof. Reticle projection, interaction selection and firing have
   separate acceptance checks.
7. Hold a controller still while moving the head. Translation cancellation assumes
   that the native world anchor follows the same head translation. The existing
   orientation-only head-view lane does not prove that assumption; distinguish
   reference/6DoF coverage from the corrected shared-origin feedback.

Revert: `ik.mode 0`, `aim.enable 0`, `aim.origin 0`. Native-origin restoration is
performed on the next producer callback if our previous edit is still present.
No global runtime/layer changes are needed for this protocol. xr-tape poses alone
cannot prove hand rendering, attachment ownership, muzzle orientation or impacts.

## Verification

See `build/h021-integration-audit/` for the isolated Release build and CTest receipt.
The expanded `anim_ik` target covers concurrent and controlled-contention
publication, timestamp bounds/clear, native-origin restoration and feedback,
distinct grip/aim/muzzle fixtures, rotated muzzle offsets, owner/binding/selection
refusals, per-hand calibration and owner/recenter/focus invalidation, location/index
guards, and the native firing-result layout with poisoned padding.

`tools/re/verify_h021_integration.py` checks the new native ownership/observer byte
contracts against the complete supported-image hash. Existing H-021 verification
remains separate. None of these tests executes Prey code or demonstrates headset
acceptance. Exact final test counts are recorded in the research log.
