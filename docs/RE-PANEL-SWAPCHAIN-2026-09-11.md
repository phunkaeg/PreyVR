# Panel swapchain separation: static analysis

Static only. No build, injection or live experiment performed for this note.
Target SHA256 `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`,
base `0x180000000`, confirmed current in Ghidra before any byte was read.

## The blocker, stated exactly

`XrCompositionLayerQuad panelLayer` and `XrCompositionLayerProjection layer`
address the **same images**. `panelLayer.subImage.swapchain=gHost.swapchain` with
`imageArrayIndex=0`, and `cylinderLayer.subImage=panelLayer.subImage`. That
swapchain is created once at session start with `arraySize=2`, sized to Prey's
backbuffer, usage `COLOR_ATTACHMENT|TRANSFER_DST`.

So while a panel is up the code cannot submit both, and does not try: the layer
list takes the panel in an `else` against the projection, and the backbuffer is
copied into *both* array slices rather than a stereo pair being built. Adding
the projection layer today would composite the flat inventory behind the curved
inventory, which is why this has to come first.

## The pattern already exists in this file

`hudSwapchain` is the same problem already solved, and nothing new needs
designing:

- created lazily from the captured texture's `D3D11_TEXTURE2D_DESC`, so format
  and size follow the source rather than being guessed;
- destroyed and rebuilt when that desc changes;
- `arraySize=1`, usage `COLOR_ATTACHMENT|SAMPLED`;
- images enumerated into `gHost.hudImages`, acquired/waited/copied/released per
  frame, with `RefuseHudLayer` on every failure path.

**Transparency is solved on that path too, and this is the finding I would most
want checked against Codex's:** `hudLayer.layerFlags` is already
`XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT`. The readiness doc's
"menu quad and cylinder have no source-alpha flag" is accurate, but it reads as
a missing capability when it is a missing flag on one of two otherwise identical
paths.

Better still, the capture side already produces an alpha channel:
`CaptureFlash` does `ClearRenderTargetView(capture.target, {0,0,0,0})` before the
movie draws. The private target therefore starts fully transparent. Whether the
PDA movie then paints an opaque backdrop over it is still open — that is the
readiness doc's point 1 and it is a question about the artwork, not about our
plumbing.

## Proposed shape

1. `gHost.panelSwapchain` + `panelImages` + `panelDesc`, created from
   `InventoryLayerTexture()`'s desc by the same lazy rebuild the HUD path uses.
2. Copy the capture in; point `panelLayer.subImage` and `cylinderLayer.subImage`
   at it.
3. Add the source-alpha blend flag to both.
4. Stop bypassing `SubmitStereoPair` while `panelActive`, so the projection
   carries a real per-eye pair instead of a doubled backbuffer.
5. Submit projection first, panel second.

Steps 1-3 are mechanical. Step 4 is the one with a behavioural risk: it changes
what the world looks like behind a menu, not just the inventory.

## What I could not settle, and why it does not block

**Does Prey render the world while the inventory is up?** `CSystem::Render`
(RVA `0xE0BA30`) does not always draw it. Decompiled:

- outer gate `system+0x9D7 == 0` and `system[0x156] != 0`;
- a virtual call, `system[0x156]->vtable+0x30`, returns flags. When `& 2` is
  clear it takes the ordinary path: three `GetIRenderer` calls, `SetViewport`,
  then `RenderWorld(0xF, camera, "CSystem::Render")`;
- when `& 2` is **set**, the world is rendered only if
  `gEnvLike+0x2AB == 0` **and** one of three floats at `+0x794`, `+0x7A4`,
  `+0x7B4` exceeds `0.05` in magnitude. Otherwise it calls
  `SetState(0x265,-1)` and draws a single full-screen 2D image at
  `0x44480000 x 0x44160000` — 800.0 by 600.0, CryEngine's virtual screen.

`param_1[5]+0x2AB` is exactly the byte the readiness doc named. I did not link a
PDA writer to it: the instruction search for stores to `+0x2AB` timed out across
86,439 functions, so I have **no** result there, not a negative one.

The design does not need the answer. Once the panel owns its images, whatever
the backbuffer holds becomes the projection, and the first run in a headset
distinguishes live world, frozen world and 800x600 backdrop by inspection. That
converts a static question that has resisted two sessions into an observation,
which is the reason to do the separation before the investigation rather than
after it.

## Ordering I would argue for

Separation first (1-3), submitted with the projection layer still suppressed, so
the only change is which images the panel reads. If the inventory still renders
correctly, the plumbing is proven in isolation. Then 4-5 as a second change,
where a regression can only be the world path.

Doing both at once means a wrong panel has two candidate causes.

An earlier draft cited today's black inventory as evidence for this. It was not:
that had one cause -- a capture shipped without anything consuming it -- and
the lesson it actually carries is the reverse: land the separation *with* its
consumer wired, never as a half-step.
