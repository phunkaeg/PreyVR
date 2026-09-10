# Hologram candidate 10 — validation status

This is a test candidate, dated 10 September 2026. Candidate 10 adds right-stick
click to the native Favorites Wheel shortcut. This binding is built and checked
offline; opening and selecting weapons with it still require a live check.

Candidate 09 fixed the rendered-eye queue draining during menus and skipped XR
submissions. The user confirmed corrected stereo and a curved in-game popup in
the Quest / Virtual Desktop session. That session then crashed on a native job
worker (read address 0x23C, Steam PreyDll RVA 0x27EC40). The cause remains unresolved;
this candidate retains the stereo fix and is not a stability-approved release.

Candidate 10 DLL identity is recorded in its package manifest.

## Checked

- Release build succeeds and all 35 offline tests pass, including analytic
  curved-screen hit mapping, transformed poses, conservative framing in both
  eye frusta, held-trigger entry and drag button-state handling.
- The preceding candidate 05 visibly hovered over Options and opened it with
  the right-controller trigger in the actual game, through xr-sim. It also
  rendered the curved menu, beam and dot in both simulator eye captures.
- Candidates 06 and 08 started the game and enabled VR through the launcher without a
  retry. Its log records reaching gameplay. The simulator reports no session
  errors or out-of-order frame endings in the inspected session.
- Candidate 08 moved an inventory item from (1,1) to (2,3) through continuous
  controller motion and trigger release. Native callbacks and both-eye images
  agree. Moving that item toward (2,5), then leaving the panel, restored it to
  (2,3) using native CancelPickItem. Returning while held did not press again.
- Candidate 08 also starts successfully through VirtualDesktopXR with cylinder
  support. The user is testing it in the Quest; startup is not comfort acceptance.
- Native mouse-call signatures, concrete receivers and input-mode selection
  were checked against the supported Steam executable. Offline tests and a
  returned native call alone are not evidence that a particular UI accepted it.

## Still to test

- Final-candidate main-menu selection, beyond the preceding candidate's pass.
- Live drag cancellation on focus loss, recenter and hand change;
  left-hand pointing and flat-screen fallback in the final candidate.
- Broader Quest 3 / Virtual Desktop readability, comfort and controller feel.
  Correct stereo and visible curved in-game menus were confirmed in candidate 09. The private simulator cylinder
  compositor used here is validation instrumentation and is not shipped.
- Broader weapon, level and campaign coverage. This is not a performance result.

In-world terminals use a separate examination path and are not controlled by
this menu beam yet. Gameplay HUD and menus also have different presentation:
the native HUD uses a transparent flat layer, while modal menus use the floating
quad or cylinder. The reticle remains at the HUD's fixed depth.

Project evidence: `docs/RE-UI-POINTER-2026-09-10.md` and
`docs/evidence/ui-pointer-2026-09-10/`.
