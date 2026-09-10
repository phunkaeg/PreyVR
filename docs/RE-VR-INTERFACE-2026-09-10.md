# Player interface integration — 10 September 2026

The player preview adds an automatic launcher, controller menu navigation, a
recenter command, a spatial menu panel, and a separate transparent native HUD
layer. The target remains Steam `PreyDll.dll`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
The [player guide](PLAYER-GUIDE.md) describes the supported controls and limits.

## Native interface seam

Question: can DanielleHUD draw once into a private target, without a second Flash
advance/render, while the original scene copy disappears?

`CFlashUIElement::Render` at RVA `0x2FEBC0` supplies the element name at `+0x30`
and primary player at `+0x58`. The measured primary player vtable is `0x1DB56D8`;
its render proxy is the secondary interface at `player+8`, vtable `0x1DB58B8`.
The queued render callback is `0xE8C5E0(proxy, release)`. The game supplies the
correct receiver and render thread. Source ancestors were not used as offsets.

`HudLayer.cpp` identifies only DanielleHUD, redirects its existing callback to a
transparent D3D11 target, and calls the original exactly once with its original
release argument. The redirect preserves native stencil contents with a private
depth copy. A thread-local `OMSetRenderTargets` guard replaces only a bind of the
original top-level target; internal Scaleform filter targets stay native. The
original render targets are restored before returning. There is no second
`CSystem::Render`, extra Flash advance, or external thread making engine UI calls.

Raw source texture evidence is in `evidence/vr-interface-2026-09-09/hud-raw/` and
`hud-alpha-check.json`: 2560×1440 RGBA, 60,817 nonzero-alpha pixels versus
3,625,583 zero-alpha pixels. The image contains HUD graphics, with no scene.
Some glow pixels intentionally carry additive colour with zero alpha; a strict
RGB≤alpha test would incorrectly reject them. Callback and target observations
are in `flash-bindings-gameplay.json` and `flash-bindings-inventory.json`.
The on/off control and submitted layer captures distinguish extraction from
simply drawing a second HUD over an unchanged scene copy.

The HUD is submitted as a premultiplied alpha OpenXR quad. Unsupported targets
or image failures disable extraction so native HUD rendering resumes. Subtitles
and world markers remain in the scene. This is one native HUD canvas, not separate
health, ammo and prompt widgets. The reticle keeps the native engine's screen
fraction and remaps only its Flash dispatch to the smaller canvas. Off-panel
coordinates clip naturally instead of making a misleading edge reticle. Its
depth is fixed to the HUD plane; it is not a world-surface raycast marker.

## Spatial layout and controller input

The panel is 2 m away, with a combined menu/control-card envelope capped at 60°
horizontal and 40° vertical. The fitter checks corners against both actual
runtime eye poses/frusta, including asymmetry and cant, with a 72% tangent margin.
Quest 3 simulator frusta are left −54°/+44°, right −44°/+54°, and ±55° vertically.
Those are the tested simulator profile, not a universal optical measurement for
every user's face fit. Narrower runtime frusta reduce the panel further.

Menus are anchored when opened and repositioned on recenter; the gameplay HUD
follows head orientation. The main scene uses the existing stereo path. Menus
copy the complete current backbuffer into a BOTH-eye core quad, preserve aspect,
and invalidate retained gameplay eye images when entering/leaving the panel.
The separate control card uses an opaque sRGB texture and a static swapchain.
Candidate 06 intermittently lost parts of its translucent card in simulator
captures, including after retry. Its native menu remained visible. Candidate 07
removes unnecessary alpha blending from this fully rectangular card; this is a
presentation simplification, not a claim that the earlier disappearance's writer
or cause was established. The gameplay HUD still requires premultiplied alpha.
The opaque card also lost text in some simulator captures. A bounded D3D11
readback on xr-sim's own draw thread captured its actual 1024×160 GPU texture:
all three text lines and alpha 255 were intact (`guide-texture-07.bgra`, JSON
metadata and PNG). The same instrumented frame composited correctly. This proves
the upload on that inspected frame, not the cause of the intermittent failure;
the stall may alter timing. Do not claim all control-card frames passed. The
native menu remained present, and the player guide contains the same controls.

This follows the comfortable-distance and central-framing guidance in
[Meta's comfort guidance](https://developers.meta.com/horizon/design/comfort/)
and [display guidance](https://developers.meta.com/horizon/design/display/).
[Core OpenXR quads](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerQuad.html)
provide a portable baseline; the optional cylinder extension is not required.
There is no claim that a flat quad has become curved.

`HudBridge` samples the concrete UI visibility byte at `+0x70` on the game thread,
using the verified getter `0x2FD150` (`0F B6 41 70 C3`). Shell, pause, options,
save/load, loading, PDA, external inventory, and modal dialogs gate gameplay
input. Stale/unknown UI state does not enable locomotion or firing.

Right stick navigates; A/B select/back; X/Y carry native contextual actions;
grips select tabs on release; triggers select pages. Both grips cancel pending
tab changes and recenter, including staggered chord presses. Buttons/sticks held
on entry must return to neutral. Gameplay move, turn, fire and action producers
also require release after closing a menu. Input press/release pairs reserve
queue capacity atomically so a full queue cannot leave an orphan press.

Touch bindings include both X/Y. Grip is a float squeeze action with a threshold,
which also avoids the simulator's unsupported boolean squeeze/value conversion.
Index bindings are supplied from Khronos registry paths; left stick click is
pause because the Index system button belongs to the runtime. Physical Index
controllers were not tested.

## Startup and recovery

`Start Prey VR.cmd` finds/selects the game, checks the exact supported binary,
starts Prey at the saved render size, waits for its window/module, and uses the
included x64 injector. It writes no mod DLL into the game directory. Runtime
overrides are process-scoped. The launcher refuses a concurrent fleet game.
Settings persist beside the launcher in `PreyVR.json`.

The VR coordinator waits for the requested backbuffer to settle, starts XR,
applies console settings through the frame queue, waits for a fresh tracked
head, recenters, enables the render/input/IK features, and requires successful
image submissions before reporting ready. F11 enables/retries; F12 and the grip
chord reset yaw and position together. Refocus with a held F-key does not trigger
an action. Renderer-size and tracking failures name their stage.

The old startup `hud_bobHud` command was removed: the actual title-screen game
log reported it as unknown. A bridge return code did not prove cvar acceptance.

Candidate 04's forced session-loss test crashed in a native culling worker at
`0x27EC2A`, reading `CCamera+0x228` as a camera-list pointer. The pointer contained
`0x3F80000000000000`; the original dump and decoded context are retained. This
does not identify the writer. Candidate 05 survived normal disable/re-enable
both at the menu and in the loaded level. Runtime loss now requests shutdown,
waits for the worker to disarm native camera/pose/input consumers, and only then
destroys XR resources on the render thread. Validation of that order is recorded
below; do not call the original corruption's writer proven.

## Corrections found while exercising the loaded save

The prior static arm fixture treated raw QuatT spans as DynArrays. On the actual
target, `CPoseData` vtable `0x1D27228`, slot `+0x48`, points to `0x87C6D0`:
`return *(this+0x18) + index*28`. Count is `pose+8`. Thus the embedded skeleton
bind count is `skeleton+0x20`, absolute slice `skeleton+0x30`. The current ADIK
pose follows the same count/relative/absolute fields. The word before the slice
was a preceding float (`0xB9E57000` in the capture), not a count. Production
readers and fixtures now use the owning counts and poison the false prefix.
True joint/attachment/ADIK DynArrays retain their measured prefix contract.

The wrench's concrete vtable is `0x1E92F00`; its ammo helper is empty and its
root/cog bind rotations are identity. Its explicit presentation policy aligns
the authored weapon model frame, restoring hand motion without pretending it
has a barrel. The Disruptor (`0x1E31290`) has a positional `muzzle` with a rotated
axis; its measured `fx_muzzle` supplies the barrel-facing presentation frame.
Neither policy changes native ammo positions or introduces a physics call.

Bone attachment names use `+0x10` (`0x8CE820`); the skin attachment vtable
`0x1D1EDA8` uses `+0x18` (`0xE92610`). Both are direct native getter-byte proofs.
The resolver can pass an unrelated skin while looking for a bone helper, but
refuses a matching unsupported helper rather than changing native precedence.

These correct the layout and asset-coverage assumptions of
[the earlier static alignment report](RE-WEAPON-BASIS-ALIGNMENT-2026-09-09.md).
Its frozen evidence remains intact. Static fixtures alone did not establish live
asset compatibility.

## Observation environment

Actual injected Prey, existing Talos Lobby save, D3D11 on the matching adapter,
with xr-sim and xr-tape. No headset acceptance or GPU frame-time budget is claimed.
The private validation runtime fixes three independent instrument defects:
compositor matrix packing, view-matrix inversion, and per-hand action state.
`private-runtime.patch`, source hashes and its CTest log preserve that scope.
Neither the shared runtime nor the player package includes those test changes.

Source/build snapshots, final validation results, and receipt hashes accompany
this report. Remaining acceptance includes physical Quest/VDXR comfort, other
headsets/controllers, all weapons and their animated reload/recoil states,
in-world terminals, and performance under representative combat load.

## Candidate 06 live results

The launch used the packaged DLL in `build/packages/20260910-player-preview-06`,
PID 72072, run `runs/player-20260910-021201`. Only this verified process was closed
at the end. No other fleet game was running before launch. Shared runtime
registration and game binaries were unchanged.

| Check | Measured result |
| --- | --- |
| Automatic launcher, title, Continue, loading prompt | Ready; A input reached each native screen and the existing Talos Lobby save. |
| Inventory | X opens; right stick navigates; trigger changes to chipsets; released grip changes to Neuromods. Screens visible in both eyes. |
| Recenter | Head moved to (0.45, 1.8, 0.3), yaw 25°; staggered two-grip chord moved the panel back in front without changing the tab. |
| Held controls across actual menu exit | Stick and trigger remained held in `held-exited-06.json`; gameplay raster returned. `turnPosted=1`, `firePressed=0`, `fireReleased=0`, camera yaw 111524 mdeg and move axes 0,0 stayed unchanged across before/after reports. |
| Disruptor | `attachment_default`, helper `fx_muzzle`, forward visible model. |
| GLOO | Equipped with hand yaw 30°, returned to forward without equip-angle capture; `attachment_default`, helper `muzzle`. |
| Wrench | Equipped with hand yaw −30°, returned to forward; `wrench_model`; generation 9, 16,370 alignment applications, zero refusals. |
| Floating HUD | 16,827 captured, zero refusals at the wrench report; transparent HUD over scene, native off/on control from earlier candidates retained. |
| Forced XR loss inside PDA | Features disarmed before teardown; `vr.enable` recovered in the same process. |
| Forced XR loss during gameplay | Repeated disarm/teardown/re-enable succeeded; frames, HUD and arm drive resumed. |
| Crash check and close | Native error.log timestamp remained the older candidate-04 fault (01:51:04 AEST); candidate 06 closed normally after `vr.disable`. |

These are in-game observations with a simulated headset/controller profile.
The menu-only `held-menu-before/after-06` pair did **not** cross back into gameplay
and is not the exit proof; use `held-exit-before/after-06` and the raster instead.
Some earlier probe filenames say “pistol” although their measured asset is the
Disruptor. Use the concrete class and helper data, not filenames, as identity.
Empty/error probe outputs are excluded from affirmative receipt evidence.

The earlier candidate-04 session-loss fault is preserved in its own dump, native
error log and register/disassembly records. Two clean recoveries after ordering
the shutdown are regression evidence, not proof of the original pointer writer.

## Final candidate and trace limits

Candidate 07 (`runs/player-20260910-023114`, PID 121024) rebuilt the opaque card;
all **33/33 CTest tests passed** in 8.01 seconds. DLL SHA-256 is
`7e38452274fc570b0adfaa67db46c0c2417755356c38f6a5512436fc42f755b0`.
Automatic startup, title/Continue/loading prompt, actual inventory and return to
gameplay were repeated. `final-active-report-07.txt` records active VR, Disruptor
`fx_muzzle`, 3,075 alignment applications/zero refusals, and 3,174 HUD captures/
zero refusals. `final-gameplay-07` and `guide-probe-07` are the inspected final
captures; earlier `gameplay-07`/`inventory-07` captures were still at the loading
prompt and must not be cited as inventory acceptance. The game closed normally
after `vr.disable`; the error-log timestamp was unchanged from candidate 04.

The three candidate-06 xr-tape traces were checked with `xrtape_check.py`; none
gets an unqualified overall PASS. The first and third hit their 20,000-frame cap.
The checker reports 1/2/7 zero-layer frames respectively around transitions;
the mod intentionally invalidates old eye images rather than reuse them across
menus. The generic located-FOV equality check also fails: this integration
declares the measured native rendered frustum (120° horizontal) and the runtime
reprojects it into its own optics. Equality to the simulated optical frusta is
not this implementation's projection contract. This does not establish perfect
native culling or eliminate the existing vertical coverage limit. Layer-budget
and depth checks were skipped, not passed. Full checker outputs are preserved.

Candidate 07 produced no xr-tape output despite the requested process environment;
its observation evidence is the mod log, xr-sim layer/pixel captures and the
bounded readback. Do not silently promote candidate-06 traces to the newer DLL.

The distributable is a **player preview**, with the known control-card capture
inconsistency and physical headset/readability acceptance still open. It is not
a claim of full-runtime conformance, all-weapon coverage or production readiness.
