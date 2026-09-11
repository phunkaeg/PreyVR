# Inventory over the gameplay world: readiness

Static assessment; no build, injection or live experiment performed.

Later implementation: [inventory swapchain separation](INVENTORY-SWAPCHAIN-STATIC-2026-09-11.md)
adds the opt-in capture consumer and offline verification. The assessment below
records the earlier baseline; paused-world rendering remains open.

## Already available

- Inventory identity and native rendering callback: prior in-game receipt
  `evidence/vr-interface-2026-09-09/flash-bindings-inventory.json` observes
  DaniellePDA player `0x28e5fc56430`, render proxy `player+8`, callback RVA
  `0xE8C5E0`, 23 callbacks on the render thread. It also records internal filter
  targets, which must remain separate from the final destination.
- HudLayer.cpp already redirects one identified movie's original callback into
  a private transparent D3D11 target, preserves stencil, and restores targets.
  It only identifies DanielleHUD and requires gameplay input to be allowed;
  consequently it does not currently extract inventory. Generalize by purpose,
  keeping movie identity, target lifetime and capture freshness separate.
- Existing fitted quad/cylinder, anchor/recenter, pointer mapping, native click,
  inventory drag and cancellation. Input stays modal even if the world is visible.
- Existing stereo projection submission and alpha-blended HUD layer.

## Confirmed current limitations

XrSessionHost.cpp invalidates retained eye images on panel transitions, bypasses
SubmitStereoPair while panelActive, copies the complete backbuffer to its panel
swapchain, and submits the panel INSTEAD OF the projection layer. Menu quad and
cylinder have no source-alpha flag. This explains black surroundings without
proving anything about the native inventory's background artwork.

For a floating screen with the world visible around its border, the screen can
retain its dark backing. Transparent cutout artwork is a separate enhancement;
it is not a prerequisite for showing the room around the screen.

## Remaining proof and implementation

1. Capture DaniellePDA (and required modal child dialogs) separately from the
   scene. Use the existing once-only callback redirection; never replay Flash
   update/render or black-key the completed backbuffer. An inventory-only alpha
   capture has not been established. SetBackgroundAlpha exists in donor headers
   but would not necessarily remove opaque shapes drawn by the movie.
2. Verify the native world render path during inventory. Steam Open RVA 0x163A580
   calls 0x163CE30(this,1,0), then looks up and shows DaniellePDA. The latter
   forwards (1,1,0,0) through the framework receiver's +0x68; the EGS symbol
   counterpart identifies the intended operation as PauseGame. Its concrete
   Steam virtual callee was not resolved here, so this is not a shipping ABI.
3. Steam CSystem::Render RVA 0xE0BA30 contains a world-render gate at
   `*(system+0x28)+0x2AB`, as well as process/ignore-update gates. The EGS source
   names the corresponding member m_isFMVPlaying and has PDA transition writers.
   This assessment has NOT linked a Steam PDA writer to that exact byte or proved
   which gates are active in the live inventory. Do not globally clear a movie
   flag or unpause gameplay to work around this.
4. Submit world projection first and inventory panel afterward, on independent
   swapchains. Preserve actual rendered eye pose/FOV metadata, fresh head tracking,
   FIFO draining and clean enter/exit behavior. Merely retaining an old eye pair
   does not provide a world that correctly responds to positional head movement.
5. Keep opaque full-screen fallback for front-end/loading screens, absent world
   or failed capture. Distinguish inventory modality from presentation selection.

Decision: enough infrastructure and a known inventory render seam to start the
implementation, but not enough evidence to promise a finished, head-tracked
world behind paused inventory. First discriminating test: redirect PDA once,
capture its alpha and the remaining scene separately, and observe world draw /
camera updates with inventory closed and open. Keep the previous crash as an
independent unresolved stability issue.

Static Steam decompiles: `evidence/inventory-hologram-2026-09-11/native-static.json`.
Target SHA256: 7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7.
The project graph was queried but returned unrelated target-symbol matches;
current source, prior callback receipts and hashed Steam bytes were used instead.
