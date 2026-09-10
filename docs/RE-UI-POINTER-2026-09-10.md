# Controller beams and curved menu screens — 10 September 2026

The new implementation adds a controller ray, native Scaleform mouse delivery,
drag ownership and optional OpenXR cylinder composition to the existing floating
menu/inventory panel. Gameplay DanielleHUD extraction remains the existing
exactly-once native render callback. The [player guide](PLAYER-GUIDE-HOLOGRAM.md)
describes controls and configuration.

## Question and target

Can a controller ray select the same native menu item whose pixels appear under
the VR pointer, without dispatching gameplay mouse presses or entering Scaleform
from the XR/render thread?

Target: unmodified Steam x64 `PreyDll.dll`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`,
preferred image base `0x180000000`. All addresses below are RVAs in that binary.
Ghidra `/Prey/PreyDll.dll` metadata and read-only PE verification agree.

[Vittorio Romeo's notes](https://gist.github.com/vittorioromeo/8985f5c538deccf19e9a8c84fed64aea)
identify a separate examination-reticle path for in-world terminals. Their target
is the EGS DLL installed by Chairloader, including on a Steam installation.
Those addresses are leads, not offsets for this build. No donor code or addresses
were copied into this implementation. The fetched gist is preserved as evidence.

## Native mouse contract

The CFlashUI constructor at `0x2D3240` installs primary vtable `0x1CA6898`
and hardware-mouse secondary interface at primary+8, vtable `0x1CA6008`.
The existing HUD accessor at `0x1665780` supplies the same UI singleton.

| Receiver | Slot | Concrete callee | Observed function |
| --- | --- | --- | --- |
| CFlashUI +8 mouse interface | +8 | `0x2CFCC0` | Hardware mouse event |
| CFlashUI primary | +0xE0 | `0x2CEF30` | Native Flash mouse dispatch |
| CFlashUIElement | +0x300 | `0x2FF0C0` | Cursor event into its movie |
| Flash player, vtable `0x1DB56D8` | +0x1C0 | `0xE8C9F0` | ScreenToClient |
| Same Flash player | +0xF0 | `0xE8CB20` | GFx cursor-event conversion |

The mouse callback ABI is `void(secondaryThis, int x, int y, int event,
int wheel)`, Windows x64. Event 0 is move, 1 left press, 2 left release; wheel
is zero here. It subtracts 8 before calling the primary receiver. Native code
constructs the Flash event, applies viewport translation and chooses eligible
movies. The dispatch also retains the pressed element for the release path.
The mod does not handcraft a GFx event or guess an inventory function name.

`HudDispatchPointer` checks the accessor signature, both concrete receiver
vtables, both call slots and the 20-byte callback prologue before a guarded call.
The independent `verify_static.py` checks six concrete slots and the prologue
against the hashed PE. `static-contract.json` records the result.

The native hardware-mouse broadcaster is a second reference route: global
`0x224DAB0`, vtable `0x1D988C8`, slot +0x20 -> `0xDD0220`. CFlashUI registers
its +8 receiver from `0x2CD4C0`. The new code calls the verified CFlashUI listener
directly, keeping unrelated hardware-mouse listeners outside its scope.

Prey also selects mouse/gamepad UI behaviour in the independent input listener
`0x182D3A0`. Device 0/1 selects mouse mode 2, subject to the native force-mode
setting; XInput selects gamepad mode 3. CMouse::Init `0x9D45C0` registers
`maxis_x` as key `0x10A`. The mod posts this mouse axis with state Changed and
value zero before a pointer move. This is a mode signal with no mouse movement
or held mouse-fire button. Returning from PostInputEvent remains delivery
evidence only; visible selection is required for acceptance.

## Threading, coordinates and ownership

`XrSessionHost` builds the screen and ray from the same current tracking sample.
The hit's UV maps to the actual native backbuffer dimensions. `UiPointer` passes
one latest snapshot to the engine main-thread `CSystem::Render` drain after the
native modal-state refresh. Native UI calls never run in the XR frame service.

The selected hand is right by default. The pointer appears only over the modal
panel, with valid fresh tracking. On panel exit, focus loss, hand change or
recenter, candidate 08 cancels a captured native inventory drag, releases at the
last on-panel coordinate, and clears hover on the next drain. A trigger held on entry cannot click;
it must return to neutral. A press outside the panel cannot become a click by
moving inside while held. Leaving the panel cancels an inventory drag; it does
not place an item at the clamped grid boundary. A refused native call makes one guarded attempt
to release the held drag and disarms the pointer lane.

The beam-hand trigger is reserved for pointing while menus are modal. Other
button navigation remains available. Existing gameplay lanes still require
neutral input after leaving a menu. The UI click itself is not posted as a raw
mouse1 press through every gameplay consumer.

## Cylinder and framing

OpenXR's [cylinder contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerCylinderKHR.html)
locates the cylinder axis, not the visible centre. For arc width W and angle A,
radius is W/A. The mod places the axis one radius along the panel's local +Z,
leaving the visible centre at the original two-metre position. Height is
W/aspect, and the texture faces inward. The ray intersects that same analytic
surface; the dot is a small tangent quad two millimetres toward the viewer.

The extension is enumerated and enabled explicitly. Missing support uses the
core quad path. Default arc angle is 35 degrees, configurable from 0 to 60.
The menu and flat control card fit both runtime optical frusta, sampling the
curved top/bottom edges with the existing 72% tangent margin. This is conservative
Quest 3 framing that adapts to other runtimes, not a claim about every physical
face fit or visibility mask. Gameplay HUD stays flat for stable aim mapping.

## Evidence so far

- Release build 05 succeeds; `ctest-05.txt` records **33/33 passing tests**.
  Geometry fixtures compare independent analytic cylinder targets from an
  off-centre hand, common room transforms and narrow/canted eye frusta. Pointer
  fixtures cover entry while held, off-panel press, drag and focus loss.
- Run `player-20260910-082515`, candidate 02, delivered move/press/release through
  native DanielleShell and its GFx movie. The bounded downstream trace is
  `native-mouse-consumers-02b.json`.
- After selecting keyboard/mouse mode as a control, a right-controller trigger
  click at native pixel 927,814 opened the pause menu's Options page. Both eye
  captures `state-b/capture/after-keyboard-click-02b_*` show the result. This
  establishes an actual native selection, not merely a returned call.
- Candidate 05 adds the automatic zero-mouse-axis mode signal and launcher
  configuration. DLL SHA-256 is
  `43c378290f65ae71d03290525df2ccd575c1d827c64fbe86106100bdd8870de9`.
  Run `player-20260910-090755` proves automatic hover and a trigger click opening
  Options from the main menu at pixel 927,814, without manual mouse-mode input.
  `state-c/capture/mainmenu-05` and `options-auto-05` show before and after;
  the log records one press and one release with no pointer refusal.
- A separate private xr-sim build adds a 96-strip cylinder compositor and layer
  metadata for validation. Its patch, hashes and build result are preserved.
  It is not shipped and does not modify shared xr-sim or machine runtime state.
  `state-c/capture/curve-main-05` now shows the native menu curved in both eyes,
  with the beam and dot. Metadata records angle 0.61087, radius 3.24812 and
  axis Z=1.2481, putting the visible centre at Z=-2. `run-05-layer-summary.json`
  preserves the selected submission metadata. Its coarse pixelsCovered/aimRay
  aggregate fields are not used as geometric acceptance checks.
- Build 06 fixes two exposed lifecycle issues: an initially null `gEnv.pInput`
  waits for native readiness instead of failing startup, and a stale modal poll
  preserves the existing panel instead of flashing a projection and reanchoring.
  `build-06.txt` and `ctest-06.txt` pass, including all 33 tests. DLL SHA-256 is
  `5a1786a04fd4c0df638a981c4a058d7c59aaf064d722b6be8edb17b2f02e599c`.
  Run `player-20260910-091936` started VR automatically on a fresh launch,
  without a manual retry. Its log records gameplay and rig identification;
  xr-sim reports no session errors or out-of-order frame endings. The main-menu
  capture is `state-d/capture/mainmenu-06`. Reaching gameplay does not establish
  that the beam selected Continue. Subsequent continuous-motion tests proved
  inventory dragging; a single simulator hand teleport missed the native drag
  transition and was an inadequate drag test.
- Candidate 06 `inventory-continuous-06.json` records native drag (1,1) through
  (2,2) to (2,3), then placement of item 64982 with moving/dragging flags cleared.
  `inventory-focus-06.json` exposes a real cancellation defect: the off-screen
  move became native grid (1,1), followed by placement there. Stationary desktop
  mouse hover alone was not proved responsible for either result.
- Candidate 08, SHA-256
  `bf9cbf3c14c2fa04d3b997b26811ee72c045d4d9d1df583732fc6cc073e121d0`,
  passes **35/35** tests (`build-08.txt`, `ctest-08.txt`). Production-dispatch
  fixtures cover snapshot read contention, expiry, explicit clear, recenter,
  hand change, ordinary release, cancellation order and one-shot fault cleanup.
  Guarded-native-adapter fixtures cover receiver type, item identity, visibility,
  completed drags, unreadable memory and module-pinning gates.
- Run `player-20260910-101606` proves ordinary dragging in candidate 08:
  `inventory-normal-08.json` records item 64982 moving (1,1) to (2,3), then a
  native Place callback clearing both flags. The uniquely named both-eye
  `state-f/capture/continuous-drag-release-06-inventory-normal-08_*` captures show
  the item at (2,3); the tag contains 06 because the original motion script was reused.
- In the same run, `inventory-cancel-08.json` records that item dragged from
  (2,3) toward (2,5). Moving the beam off-panel called native CancelPickItem,
  logged `inventory_drag_cancelled=1`, then released at pixel (330,799).
  The `cancelled-inventory-cancel-08_*` captures show the item restored to (2,3),
  with no subsequent native Place callback in this bounded trace. Returning
  while held did not press again. An earlier `inventory-offpanel-08.json` had no
  inventory callbacks because the assumed source slot was empty; it is not a
  cancellation test. The second test used the observed occupied slot.

## Native inventory cancellation

Steam constructor `0x162B010` registers `inventoryPickItem` at `0x162CFE0` on
the concrete inventory receiver (primary vtable `0x1E64B40`). The callback is
observed after its original body, on the engine main thread, only while the mod
owns a native button down. Flash queues mouse input, so the callback can occur
outside the original cursor dispatch. Receiver +0x28 is the item, +0x34 moving,
and +0x35 dragging. The visible sender must have CFlashUIElement's concrete
vtable `0x1CAB358`; its IsVisible implementation reads +0x70.

Native OnPlaceItem `0x162D330` uses CancelPickItem `0x162B840` when placement
fails. The latter restores the native original location, owns fallback policy,
releases the Flash move state and clears drag flags. Candidate 08 calls that
same function after verifying exact code bytes, receiver, sender visibility,
item identity and thread. It does not write inventory coordinates itself.
Static bodies are frozen in `native-inventory-cancel-07.json`; exact receiver
and slot searches are in `steam-ui-xrefs.json`. The native call plus observed
restoration in candidate 08 provides runtime acceptance of this bounded case.

Candidate 07 was rejected at startup by an inverted module-pin check (5023).
No beam acceptance is claimed for it. Candidate 08 fixes that check and adds a
regression. A separate static defect also allowed a snapshot try-lock miss to
act as tracking loss; a fresh retained sample now bridges contention, bounded
by 200 ms and an explicit-clear generation. That defect was not established as
the unique cause of the earlier unexpected live release.

An early probe hooked the very `0x2CFCC0` bytes the mod checks, causing refusal
193 and an empty capture. That run is an invalid consumer-coverage test, not
evidence that the native cursor path is absent. Subsequent probes observed only
downstream functions. Temporary delayed captures during loading later resumed;
there is no established deadlock from those delays.

All earlier owned Prey runs were disabled and closed normally. Other fleet games
started after two of our successful pre-launch guards: Far Cry 2 during candidate
02, then SWAT4X during candidate 05. Subsequent launches correctly waited/refused.
Those concurrent workloads prevent any performance-baseline claim. No other
game was stopped or modified. Candidate 06 launched after SWAT4X closed. All
simulator runs through candidate 08 have now been disabled and closed normally.
After the user offered a headset test, candidate 08 was started through their
configured VirtualDesktopXR runtime as PID 49556, run
`player-20260910-102050`. The log confirms API 1.0 fallback, cylinder extension
support, adapter agreement, session begun and first submitted frame. The user
owns input in that session. Runtime startup is not headset acceptance.

## Remaining acceptance

Automatic mode and Options selection passed in candidate 05; normal inventory
dragging and off-panel cancellation passed in candidate 08. Still check final
candidate menu selection, focus loss, recenter while held, left-hand pointing
and flat fallback live. Physical Quest/Virtual Desktop comfort, readability and
controller feel are under user test. The Steam examination update has
since been identified in [the world-UI report](RE-WORLD-UI-CURSOR-2026-09-10.md);
world-terminal beam input still needs its surface mapping and active receiver
contract before an implementation can use that separate path.
