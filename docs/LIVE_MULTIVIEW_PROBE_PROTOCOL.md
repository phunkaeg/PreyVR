# Live multi-view probe protocol (H-001, remaining half)

## Question

Does Prey's renderer retain a view structure that can carry more than one view info? The stereo
half of H-001 is already a confirmed negative — there is no `r_Stereo*` control surface, no
`IHmdDevice` layer, and no CryVR plugin. This protocol decides the multi-view half, which literal
search cannot answer because renderer RTTI is absent from the shipping build.

The answer selects between the two remaining per-eye routes:

- **more than one view info** — populate the second directly; far cheaper.
- **exactly one** — the per-eye route must be a mod-owned scene re-entry at the
  `BeginRendererScene`/`EndRendererScene` boundary.

## Why the command ring is the target

Decompiling `EndRendererScene` (`PreyDll.dll+0xF7E210`) found exactly one doubled structure in the
whole function:

```text
if (renderer[0xAC30] == 0) {
    renderer[0xAC28] = 2;
    for (2 entries) FUN_181081760(renderer + 0xD98, entry);
} else {
    FUN_1810822B0(singleton + 0xD98, renderer[0xAC30 + (renderer[0xAC28] & 1) * 8]);
    renderer[0xAC2C] = renderer[0xAC28] - 1;
    renderer[0xAC28] = renderer[0xAC28] + 1;
}
```

Two entries, a counter initialised to 2, and parity indexing. That is the shape of CryEngine's
render-thread double buffering, and each entry is the leading `CRenderView` candidate.

This is a hypothesis about identity, not an established fact. The probe must be prepared to find
that these entries are command lists rather than render views.

## Preconditions

- Prey running and sitting in a **stable loaded save**, not a menu or loading screen.
- ReGenny running and attached to `Prey.exe`.
- `regenny/PreyVR.genny` open, providing `PreyRendererFrameProbe`.
- No debugger breakpoints set. This probe is read-only and must stay that way.

## Steps

1. Attach ReGenny to `Prey.exe`.
2. Select type `PreyRendererFrameProbe` at address `PreyDll.dll+0x2B3E8E0->0x0`. R-005 holds a
   *pointer* to the singleton, so exactly one dereference is required.
3. **Sanity-gate before trusting anything else.** Require all of:
   - `swapchain` and `device` non-null and pointing into `dxgi.dll` / `d3d11.dll` vtables;
   - `scene_nesting` a small non-negative integer;
   - `frame_slot` small and changing between samples.
   If these do not hold, the base address or the dereference is wrong. Stop and re-derive rather
   than interpreting garbage.
4. Record `command_ring_count` across several samples and confirm it increments, and that
   `command_ring[0]` and `command_ring[1]` are both non-null and distinct.
5. For each ring entry, attempt `regenny_rtti_vtable_typename`. Renderer RTTI is absent, so a
   nameless result is the expected outcome and is not evidence of anything.
6. Dump `0x400` bytes at each entry and look for **repeated float blocks with matching stride** —
   a view info carries a view matrix, a projection matrix, and a camera position. Two adjacent
   copies of that pattern within one entry is the multi-view signal. One copy is the negative.
7. Cross-check the candidate by moving the player and re-sampling: values inside a real view info
   must change with camera motion. A block that stays constant under motion is not a view info.

## Decision rule

- **Two or more view-info blocks per entry, confirmed to track camera motion** — multi-view is
  retained. Promote the offsets and open a follow-up on populating the second view.
- **Exactly one block, or the entries are not view-like at all** — the stock resident multi-view
  candidate closes negative and a mod-owned per-eye route becomes the working assumption.
- **Ambiguous** — record what was seen and do not promote either way. An honest null is a result;
  a guessed layout is not.

## Safety

Read-only throughout. No writes, no breakpoints, no code modification. `PreyRendererFrameProbe`
exists to be read; nothing in this protocol authorises writing to any field in it, and the Present
and vsync fields it models are explicitly out of scope here.

## Status

**Completed 2026-08-01. Result: the probed resident view block is single-view.** This closes the
stock/exposed route, not every possible dynamic scene re-entry seam.

See [`../captures/traces/2026-08-01-prey-multiview-probe.md`](../captures/traces/2026-08-01-prey-multiview-probe.md).

Outcome against the decision rule above:

- The command-ring hypothesis was **refuted**. Both entries are `ID3D11Query`-shaped D3D11 COM
  objects (11 contiguous `d3d11.dll` vtable entries), double-buffered GPU timing queries. Recorded
  as R-027 so the candidate is not re-investigated.
- The real view block was found next door at renderer `+0x4A08`, stride `0x328`, and promoted as
  R-026. It carries **exactly one** orthonormal camera per slot.
- The two slots are frame double-buffers, not eyes: while the player was stationary they were
  byte-identical, separation `0.00000`. An eye pair must differ by a constant lateral IPD offset at
  all times.
- Liveness passed — position moved `0.67244` units over 100 frames with visible head bob in Z.

The statically-derived offsets in `PreyRendererFrameProbe` are therefore validated, and the
confirmed camera/frustum group has been promoted into `PreyRendererView` in
`regenny/PreyVR.genny`. The Present and vsync fields in the probe overlay remain read-only and
out of scope.
