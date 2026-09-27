# Native HUD status on the wrist

## Decision

Use Prey's own health, psi and suit presentation for the wrist display. The
GDI card in `WristPanel.cpp` is a prototype, not the intended final artwork.
Keep the existing wrist pose, gaze/visibility gating, size preference and
swapchain ownership; replace the source of the pixels once the native widget
boundary is established. Holsters are independent of this change.

Prefer rendering the original status subtree to a transparent target. This
preserves its icons, animations and additional state rather than trying to
recreate the movie from the five numbers currently sampled for the prototype.
Do not substitute a fixed rectangle cut from the complete HUD: reticles and
prompts can move, and native layout/animation can move the status graphics.

## What is established

Static target: Steam x64 `PreyDll.dll`, image base `0x180000000`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.

The existing body-equipment evidence verifies native health and armor update
producers. This follow-up also inspected `/Prey/PreyDll.dll` through Ghidra:

| Producer RVA | Movie dispatch | Observation |
| --- | --- | --- |
| `0x158CAB0` | `healthUpdate` | Existing evidence includes current/max health plus additional presentation arguments. |
| `0x13611D0` | `armorUpdate` | Existing evidence computes suit integrity from damage and maximum. |
| `0x15AD7C0` | `psiUpdate` | Gets the HUD through `0x1665780`, then invokes its virtual `+0x210`. Four arguments are assembled: rounded fields at component `+0x400`, `+0x404`, a caller integer, and rounded `+0x408`. The meaning of `+0x404` is not established here. |

These are UI function names, **not display-object paths**. They show why a card
with only current and maximum numbers cannot be assumed to reproduce the full
native presentation. No new native call has been added based on this inspection.

`HudLayer.cpp` already captures the complete DanielleHUD draw into a transparent
texture. Historical source-texture evidence is documented in
[the interface investigation](RE-VR-INTERFACE-2026-09-10.md). It does not establish
an independent status-widget render target or status-only display subtree.

## Remaining boundary

Recover the movie hierarchy and follow the three update functions to their
display objects. Determine whether status is one subtree, several siblings, or
mixed with unrelated elements. Establish local bounds, masks, filters, animation
ownership, authored visibility and any 3D transforms before choosing capture.

The installed main-game PAK directories were checked offline. Python's ZIP
reader rejects their central directories, including GameData, Scripts and the
Precache patch. A ZIP-signature check alone returns true for some of these files;
that is not successful extraction. Scoped searches found no extracted HUD movie
in the local project or the supplied sibling repository. This is a coverage
limit, not proof that the assets cannot be recovered.

Chairloader's [ExtractionStage.cpp](https://github.com/thelivingdiamond/Chairloader/blob/main/Src/Preditor/Launcher/ExtractionStage.cpp)
uses the initialized engine's CryPak archive service, including optimized reads
for encrypted PAKs. It is not a standalone ZIP decoder. It was inspected, not
executed: this task retains the user's static/code-only restriction.

## Implementation contract once the boundary is known

1. Resolve the status objects by verified movie paths, scoped to the current
   movie generation. Verify the target build's display-object API and receiver.
2. Render status in its authored order, including masks and filters, into a
   private transparent target at the native owning render seam. Advance the
   movie only once. Do not repeat arbitrary queued Flash callbacks or releases.
3. Preserve fresh authored transforms rather than compounding wrist transforms
   or fighting animation. Restore every temporary change before returning.
4. Publish status pixels with their capture generation/frame, dimensions and
   format. Reuse wrist pose/gaze logic and preserve premultiplied alpha and the
   source's colour-space interpretation; the existing opaque GDI upload is not
   the native-pixel transfer contract.
5. Keep the normal status visible on capture/consumer failure. Removing its
   forward-facing copy requires a successful consumer handshake and an explicit
   policy for when the wrist is lowered; it must not disappear merely because
   the preference is enabled.
6. Check status against the original at full/low health, damage/healing, psi
   spending/recovery and suit damage, including native warnings and unavailable
   psi. Check menus, save/load, reference-space changes and unsupported builds.

Initial implementation should preserve the forward-facing native status while
validating the wrist copy. A later option can move it exclusively to the wrist
after visibility and failure recovery have been verified.

## Current outcome

No native-widget retargeting is implemented by this follow-up. The optional,
default-off prototype card is unchanged. The native HUD remains intact. No game
was launched, attached or modified, and there is no new headset acceptance claim.
