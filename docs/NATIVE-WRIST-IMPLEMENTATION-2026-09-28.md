# Native HUD wrist capture candidate

Implemented on `codex/vr-options`, following the [offline extraction](NATIVE-WRIST-HUD-2026-09-27.md).
This replaces the custom status-card rendering path. It is built and tested
offline; no game launch, injection, attachment or headset acceptance occurred.

## Presentation and controls

Enable **VR Options → Equipment → Wrist Status** (still off by default). Raise
the left palm and look at it. The candidate copies Prey's health, psi and suit
meters, shared backgrounds/line and status effects onto a transparent wrist
quad. The usual forward HUD remains unchanged, including its status meters.
The wrist is hidden in menus, during two-handed aiming, on invalid tracking and
when looking away. Size remains 80–140%; the quad is 18 × 12 cm at 100%.

The copy retains native transforms, masks, artwork and animation state. It also
retains native visibility: an intentionally hidden native meter stays hidden.
The authored HUD perspective is preserved, not converted into internal stereo
depth. This does not change inventory stereo, add hand occlusion or remove the
forward copy. A blank native status group produces transparent output.

## Capture and ownership

1. A fresh XR wrist-view request permits at most one extra DanielleHUD draw.
   The existing name/proxy/build checks identify the movie. Normal HUD drawing
   happens first, to its existing destination, with no sprite filter.
2. Inside the native Flash proxy's critical section and global Flash lock,
   resolve 20 excluded sibling sprites and three meter sprites using the native
   root `GetVariable`. Values and handles live only for this callback; no object
   pointer is retained across frames or movie reloads. Missing/aliased objects,
   unexpected types/vtables or mismatched owners refuse the wrist copy.
3. Repeat root `Display` once into a separate transparent texture and cloned
   depth/stencil. A thread-local filter skips only the 20 excluded sprites at
   their own `Display` entry. Selected widgets keep their internal draw order,
   masks and filters. No `Advance`, outer callback or player release is repeated.
4. At least one of the three meter Display entries must be observed during replay. This rejects
   an ancestor bitmap cache that bypasses the children and would otherwise copy
   unfiltered content. Requiring all three would reject legitimate hidden meters,
   such as locked psi. Observation proves traversal, not nonempty pixels. Missing
   traversal refuses that frame but allows another request when native UI changes.
5. Clear the filter, restore render targets and release every owned value. A
   faulting release is not retried, since it may already have decremented its
   reference. Native replay/ownership failure disarms wrist replay until the XR
   session restarts. The native HUD was already drawn without the filter.
6. Drain the capture once at the XR frame boundary, including skipped frames.
   Stale captures are not resubmitted. Native capture failures never redirect or
   suppress the normal HUD. Private resources are released at session teardown.

## Why visibility writes were rejected

The first candidate considered saving/restoring `_visible` through SetVariable.
Static inspection rejected that approach before any game run. Steam
`GFxSprite::SetVisible` (`0x18BF0C0`) changes the visibility bit, but under the
movie's optimization flag also changes optimized animation-playlist membership
and propagates state. Restoring the visible Boolean alone is not a proof of
restoring all those side effects. The final implementation never calls this
setter or changes native display properties.

## Steam contracts

Target SHA-256: `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
All addresses below are RVAs in this x64 module, not EGS offsets.

| Contract | Steam evidence |
|---|---|
| Root GetVariable | Root vtable `0x1EB4B60`, slot `+0x88` → `0x18ABE10`; ABI `(root, outValue, path)` |
| Value ownership | `0x183F680` produces type `0x48` for a retained display handle; type at `+8`, payload at `+0x10`, interface at `+0`; `0x183BDE0` releases it |
| Handle → sprite | `0x1894A30` calls constructor `0x1894530`, which stores the whole character pointer at handle `+8`; null/detached handles are refused |
| Concrete sprite | Vtable `0x1EB5700`, Display slot `+0xE8` → `0x18BABA0` (799 bytes) |
| Owner | Sprite vtable `+0xA8` → `0x18B8ED0`: `mov rax,[rcx+118h]; ret`; owner must equal the current movie root |
| Draw consumer | Sprite Display reaches display-list routine `0x189E270`; that routine invokes child slot `+0xE8` and preserves native mask handling |
| Unsafe alternative | Sprite SetVisible slot `+0x198` → `0x18BF0C0`; changes more than visibility |

EGS symbols supplied names/search leads. Ghidra checked the Steam bodies, concrete
vtable entries and handle constructor. Runtime installation checks nine complete
body/accessor hashes in addition to the existing supported-module gate; the
offline verifier checks the exact DLL SHA, vtables and mutation refusals.
Excluded movie objects are named non-mask siblings in the inspected patched
asset. Arbitrarily modified HUD assets are outside the verified asset coverage.

## GPU fitting and colour

The extra draw preserves its native destination dimensions. Two compute passes
find nontransparent bounds and fit them, with padding and preserved aspect, into
a 768 × 512 transparent output. Bounds and fitting use the same GPU capture;
there is no CPU readback or previous-frame crop. Native HUD scale and resolution
therefore do not require a fixed pixel rectangle. Bounds can change when native
content opens or closes; perceived size stability remains a headset check.

Input copies use UNORM views to preserve encoded bytes. BGRA inputs are explicitly
read as colour channels and written to RGBA; no additional gamma conversion is
applied. The XR swapchain inherits the main swapchain's linear/sRGB interpretation.
Premultiplied alpha is retained and submitted with source-alpha blending. Compute
shader, class instances, used SRV/UAV slots and predication are restored.

The existing `InventorySwapchain` owner handles bounded acquire/wait/release.
The prototype used a 400-pixel-high texture, below that owner's 480-pixel minimum;
the new 512-pixel output satisfies it. Duplicate native-stat polling and the GDI
upload were removed from the wrist path. The old painter remains only as a
historical source/test fixture.

## Verification and next live check

Release build succeeds. The offline CTest suite passes 50/50, including:

- Partial handle-resolution failure at every slot, duplicate references,
  release failures, exact exclusions and positive meter-traversal control.
- Real D3D11 WARP pixels: all four RGBA/BGRA linear/sRGB input formats, exact
  centre colour/alpha, transparent background, centred aspect-preserving fit,
  edge-touching content, changed input resolution and empty-frame clearing.
- Compute bindings/predication restored; source-device mismatch refused;
  resource recreation across device changes. D3D11 debug messages are checked
  when the debug runtime is available.
- Existing inventory swapchain, options, input, stereo and fail-closed tests.

Reproduce without launching Prey:

```powershell
python -E -B tools/re/verify_native_wrist.py
cmake --build build/integration --config Release --target preyvr preyvr_native_wrist_tests
ctest --test-dir build/integration -C Release --output-on-failure
```

`hud.layer` reports `nativeWrist={captured=… refused=… fault=…}`; `vr.options`
reports `wristLayerFrames`. Captures and submitted layers are not visual proof.
The old `wristSamples` counter is no longer used by the native-pixel path.

When game runs are authorized, first check wrist and forward HUD simultaneously
at full/low health, damage/healing, psi unavailable/spending/recovery and suit
damage. Confirm no reticle/pickup/climb leakage, no stuck animations after
lowering the wrist, menu/save/load transitions and disabled-feature behavior.
Then check readability, apparent size stability, colour and frame cost in the
headset. The nine native body hashes and WARP tests do not establish these live
results. No installed DLL or release package was replaced by this work.
