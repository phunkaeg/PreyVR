# Review of PreyVR 0.5.0 preview

Reviewed commit `1ac2d61`, with a clean working tree before review. This is a
source/integration review and offline build validation, not a new headset run.
The findings concern the combined preview implementation; commit authorship does
not establish which agent originally wrote each change.

## Findings

### P2 — Refresh the modal state before validating queued menu presses

`src/dll/InputPost.cpp:154` calls `MenuTapDispatch::Allow` with the cached menu
epoch and visibility. But `src/dll/CameraEditHook.cpp:1032` drains input before
`DrainQueuedHudCalls`, which refreshes those values at `HudBridge.cpp:500`.

If native input or a UI transition closes a menu between render callbacks, the
next queued menu press can still pass the previous frame's fresh-looking
`menuOpen=true` and matching epoch. It then reaches native gameplay consumers.
A queued Y/D-pad event can therefore still select/open weapons across this
transition. The epoch guard correctly handles transitions it has already sampled,
but that is weaker than checking the current native modal state before dispatch.

Refresh the native state on the main thread before validating queued presses.
Retain the dispatcher's existing rule that a delivered press receives its release
even if the press closed the menu. Test the actual refresh/dispatch ordering with
a menu that closes externally between frames. The current `input_queue` cases
test the policy with an already-updated state; they cannot catch this ordering.

This establishes a remaining timing window, not that it caused the wearer's
previous spontaneous-wheel observations.

### P2 — HUD-only screenshots consume the XR frame's capture

`src/dll/HudLayer.cpp:297` changed `HudLayerTexture()` into a consuming read:
it clears the capture timestamp. An existing diagnostic caller remains at
`src/dll/FrameCaptureWin32.cpp:165`.

`FrameObserverHook.cpp:60` runs `ServiceFrameCapture` before `ServiceXrFrame`.
Consequently, `capture.hud` obtains the texture and clears its stamp before the
XR host obtains it at `XrSessionHost.cpp:1171`. The native HUD was already
redirected away from the scene, but the host now sees a null HUD texture,
omits the overlay, and records that eye as lacking a separate capture. That
incorrect eye flag can suppress the overlay until the affected eye refreshes.

Give the screenshot path a non-consuming peek, or share one frame capture result
with both consumers and drain it only at the frame boundary. Merely moving the
screenshot after XR would make the diagnostic itself fail to find the capture.
Check that a HUD-only screenshot leaves both XR overlay eligibility and the
retained eye's capture metadata unchanged.

### P3 — The diagnostic `menu.gate 0` bypass no longer reaches the queue

`XrInput.cpp:380` still lets the navigator operate outside a menu when the gate
is disabled. However, line 413 passes a nonzero menu epoch for every action except
Start. `InputPost.cpp:206` rejects these scoped actions when no native menu is open,
regardless of the bypass setting. The reported action counter also increments even
when that enqueue is refused.

The default safety gate should remain enabled. Either explicitly carry the
diagnostic bypass through the enqueue/dispatch policy, or retire the bypass command
and update its documented use as a control. Do not treat `menu.gate 0` as a valid
experiment for restoring the old ungated behavior in this build.

## Checks that passed

- Release build completed from the reviewed source.
- CTest: **39/39 passed**, including inventory swapchain WARP pixel copies,
  inherited linear/sRGB formats, failure handling, input queue and stereo tests.
- DLL SHA-256: `7F5727292A9ECBCB2D9E21216B2600066691A1F0A34702AEA72A8C602537612F`.
  This exactly matches the preview's recorded package manifest.
- Inventory format selection follows `gHost.colorFormat`; the former independent
  hard-coded sRGB selection is absent.
- Inventory swapchain destruction precedes XR session destruction; timeout handling
  does not treat a positive `XR_TIMEOUT_EXPIRED` as permission to copy/release.
- Capture requires a compatible consumer lease, and ordinary upload failures use
  the captured inventory as the legacy panel source for that frame.

Build/test logs: `build/review-0.5.0-20260913/`. The initial build encountered the
host's duplicate `PATH`/`Path` MSBuild environment problem. A process-scoped
canonical `Path` variable and disabled MSBuild node reuse resolved it; no project
source or machine-wide environment setting was changed.

No production source was changed, no game was launched or injected, and no package
was redeployed by this review. Full headset acceptance and the reported wheel and
reticle symptoms remain separate from the offline checks.

## History repair check

The live repair manifest `build/chat-recovery-20260912/repair-20260913-075033/manifest.json`
records successful publication. Both tasks now use recovered transcript paths.
SS2's complete file is indexed (5,223 items). Prey's recovered completed history
contains 5,038 items, and Codex's task API returns the formerly missing latest
completed turn. The old ordinal-error log entries inspected predate the repair.
This verifies historical recovery; it is not a guarantee against future app bugs.
