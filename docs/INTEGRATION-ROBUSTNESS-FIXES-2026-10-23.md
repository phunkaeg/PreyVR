# Robustness integration fixes — 23 October 2026

Integrate `jordi/robustness` at `b07a056c047741257a1443ee53fe4a7724e032b5`
into `collab/integration`, starting from `368118b10cb1482dbaedd8b38f8b5ba4e9d4a0d8`.
Do not include `jordi/robustness-body` or its `8de6574` full-body solver.

## Changes

- VR shutdown/failure disables left-hand interaction, carry, grenade steering and
  unarmed arm takeover. Pending carry releases and motion history are invalidated.
  Native hooks remain installed and forward native behaviour when disabled.
- Physical trigger release is distinguished from menu/tracking/disable/recenter
  cleanup. Engine-owned held buttons still receive their releases. Cleanup cannot
  authorize the mod's early grenade throw or apply cached hand velocity. This
  does not claim to cancel every native animation or native throw callback.
- Motion samples require fresh publication, valid hand tracking and matching
  session/reference identity. Invalid, duplicate or discontinuous samples reset
  history. Release estimates add an elapsed publication duration to XrTime;
  absolute QPC and OpenXR timestamps are never compared.
- Carry release requests are bound to their held entity, epoch, reference and
  lane generation. Tracking/reference loss returns a held object to native carry;
  repick it to attach it to the hand again. Native guarded drop and collision
  restoration remain intact. Missing velocity with valid tracking means a drop.
- Grenade releases are bound to the same weapon/session/reference. A missing hand
  or refused obstruction query retains native spawn and velocity. Valid misses
  allow controller spawning; hits pull the spawn back. Suppression of the delayed
  animation event survives shutdown when its grenade has already been thrown.
- A narrow game FOV no longer silently reverts to an incompatible projection
  declaration. The normal integer-copy path remains. When that native-field rect
  exceeds the swapchain, an angular UV remap resamples into the available image.
  Unsupported/failed remaps skip the projection. Immutable-FOV runtimes always
  retain the native field, including when `xr.nativefov 0` was requested.
- The remap uses private UNORM views to preserve inherited pixel encoding across
  RGBA/BGRA and linear/sRGB swapchain spellings. A deferred D3D11 command list with
  `ExecuteCommandList(..., TRUE)` restores the game's context state; see
  [the Microsoft API contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-executecommandlist).
  It cannot reconstruct scene pixels outside the rendered field; those stay black.
  Floating quad/cylinder menus keep their original pixels: projection-FOV
  placement must not alter the native menu band or controller hit coordinates.
- The shared layer-texture validator accepts valid small textures, including our
  existing 1024×224 body-slot card. Invalid dimensions/formats/array/sample layouts
  still refuse. This fixes our inherited card preparation bug.
- **VR Options → World** adds saved left-hand interaction (on by default), hand
  carry/throw and hand grenades (both experimental, off by default). Carry requires
  left-hand interaction. Stopping VR preserves choices while disabling effects.
  The dirty-setting mailbox is widened to 64 bits. Existing gameplay toggles remain.
- Saved Debug Overlay OFF and Muzzle Aim OFF are applied. Explicit process launch
  overrides use `PREYVR_DEBUG_OVERLAY` / `PREYVR_MUZZLE_AIM`; normal startup never
  turns rays on just because the current mask is zero. Launcher switches are retained.

Jordi/Thomas rendering, foregrip, IK, interaction, native HUD extraction and
inventory isolation work are retained. No new native address or ABI is introduced
by this correctness pass. Full-body pose/feet/crouch work remains outside this merge.

## Validation and limits

Release build: MSVC 19.50.35729.0, Visual Studio 18 2026, x64, pinned local MinHook
and OpenXR 1.1.60 sources. **73/73 offline CTest checks passed.**

New tests exercise the production carry/grenade adapters with native/XR boundaries
substituted, the actual D3D11 WARP FOV shader and pipeline restoration, plus motion
and fire ownership transitions. Existing tests now cover the status-card texture's
real swapchain preparation/upload and saved opt-outs through the options adapter.
The options panel was rasterized with its production renderer and inspected.

No Prey launch, injection, xr-sim game session or headset run was performed.
Native gameplay admission, physical throw feel, all weapon/UI states, visual FOV
quality and headset comfort still need acceptance. Source tests do not prove these.

Reproduce after configuring a normal Windows build with the pinned dependencies:

```powershell
cmake --build <build-directory> --config Release --parallel 6 -- /nr:false
ctest --test-dir <build-directory> -C Release --output-on-failure
```

The frozen results and tested DLL identity are in
`docs/evidence/robustness-fixes-2026-10-23/`. No release package is published by this merge.
