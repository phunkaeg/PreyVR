# VR scheme pre-run audit and fixes — 2026-09-08

Reviewed `f03d378`, covering the six lanes in the latest scheme report. The user
then authorized fixes. No Prey launch, injection, attach, game command, or live
capture was performed. Changes are in the working tree; the rebuilt DLL is in
the isolated audit build, not the usual configure/headless build folders.

## Fixed defects

### 1. P1 — movement hooks violated the native ABI and overwrote saved RDI

`MoveLane.cpp` declared the handlers as `void(void*, void*)`. The actual action
callback has receiver, entity ID, action-name reference, activation mode, and
float value, returning a boolean. On this x64 target the float is at entry
`[rsp+0x28]`. Both handlers read AND overwrite that argument slot.

This is proved beyond a header mismatch. The retained old Release object does
`push rdi; sub rsp,0x20; call rax` without forwarding the fifth argument. If S is
the wrapper entry stack, the native entry is S-0x30; its `[rsp+0x28]` is S-8,
the saved RDI slot. The engine reads those bits as the axis value and zeros its
low dword. Returning through the wrapper then restores the corrupted register.

The fixed wrapper forwards all five arguments, allocates the outgoing stack
slot, and preserves the return. Its compiled code explicitly copies the incoming
float to `[rsp+0x20]` before `call`. Native reads/writes now address that slot.
The observer now samples movement **after** the original handler; previously its
reported axis described the old value, despite claiming to confirm delivery.

Evidence: `docs/evidence/vr-scheme-move-abi-2026-09-08.json`, containing both old
and fixed object disassembly and hashes. `tools/re/verify_vr_scheme_native.py`
checks six native byte landmarks against PreyDll SHA256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Ghidra had no defined functions at these two entries, so raw target bytes and
the compiled object were used; an undefined function was not treated as absence.

### 2. P1 — input could remain held after focus loss or disable

The lanes returned when tracking disappeared and stopped running after disable,
leaving the engine's last analog value in place. The change epsilon could also
suppress the final zero after a slow release. A failed movement post advanced
the shaper state before delivery; a partially posted pair could therefore escape
later neutralization.

The game thread now neutralizes held inputs on focus loss and disable, retries
refused releases, bypasses epsilon for exact zero, and retains delivery state
across partial movement pairs. Command setters only publish atomic requests;
they no longer reset non-atomic shaper/turn state from the polling worker.
Deadzone changes preserve the previous sent values until replaced or released.

The production-lane replay initially reproduced missing focus, disable, and
small-value releases. The fixed replay also covers partial posting and retry.
As before, a successful PostInputEvent return establishes a dispatch attempt;
actual action acceptance still requires the engine consumer or visible behavior.

### 3. P1 — the resize guard abandoned an OpenXR image and begun frame

`XrSessionHost.cpp` checked size after acquire/wait, then returned without
`xrReleaseSwapchainImage`, `xrEndFrame`, or `FrameContract::OnSubmitted`.
Consequently the local contract rejected the next wait as out of order.

The guard now runs before image acquisition. A mismatch skips copying and
reaches the normal zero-layer end-frame path. Adopting a new resolution still
requires a controlled session restart; this patch does not implement resizing.

### 4. P1 — diagnostic HUD calls entered native UI from the poll worker

`hud.call` synchronously resolved and invoked the HUD from `PollThread`, while
normal reticle dispatch ran on the game thread. It now copies name and arguments
into an eight-entry queue and drains one call on the main CSystem render seam.
The command reports `result=0 queued=1`; completion is logged separately. The
queue owns the function-name string through the entire synchronous dispatch.

### 5. P2 — reticle and firing origins still disagreed in four of six cases

The sample selected the calibrated muzzle whenever available, regardless of
`aim.origin`. Without calibration it selected the native eye, even when firing
used the hand. Only native-eye/no-calibration and muzzle-mode/calibration agreed.

The sample now describes the ray actually installed: eye for mode 0, hand for
mode 1, calibrated muzzle or hand fallback for mode 2. The reticle consumes that
publication. All six mode/calibration combinations are exercised against the
production cached-ray hook with a fake player buffer. Invalid tracking clears
the previous sample. Sample orientation is now world-space, like its direction.

A muzzle position offset is **not** a calibrated barrel rotation. The live
direction remains the OpenXR aim axis, so the sample remains `Confidence::origin`.
The old `Confidence::barrel` assignment overstated what calibration measured.

### 6. P2 — the reticle projected before the eye camera was installed

The cached-ray hook ran during gameplay update. Stereo projection is installed
later, inside CSystem::Render, and restored on return (even keep-rotation keeps
only the basis). Reading the global camera in the cached-ray hook therefore did
not provide the eye projection that would draw the image.

Reticle projection/dispatch now runs after the render seam installs the eye camera,
using the fresh, generation-checked aim sample. The normal flat render path also
updates it. This route renders one eye at a time, so the single native reticle
position can be updated for each successive eye render. It still needs visual
validation of native movie placement, especially its 16:9 canvas behavior.

## Verification and build handoff

- Full Release DLL build succeeded. Existing `strncpy` warnings in XrInput remain.
- Freshly built standard CTest suite: **28/28 passed**.
- Isolated production-code integration replays: **3/3 passed** (movement, aim,
  HUD queue); instructions in `tools/re/vr_scheme_review/README.md`.
- Six exact native ABI landmarks passed; before/after wrapper assembly checked.
- `git diff --check` passed. No headset acceptance is claimed by these tests.

Rebuilt DLL: `D:/Dev Debug/PreyVR/build/h021-integration-audit/Release/PreyVR.dll`
(767488 bytes), SHA256
`e1daab3b66343150f92bd10550e33ecf29cc2a7c7a1b29eadca4f68bb5174605`.
Its loader and build manifest are alongside it. The usual configure/headless
DLLs were not replaced; testing one of those would test the older implementation.

## Remaining feature limits

The HUD is still baked into scene images. Aspect/canvas cvars and bob controls
are placement controls, not a curved panel or independent composition depth.
No second HUD render or extraction was attempted.

The reticle uses a fixed convergence distance (default 10 m), not a hit-depth
raycast. Behind-camera aiming still leaves the previous symbol; true hiding
needs the correct native display contract. Per-eye render timing is now correct
in source, but the movie's canvas-to-pixel mapping and binocular placement need
headset/capture evidence. Do not call dispatch counters visual acceptance.

Muzzle calibration establishes position only. Matching the rendered barrel's
orientation, and the observer's sample against the model's actual IK update,
still require evidence. It is not sufficient to label the entire rig/render/ray
chain reconciled merely because `aim::Sample` exists.

Resolution launch arguments remain manual and work for arbitrary chosen sizes;
runtime recommendations are reported, not automatically adopted. The mirror-FOV
lever applies to fallback mirror content, including any 3D fallback, not to a
detected-menu-only composition layer. Existing measurements were not repeated.
