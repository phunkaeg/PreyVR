# Build takeover: first correction and next milestone

The first takeover build corrects the near-FOV override. The next feature
milestone is deterministic weapon/aim alignment across equipment changes.
Collision follows that milestone; a world ray does not repair an arbitrary
rotation captured between a controller and the animated wrist.

Scope: source edits, read-only native analysis, compiled offline tests. No game
launch, injection, native query call, runtime configuration change or headset
acceptance was performed. Base revision: `5efd7c8`; exact source snapshots and
output hashes are in `evidence/build-takeover-2026-09-09/build-manifest.json`.

## Correction shipped in the local build

`1f5a028` wrote `CD3D9Renderer+0x95B4` from three `CSystem::Render` paths.
Those call sites do not establish ordering after a render-thread command-buffer
latch. They also omitted other render fallbacks and dereferenced a singleton
and member without a fault guard. A plausible float is not an offset proof.
This audit does **not** attribute a measured headset symptom to that code.

The replacement hooks `RT_BeginFrame` (RVA `0xF7D710`), calls the original,
then applies the requested value through that call's renderer receiver. This
orders the write after the native latch on the same thread. The three game-thread
writes are removed. No mutex is held over an engine call or in the detour.

The hook is installed only on an explicit nonzero request, after module pinning,
with a 23-byte entry gate and an exact 23-byte gate at the latch. The runtime
receiver must have the expected concrete function at vtable `+0x8C8`.
Only the mod's memory access has SEH protection; engine exceptions retain their
normal handling. A refused receiver/value disarms further writes until an
explicit command. The retained trampoline lives until process exit.

Native near consumers use the override only for `1 < FOV < 179` degrees.
The command therefore accepts `11..1789` decidegrees, or zero to disable.
Engine zero/negative fallback values no longer prevent enabling the override.
Disabling lets the next native frame-begin latch restore the current cvar;
it does not write an old saved value into the renderer.

Commands:

```text
near.fov 885     # set 88.5 degrees
near.fov         # read status without changing/rearming the setting
near.fov 0       # disable; next frame begin uses the engine's current cvar
```

`applied` and `refused` are cumulative callback counters. `thread` identifies
the last successful callback thread. `observedDeciDegrees` is the latest native
latch seen before our write, qualified by `observedValid`. `faulted=1` means
further writes are disarmed even while the requested value remains nonzero.
These independently loaded diagnostics are not a coherent per-frame trace.
Equal readback cannot prove no competing writer: startup sets the cvar too,
and another writer can write the same value. Zoom-manager behavior was not
remeasured here. The startup comments no longer present it as a proven diagnosis.

## Native proof

Target: Steam `PreyDll.dll`, x64 Windows ABI, base `0x180000000`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Ghidra 12.1.2 program identity/import hash matches the installed PE file.

At `0xF7D724`, `RBX = RCX` preserves the renderer receiver. At `0xF7DC85`,
the function loads the cvar at `0x2B1C64C`; at `0xF7DC94`, it stores that float
to `[RBX+0x95B4]`. The captured function's returns follow this latch.
Renderer vtable `0x1DD2E08 + 0x8C8` holds `base+0xF7D710`.
The complete 2112-byte frame-begin span matches Ghidra and disk, as do both
compiled code gates. The consumer's strict FOV range is retained in the H-011
native captures, including `captures/re/h011/f43d70-update-nearest.c`.

Static ordering and the harness do not establish every downstream rendering
consumer, actual callback cadence through a level load, visual projection
agreement, performance, or headset comfort. Those remain runtime acceptance.

## R-127 corrections before a new physics call

1. **The environment route is proved.** At `0xDEF059/0xDEF064`, the native
   `CSystem` constructor stores `base+0x224D980` into `this+0x28`.
   Its vtable `+0x210` points to `0xDF1720`, whose nine bytes are
   `48 8B 41 28 48 8B 40 48 C3`: load `this+0x28`, then that pointer's `+0x48`.
   `0x224D980 + 0x48 = 0x224D9C8`, exactly the slot the firing guard reads.
   Finding a direct writer to the final slot is not necessary for this identity
   proof; writes through the environment pointer need not appear as direct xrefs.
2. **The second builder is `0x124C210`, not `0x124C9F0`.** Its instruction
   sequence reads the sixth argument, sets bit 22 with `BTS EAX,0x16`, and stores
   it at params `+0x64`. Thus the value is `callerMask | 0x400000`.
   The melee builder `0x124BA90` does write constant `0x400000`.
3. Native literals are established: melee/weapon `objtypes` are `0x117/0x11F`,
   with flags `0xF`. Header enum names are strong source correspondences, but a
   one-bit difference alone does not prove the bit's semantic meaning.
4. The local newer CryEngine `physinterface.h` puts `SCollisionClass collclass`
   after the skip list, making `+0x60/+0x64` a strong **type/ignore-mask lead**.
   That is not target consumer proof. Both native builders also write byte
   `+0x68=0`; the complete target contract extends beyond those two words.
5. Two native callers establish the indirect dispatch at `+0x118`, with the
   named query and caller ID 4. They do not by themselves resolve its concrete
   implementation, every output write, or the safety of concurrent calls.
   The weapon helper uses a shared hit buffer; it is not a reusable thread-safe
   wrapper for mod jobs. Existing 0x50-byte capture records should be checked
   against the concrete output writer before being used as the sole allocation proof.

No scene-query implementation was added in this build. The target call still
needs a concrete callee/output/filter audit and a bounded execution owner.

## Next feature build: alignment first

The previous compiled reproduction remains applicable: equip-time
`inverse(controller) * animatedWrist` preserves the controller's arbitrary
offset. A raycast supplies a target position; it cannot remove this rotation.
See `RE-EQUIP-ALIGNMENT-AND-HUD-SCALE-2026-09-09.md` and its 45-degree control.

The implementation gate is to recover the current weapon's authored barrel
frame and weapon-to-wrist attachment transform before the mod changes the pose,
then derive the wrist target from the same aim pose used for firing. Carry the
equipment/attachment/reference generations with those transforms. Rebind on
actual owner changes, without capturing the physical controller as a new zero.
Keep that orientation solve separate from the still-open common position-anchor
feedback documented in `RE-IK-CROSSTALK-2026-09-09.md`.

Acceptance needs weapon switches at off-axis hand angles, aim/grip separation,
recenter/tracking recovery, unchanged free-hand independence, and agreement of
barrel, projectile and reticle. Synthetic transform tests are a prerequisite,
not substitute evidence for the actual weapon assets. Then add world queries
with owned output buffers, followed by weapon-volume sweep/overlap handling.
A camera ray or a grip-to-muzzle segment alone cannot cover the whole weapon.

## Verification

Release DLL compiled with MSVC 19.50 / Visual Studio 18 2026; **31/31 CTest
checks passed**. `NearFovOverrideTests.cpp` runs the production callback with an
emulated native latch and fixture renderer: post-latch ordering, changed native
values, next-frame disable, untouched neighboring bytes, sentinel values,
concrete-slot refusal, SEH, disarming, unavailable installation, strict bounds,
and byte-gate negative controls. It does not install MinHook into Prey.

Reproduce using `build_verify.py` and `verify_static.py` in the evidence folder.
`ctest.log`, `build.log`, `static-verification.json`, source snapshots and the
build manifest identify this exact local build. It was not deployed or injected.
