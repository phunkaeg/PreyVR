# Handover — H-011: the near/viewmodel pass receives no per-eye offset

**Static investigation added 2026-09-05:**
[Exact near-matrix route and proposed stereo discriminator](RE-H011-NEAR-PASS-STEREO-2026-09-05.md).
Prey's `0xFB0B70` explicitly clears view translation, and `0xFB2AC0` uses that
matrix for the nearest VP at view-info `+0xA0`, packed by `0xFB57A0` at payload
`+0x90`. Near asymmetry scaling is already implemented. These are static findings;
the weapon draw and fix still need live confirmation by the agent owning the game.

**Written 2026-09-05.** Self-contained: everything needed to work this without the
session that found it. Deprioritised in favour of the bone-rig hunt (H-005), which
is independent — this does not unblock that, and that does not unblock this.

## The finding, and how it was made

With stereo, head rotation and positional 6DoF all live in a headset, the wearer
lined up overlapping detail between the left and right eye images and reported:

* the **weapon model has no per-eye offset** — identical in both eyes, zero parallax;
* the **shadows the weapon casts do** separate correctly per eye.

That pair localises the fault. World geometry and shadow rendering receive the
per-eye camera offset; the near pass does not. One symptom alone would be
ambiguous; together they point at one pass.

**Consequence, and why it outranks the hand work:** even a perfect bone-level hand
takeover leaves the weapon flat in stereo. Until the near pass receives a per-eye
offset, the weapon cannot sit at a believable depth however its bones are driven.

## Mechanism — `INFERENCE`, do not promote without evidence

CryEngine draws first-person/near geometry through a `FOB_NEAREST`-style path that
transforms relative to the camera. Geometry rendered that way is **invariant to
camera translation**, so a per-eye offset applied to the camera moves the world and
carries the viewmodel with it — net zero parallax — while shadows, computed in
world space, separate normally.

That explains both halves exactly. **It is still inference.** A string search of
the shipped `PreyDll.dll` for `CameraSpace`, `camera space`, `CAMERA_SPACE` and
`FOB_NEAREST` returned nothing; the engine flags this in code, not in a string.
Treat as unconfirmed until the near-pass transform is actually read.

## Where to start — the near-render cvar family is the way in

Prey exposes CryEngine's whole near-render family as console variables, all four
confirmed present in the shipped DLL by string search:

| cvar | help text | notes |
| --- | --- | --- |
| `r_DrawNearFoV` | "Sets the FoV for drawing of near objects." | **Confirmed live**: changes the weapon, leaves the world untouched |
| `r_NoDrawNear` | default 0 (near objects are drawn) | the switch to hide the viewmodel entirely |
| `r_DrawNearZRange` | default 0.1 | |
| `r_DrawNearFarPlane` | default 40 | |

`r_DrawNearFoV` and `r_NoDrawNear` are already on the console allowlist, so both
are drivable today through `PreyVR_QueueConsoleCommand` with no code change.

### The five call sites

`r_DrawNearFoV`'s name string is at **RVA `0x1CA9470`** (file offset `0x1CA7A70`;
`.text` starts at RVA `0x1000`). A RIP-relative `LEA` scan of `.text` finds seven
references:

| RVA | shape | reading |
| --- | --- | --- |
| `0xECD2E1` | `lea rdx,[name]` + multi-argument call | the **registration** |
| `0x2EE2E1` | `lea rdx,[name]; mov rax,[rcx]; call [rax+0xB8]` | `IConsole::GetCVar` lookup |
| `0x1490DBF` | same | lookup |
| `0x1490F0E` | same | lookup |
| `0x17267BA` | same | lookup |
| `0x1727043` | same | lookup |
| `0x1765EC4` | `lea rcx` + `call rel32` | string compare, probably config parsing |

`IConsole::GetCVar` at vtable `+0xB8` is established by R-052.

**Correction, 2026-09-05:** none of these string lookups is guaranteed to be the
near-pass setup. R-069 already names the latched value at renderer `+0x95B4`.
Following its direct consumers located `UpdateNearestChange` at `0xF43D70` and
the separate view-info/nearest-projection route above. The symptom still supplies
the discriminator: zero weapon parallax with correct shadows.

## What is already ruled out

* **`i_offset_front` / `_right` / `_up` are vestigial.** Registered since the R-076
  survey with no consumer ever found; driven live 2026-09-05 at 0.25 m and then
  **1.0 m on all three axes** with a wearer watching, and **nothing moved at any
  magnitude**. They join `g_detachCamera` (R-056) as GameSDK leftovers Prey does not
  consume. This was the obvious route to per-eye weapon separation and it does not
  exist.
* **Moving the near-pass camera is not an acceptable fix for hands.** It renders
  *everything drawn close* — both arms and the body — so its camera cannot give
  independent per-hand control. It is the right lever for **stereo offset** and the
  wrong one for articulation. (Product decision, made explicitly.)

## Tools that already exist

* `PreyVR_QueueConsoleCommand` — drives console variables with no in-game console.
  Requires the frame observer enabled (`PreyVR.enableObserver()`), which also
  initialises MinHook.
* `PreyVR.startVr()` in `tools/live/preyvr-harness.js` — full bring-up in one call,
  and it **refuses** if the declared frustum does not match the rendered one.
* Hardware watch/apply facility in `src/dll/DebugWatch.*` — execute and data
  breakpoints with full register capture, and an apply mode that can edit memory at
  a chosen instruction. See R-079 and RE-009 for the mechanics and their silent
  failure modes.

## Acceptance test

The wearer's own test is the acceptance test, and it needs no instrument: with
stereo live, line up overlapping detail between the eyes. The weapon must separate
per eye the way world geometry does. Shadows already do, so they are a built-in
control — **a fix that moves the shadows differently has changed the wrong thing.**

## Cautions carried from this project

* Confirm the animator/renderer is actually running before believing any zero. A
  menu, a pause or the game in the background all freeze it, and each produces a
  confident zero indistinguishable from a real negative. This mistake was made
  three times in one session.
* A cvar being *registered* does not mean it is *consumed* — see `i_offset_*` above,
  and `g_detachCamera` before it.
* One pass changing while another does not is the proof that a lever reaches the
  pass you think it does. `r_DrawNearFoV` was accepted on exactly that basis.
