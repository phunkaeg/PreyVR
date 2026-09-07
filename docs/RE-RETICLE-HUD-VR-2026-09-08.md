# Reticle retargeting and VR HUD presentation

Date: 2026-09-08. Scope: source review, read-only Steam binary analysis,
fleet prior art and OpenXR specification research. No game launch, injection,
process attach, capture, command-channel writes or headset test. No runtime
code changed. Final source check: repository HEAD `514770c`, including the
concurrent body-yaw correction `0235057`; that correction does not change the
reticle/UI presentation paths described here.

## Recommended design

Preserve Prey's native reticle art, animations and targeting states. Separate
their presentation into three groups:

| Content | VR placement | Ownership |
| --- | --- | --- |
| Weapon aiming reticle, spread and hit feedback | Binocular world point selected by the authoritative weapon aim solution | Weapon/muzzle aim and native weapon state |
| Scanner locks, enemy/objective markers, interaction markers | Their actual target positions, with explicit offscreen handling | Native target selection; do not blindly redirect every marker to the gun |
| Health, resources, notifications and full menus | Readable flat or curved panel | Native UI logic, independent panel pose and input mapping |

The strongest first milestone is **native reticle movement plus a transparent
HUD-only texture**. Curvature is a presentation step after that extraction works.
Aiming reticles should not be flattened onto the status panel: a symbol at the
panel's depth does not coincide binocularly with an object at another depth.

## 1. Current reticle implementation is only a starting point

`src/dll/AimTakeover.cpp:199` obtains a controller aim ray. It writes cached
direction and, optionally, a controller-derived origin, then calls
`WriteReticleScreenPosition(player, ray->direction)` at line 242.
This is not a measured barrel orientation from the resulting weapon model.

`src/dll/ReticleFollow.cpp` projects **direction only** through the current
global camera and writes normalized XY at whole `ArkPlayer+0x17EC/+0x17F0`.
It has no muzzle position, hit distance or per-eye world-point projection.
The gate is initially disabled. Behind-camera/offscreen rejection leaves the
previous position in place; it does not hide the indicator.

The comment that Prey draws directly from the cached screen position is not
sufficient evidence of visual movement. The native paths below explicitly
dispatch UI calls. A memory write alone does not establish that those calls
run afterward, or that the movie reads the field independently. The searched
literal references are not an exhaustive proof that no other route exists.

## 2. New Steam-native UI anchors

Target: `PreyDll.dll`, x64 Windows ABI, image base `0x180000000`.
Disk SHA-256 rechecked this session:
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Ghidra program explicitly selected as `/Prey/PreyDll.dll`.
All addresses in this table are **RVAs**, not EGS header addresses.

| RVA | Static observation | Consequence |
| --- | --- | --- |
| `0x1665780` | Looks up `DanielleHUD` through singleton at RVA `0x224DAD0`, virtual offset `+0x60` | Named native HUD element accessor |
| `0x16657A0` | Same lookup for `DanielleMarkers` | Separate marker element already exists |
| `0x1583A30` | Resets whole-player `+0x17EC` to 0.5 and `+0x17F0` from configuration; calls HUD functions `reticleXOffset` and `reticleYOffset` | Normal reticle placement has explicit UI notification |
| `0x1582920` | Large player reset path repeats the same field writes and UI calls | Independent confirmation of field-to-UI relationship |
| `0x15ABC40` | Examination receiver state `+0x98==1`; updates/clamps receiver `+0x60/+0x64`, calls HUD `reticlePosition(x,y)` | A native two-coordinate movement route exists, but observed use is examination mode |
| `0x15AB280` | Examination transition copies player `+0x17EC/+0x17F0` to receiver `+0x60/+0x64`, dispatches `reticlePosition` on one transition | Examination and normal reticle coordinates have distinct ownership |
| `0x1667D50` | Looks up `DanielleHUD`; sends `reticleDisplay`, `interactIconDisplay`, `reticleDispersion`; also queries native cached aim for HUD target state | Preserve art/state selection rather than replace with a generic dot |
| `0x15D9440` | Calls marker accessor then dispatches `scanningUpdateTargetReticle` | Scanner indicator belongs to the marker route, not simply the ordinary HUD reticle |
| `0x11797C0` | Builds two float UI arguments, invokes returned element's virtual `+0x210` | Candidate native two-float dispatch helper; complete concrete callee still to resolve |
| `0x118C970` | Corresponding one-float argument helper, same virtual dispatch | Used for normal X/Y offset calls |

Evidence and selected decompilations are preserved in
[`evidence/reticle-hud-static-2026-09-08.json`](evidence/reticle-hud-static-2026-09-08.json).
String-xref seeds: VA `181E59E48` (`reticlePosition`), `181E57428/438`
(`reticleXOffset/YOffset`), `181E69AC0` (`reticleDisplay`), `181E5DF18`
(`scanningUpdateTargetReticle`).

**ABI warning backed by instructions:** `0x11797C5/CB` save XMM3/XMM2.
The two-coordinate helper's argument shape is element pointer in RCX, function
name in RDX, float X in XMM2 and float Y in XMM3. Its decompiled `undefined4`
parameters must not become integer R8D/R9D arguments. At `0x1583A6B/82`,
the one-float caller likewise loads XMM2. Return semantics, element lifetime,
thread affinity, and concrete virtual `+0x210` callee are not yet closed;
these anchors are **not authorization to ship a guessed native call**.

The generated Chairloader headers supply names such as `CArkUIHUD`,
`IUIElement::CallFunction`, `GetHUDUIElement`, and `GetMarkerUIElement`.
They are useful vocabulary; this report's Steam addresses came from Steam
string references and instructions, not an assumed EGS-to-Steam delta.

## 3. Align reticle, rendered barrel and firing semantics

Establish one immutable per-frame weapon aim description: equipment generation,
weapon/entity/character identity, tracking sequence, reference transform,
muzzle origin, calibrated barrel direction, and validity. All consumers should
use the same sample or explicitly account for their image/sample age.

For a rigid weapon, compose the tracked grip, calibrated grip-to-model
transform and authored model-to-muzzle/barrel transform. Validate this against
the final weapon pose after rig/IK ownership has settled. A default attachment
transform or arbitrary model +Y axis is not automatically the barrel axis.
Keep weapon-change invalidation and unsupported/fallback weapons explicit.

Existing `WeaponAttachment.cpp` already observes native firing positions and
reports owner freshness, generation, pose sequence, camera fallback and
muzzle-to-aim/grip gaps. That is a useful positive control for origin alignment,
but it does not by itself measure barrel orientation.

The previous R011-R015 / H-005C evidence establishes native cached aim consumers:
`0x1585320` produces cached ray, `0x157CBB0` returns it through player interface
`+0x40`, and `0x1694890` queries a weapon firing target. GLOO subsequently aims
from native firing position toward that target. Therefore aligning a screen
symbol to the controller without reconciling muzzle and query origin can still
leave visible barrel, reticle and projectile disagreeing at close range.

Prefer reusing an observed native targeting result where its semantics match
the weapon. Otherwise define a deliberate muzzle-ray query; do not silently
change all native filtering by substituting a generic physics ray. An aiming
point is not an exact impact prediction for every projectile or spread weapon.

Project a finite target point separately for each eye using that image's actual
pose/FOV. For no hit, use an explicit finite far aiming point; distinguish it
from a confirmed impact marker. Preserve native spread/lock/hit state. Hide
or deliberately replace an offscreen aiming symbol rather than leaving a stale
one over an unrelated object. The existing alternating eye-image cadence
makes sample/frame correlation particularly important.

## 4. HUD extraction is the main integration question

`XrSessionHost.cpp:978-988` submits only a projection layer. Eye images derive
from the game's swapchain/backbuffer path, with the native UI already composed.
A second UI layer requires a HUD-only texture **and removal of the matching
baked-in HUD from VR scene images**. Curving a full game image is not extraction.

The preferred investigation seam is native named-element display/rendering:
identify `DanielleHUD` and `DanielleMarkers`, their concrete element/movie
objects, and the render-thread playback which draws them. The headers expose
`IUIElement::Render/RenderLockless/GetFlashPlayer`, viewport access and UI flags;
`IFlashPlayer` distinguishes `Advance`, `Render`, and render-proxy playback.
This supports a route, not proof that a drop-in offscreen render-target API
has been found in this Steam build.

Redirect the existing display work into transparent private targets at the
correct thread boundary; preserve native UI updates/Advance once per game
frame. Reticle subelements may need separate routing within `DanielleHUD`.
Markers, world-material screens, subtitles, videos and full menus require
classification rather than a blanket 'all Flash is HUD' rule.

If the named-element route cannot isolate draws cleanly, a bounded D3D11 draw
classifier is a fallback. It must prove shader, texture, target, stencil/mask,
blend and frame identity. Preserve state and resource lifetime; a matching
shader alone can capture unrelated UI or world content. Alpha/color-space
handling and restoring stencil, viewport/scissor and render targets matter.

Do not replay CSystem::Render or RenderWorld to obtain UI. Existing F-014/F-015
receipts document UI corruption or deadlock from second-world-render experiments.
Extracting UI must respect the current frame/thread contract.

The shipped GameData/Scripts PAKs were not readable as ordinary ZIPs in this
session (consistent with H-018). UI XML/ActionScript movie-clip paths therefore
remain unverified. This is an asset-access gap, not evidence the UI cannot be
separated. Native function descriptors/movie-clip descriptors offer another
route to establish the exact exported functions and element paths.

## 5. Flat and curved presentation

Start with a core OpenXR quad to prove texture content, alpha and freshness.
Then use `XR_KHR_composition_layer_cylinder` when the selected runtime advertises
it and the extension was enabled at instance creation. Keep the quad fallback;
a tessellated curved surface rendered by the mod is a more involved alternative.
A core quad has no curvature parameter. [Quad specification](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerQuad.html)
and [cylinder specification](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerCylinderKHR.html).

Initial **tuning proposal**, not headset acceptance: distance/radius 1.5-2 m,
horizontal span around 55-65 degrees, preserved UI aspect ratio and adjustable
scale/vertical placement. At radius 2 m, a 60-degree, 16:9 cylinder section has
arc width 2.094 m and height 1.178 m. The cylinder pose describes its viewing
centre; do not reuse a flat panel's centre-at-minus-distance pose blindly.

Offer stable menu placement and a gameplay panel that follows substantial
orientation changes with a dead zone. Compare that with a head-attached visor
option in headset tests; avoid unnecessary pitch/roll chasing and delayed
head-follow oscillation. The current host creates LOCAL space; a view-attached
layer needs a VIEW space or an explicitly updated local-space pose.

Distance is not sufficient for comfort: check angular text size, contrast,
legibility toward panel edges, centre-view obstruction and reading stability.
Virtual curvature does not change the headset's optical focal distance.
Microsoft's immersive-display guidance explains the device-dependent focal
distance and advises avoiding excessively near content; the proposed distance
still needs testing on the actual headset. [Comfort guidance](https://learn.microsoft.com/en-us/windows/mixed-reality/design/comfort).

Use a dedicated alpha-capable XR swapchain and source-alpha composition.
Choose premultiplied/straight alpha flags to match the actual captured pixels;
test text edges on bright and dark scenes. Compositor overlay order does not
automatically provide scene-depth occlusion for markers. [Layer flags](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerFlagBits.html).

Controller pointer interaction must intersect the displayed surface: plane
intersection for a quad, cylinder intersection and angle/height-to-UV conversion
for a cylinder. Map through viewport scaling and letterboxing to native UI
coordinates. Preserve native menu focus and press/release semantics. A flat
ray-plane mapping used with a curved panel will miss buttons toward its edges.

## 6. Reusable fleet work

`D:/Dev Debug/BioshockVR/docs/phase2-ui.md` documents a transparent private HUD
target and dedicated OpenXR quad in `0.3.144-hudquad`, with separate crosshair
attribution from captures. It explicitly leaves live alpha/menu coverage open.
Reuse the composition/resource lifecycle and validation approach, not its
game-specific shader classifier or capture-local resource IDs. It is supporting
engineering prior art, not Prey validation or a completed headset receipt.

Project graph query covered ReticleFollow but little native UI. Fleet graph
resolved relevant document names; a detailed explain stalled and was stopped.
Scoped source/doc searches supplied the evidence. Ghidra script execution was
disabled; ordinary read-only xref HTTP operations were used after inspecting
the running server's schema. No server policy or game state was changed.

## 7. Concrete next proofs, in order

1. Resolve the actual `DanielleHUD` element vtable and `+0x210` implementation;
   recover function descriptors for reticle offsets/position and the relevant
   movie-clip hierarchy. Prove normal aiming, examination and scanning scopes.
2. Bounded visual proof, when runtime is available to its owner: move only the
   native reticle art while holding the scene/weapon fixed. Observe both a changed
   position and return to baseline; a successful dispatch is not visual acceptance.
3. Demonstrate muzzle/barrel/target agreement with near and far targets, lateral
   head movement, multiple weapon types, weapon changes and camera fallback.
4. Isolate HUD into a transparent texture while scene eyes contain no duplicate.
   Positive controls: visible meter/text and menu; negative controls: world,
   weapon, material screens, hidden/closed menu and stale previous-frame UI.
5. Submit flat panel, verify acquire/wait/release, alpha, formats, session loss,
   menu input and freshness. Then enable cylinder with runtime capability checks.
6. Headset acceptance: stereo reticle convergence, no vertical split, no stale
   marker, readable panel, stable placement, correct edge clicking and comfort.

Static fixtures can check barrel/grip composition, owner invalidation,
per-eye point projection and surface-to-UI mapping. XR-sim/xr-tape can verify
layer type/pose/size/alpha flags/submission timing. Neither proves the extracted
texture contains the intended HUD, nor that Prey accepted input or looks right
in the headset; correlate with graphics and visual observations.
