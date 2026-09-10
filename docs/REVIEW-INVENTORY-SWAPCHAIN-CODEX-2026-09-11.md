# Review: Codex's inventory swapchain separation

Read-only review of uncommitted work in the tree as of this note:
`src/dll/InventorySwapchain.{h,cpp}` (new) and edits to `HudLayer.{h,cpp}`,
`HudBridge.{h,cpp}`, `XrSessionHost.cpp`. Nothing here was built or run; the
tree cannot link yet (see defect 2). My own independent notes are in
`RE-PANEL-SWAPCHAIN-2026-09-11.md`; this file is the comparison.

## Verdict

Codex's is the better piece of work. It implements steps 1-3 of the shape my
note proposed — separate `arraySize=1` swapchain, capture copied in, panel and
cylinder pointed at it, source-alpha flag set — lands the consumer *with* the
capture as one change, and leaves the projection layer suppressed. That is the
ordering I argued for, and it is the corrected lesson from today's black
inventory, applied.

It also contains one mechanism my note did not propose and should have.

## The consumer lease (their idea, and the right one)

`CaptureFlash` refuses to redirect the PDA unless `SetInventoryConsumerReady`
has published a fresh lease. `ServiceXrFrame` revokes the lease at its top on
every call, takes the capture at most once per capture stamp, and re-grants
the lease only after a complete `xrEndFrame` that returned `XR_SUCCESS` with
the swapchain prepared. If the XR loop stalls, fails, or is not running, the
lease expires and the PDA draws natively.

This makes the failure I shipped this morning — a redirected draw with nothing
consuming it — structurally impossible rather than merely guarded by a flag.
The bootstrap is clean: the first inventory frame prepares the swapchain from
the backbuffer desc, submits through the legacy path, and only then grants the
lease, so the handshake never shows an empty panel. On upload failure the
legacy panel is fed the captured texture (`panelSource = inventoryTexture`) and
capture is disarmed, so even that path does not go black.

`captures[i].stamp = 0` on a matched-but-refused callback is a small, correct
touch: when a dialog opens over the PDA, `HudInventoryIsOpen()` goes false, the
stale PDA image is invalidated, and the legacy path shows PDA+dialog natively.
No frame of a stale inventory under a live dialog.

## Defect 1 — hard-coded sRGB against a linear main swapchain

`InventorySwapchain::Prepare` always creates `*_UNORM_SRGB`, with the comment
"Raw copied panel pixels follow the existing menu's sRGB interpretation."

The existing menu's interpretation is not a constant. `gHost.swapchain` is
chosen by `SelectFormat(formats, kR8G8B8A8Unorm, gPreferSrgb)` and
`gPreferSrgb` **defaults to `false`** (`XrSessionHost.cpp:193`), toggled at
runtime by `xr.srgb`. In the default configuration the main swapchain is
linear and the inventory swapchain is sRGB: identical bytes, decoded
differently by the runtime.

Consequences, in order of when they will be noticed:

1. A gamma step at the handshake frame — the panel is shown once through the
   legacy path (linear) and thereafter through the inventory swapchain (sRGB).
2. The inventory panel and the pause menu (still legacy) will not match.
3. When the projection layer is added behind the inventory, the world and the
   inventory will have different gamma. In a headset that reads as "the
   inventory looks wrong", with nothing pointing at a format flag.

Whether *both* should be sRGB is a real and separate question — display-referred
backbuffer bytes submitted as linear is the classic washed-out mistake, and the
toggle presumably exists because of it — but that is a pre-existing decision
with its own history. This change should not quietly disagree with it.

**Fix:** have `Prepare` take the sRGB-ness of the main swapchain's selected
format and match it, so the two stay consistent whichever way `xr.srgb` is set.
One parameter.

For fairness: my note's "follow the HUD path" template uses the capture's own
`desc.Format`, which matches the linear default only because Prey's backbuffer
happens to be linear — consistent by accident, and it would diverge the moment
`xr.srgb 1` is set. Neither analysis wrote "match the main swapchain", which is
the actual requirement.

## Defect 2 — the new source is not registered

`CMakeLists.txt` does not list `src/dll/InventorySwapchain.cpp`. `HudLayer.cpp`
now calls `InventoryTextureCompatible` and `XrSessionHost.cpp` constructs the
class, so the tree will not link until it is added. Most likely simply not done
yet; noted because an unregistered source is exactly the kind of dead path that
cost three live tests today.

## Behavioural note, not a defect

`HudInventoryIsOpen()` is `pdaVisible && !otherVisible`, and `kMenuElements`
includes `DanielleInventoryExternal`. If the external-container view is visible
alongside the PDA during normal use, capture disarms and that view always takes
the legacy path. Probably the intended fail-safe; worth knowing when the
separated panel appears to "stop working" in a container.

## What each analysis contributed that the other did not

Mine: the `CSystem::Render` finding (`0xE0BA30`) that the world is not
unconditionally drawn — bit 1 of a virtual-call result, `+0x2AB`, three floats
against 0.05, else a single 800x600 2D image — and the argument that this is a
measurement to take *after* separation, not a prerequisite. Also that the
source-alpha flag is inert until a projection layer sits behind the panel, so
transparency cannot be judged from this step.

Codex's: the lease, the bootstrap handshake, the single-consumption stamp, the
fallback that keeps the legacy panel populated, the 50 ms wait bound with
timeout treated as a session fault, and injectable XR entry points for offline
tests. All of it is code rather than a plan.

## Recommended next

Fix defect 1, register the source, build, run the suite, package. Then the
first headset run is the discriminating test for the whole plan: with
`ui.inventory 1`, open the inventory and confirm it still renders (plumbing),
then read `hud.layer` and the log for the `inventory_swapchain_upload` refusal
that must not appear. Only after that, the projection layer — where whatever the
backbuffer holds during the inventory becomes an observation instead of a
static question.
