# Inventory swapchain separation — static implementation

Scope: source implementation and offline testing only. No game launch, injection,
runtime setting change, or headset acceptance in this step. The installed player
package and the running game have not been changed.

## Implemented

`InventorySwapchain` owns a dedicated, single-image-array OpenXR swapchain for the
captured DaniellePDA texture. The existing world swapchain retains its two eye
slices. On a successful inventory upload, `ServiceXrFrame` skips acquiring or
copying into that world swapchain and submits the inventory handle through the
existing quad/cylinder. Pointer mapping, curvature, framing and recentering use
the same panel geometry as before. The separate panel uses source alpha with
the existing HUD path's premultiplied-alpha convention.

Capture is opt-in (`ui.inventory 1`) and additionally requires a recently
serviced, format-compatible consumer. The first frame prepares that consumer
while the native inventory still renders normally. A successful frame end arms
the next capture. Only DaniellePDA with no other recognized visible modal is
eligible; dialogs, loading, external inventory and other menus use the complete
native menu image. Visibility-call failures refuse inventory-only eligibility.

Every render-frame boundary drains the PDA capture once, including skipped XR
submissions. The original Flash callback is invoked once; this does not replay
its reference-release path or change its view matrices. The existing stereo
eye-ticket consumption remains the first operation in `ServiceXrFrame`.

The owner validates dimensions, sample count, image count and copy-compatible
format families. RGBA and BGRA never substitute for one another. It inherits the
actual selected main/menu swapchain format (linear UNORM or sRGB); an unsupported
format leaves native rendering in place. Resize/session changes
recreate the inventory resource; teardown destroys it before the XR session.
The existing world-session resize policy still requires a restart.

Upload waits are bounded to 50 ms. An acquired image may be copied and released
only after `XR_SUCCESS` from wait, including when a positive timeout was returned.
A wait/release fault requests session shutdown and disables capture. Other copy
refusals disable capture. If a draw was already redirected, its compatible PDA
texture can supply that frame's legacy opaque panel. This failure fallback may
write the world/menu swapchain; isolation applies to the successful separate
inventory route. A disable command arriving after capture finishes that last
captured frame before returning to native drawing.

## Offline verification

Build directory: `build/inventory-swapchain-static-20260911`, Release.
Logs: `evidence/inventory-swapchain-2026-09-11/`.
Final build and CTest results are in `build.txt` and `ctest.txt`; `artifacts.json`
records the relevant source and binary hashes for that revision.

The `inventory_swapchain` test executes the production owner with real D3D11
WARP textures and injected OpenXR functions. It checks pixel/channel/alpha
preservation, an independent world texture remaining unchanged, array size,
resize and session recreation, unsupported formats and MSAA, null images,
allocation-refusal caching, acquire failure, invalid image index, positive wait
timeout, release failure, no retry after a session fault, and destruction.
Format regressions cover both linear and sRGB main formats in RGBA and BGRA,
recreation when the selected format changes, and refusal instead of silently
substituting a different transfer function.
These are offline software-device checks, not evidence of a real XR runtime
accepting the layer or Prey's native capture producing correct alpha.

## Commands and next headset checks

In a future boot of this candidate, with VR and menu panels enabled:

```text
ui.inventory 1
ui.inventory
```

The report distinguishes `inventoryCapture`, the short-lived
`inventoryConsumer` lease, `DaniellePDA` capture counts and cumulative
`inventoryLayerFrames`. A lease may momentarily read zero between servicing
frames. The first successful separate submission also logs
`first_inventory_layer_submitted separate_swapchain=1 array_size=1 world_copy=0`.
Use `ui.inventory 0` to return to the native full-menu route. It remains off by
default, and no startup script enables it in this change.

Next verify repeated open/close, switching PDA tabs, native dialogs over PDA,
inventory drag/click/hover, toggling capture while open, recentering, quad and
cylinder modes, and session restart. Confirm proxy identity/capture counters,
legibility, alpha/color appearance and absence of missing overlays or stale
frames. Menu state sampling and render callbacks cross threads; offline owner
tests do not establish all native transition ordering. Compare against the
disabled native-menu control before enabling this in a player package.

### Color-policy correction

The initial implementation forced sRGB in `InventorySwapchain::Prepare`. That
was inconsistent with the main swapchain when using its linear default or a
linear runtime fallback. The normal startup script sends `xr.srgb 1`, and
`VrMode.cpp` also enables that preference, so the usual startup already requests
sRGB. A preference is not the same as the actual runtime-supported selection.

The host now retains that actual selection and passes it explicitly to the
inventory owner. Raw resource copies preserve bytes; changing only the resource
interpretation can change displayed brightness. Matching the native full-menu
route removes that avoidable difference, without assuming that a texture named
UNORM contains linear light or changing the user's world-color policy. Final
color/alpha appearance still needs native capture and headset comparison.

## Still separate work

This does **not** yet show the gameplay world behind inventory. The modal panel
still replaces projection submission; retained world eyes are still invalidated
on transitions. The native paused-world render gate needs its own proof and
implementation before projection and inventory can be composed together.

The inventory remains a single shared image for both eyes. Its native mouse
parallax stays baked into that image. Using its eight measured UI depth planes
for a stereoscopic deep hologram requires a separate rendering design; this
swapchain owner provides the independent destination, not that stereo replay.
