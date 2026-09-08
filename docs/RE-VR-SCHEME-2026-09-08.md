# The VR scheme: what was built 2026-09-08, and what was not

Six lanes were asked for. Five are built, one is static only, and two of the
open questions have since been answered by measurement rather than left for the
headset: the menu is confirmed present in the submitted image, and the render
resolution now demonstrably follows the launch arguments.

**What still needs a headset is everything about how it looks and feels** --
whether the crosshair visibly moves, whether shots land where it points, whether
the controls read well, and what the higher resolution costs in framerate.

## What is in the build

### Motion controls — built

`move.all 1`, or `Invoke-PreyVRStartup.ps1 -Controls`.

| control | posts | why that |
|---|---|---|
| left stick | `xi_thumblx` / `xi_thumbly` | R-089 confirmed these reach the player's analog handlers live |
| right stick X | `xi_thumbrx` | Prey's **own turn axis**, so mesh, capsule, aim origin and movement direction all follow together |
| right trigger | `xi_triggerr` | the analog axis the pad produces, so whatever the game binds to it fires |

Turning through the engine's own heading channel is the playbook's rule, and the
reason is that a character's facing is consumed by four systems that must not
disagree; writing any one of them alone silently desynchronises the other three.

The deadzone **rescales** past its edge rather than clipping, so leaving it is
gentle rather than a step to full rate, and a return to centre **always** posts
zero -- the engine keeps turning on the last value it was told until
contradicted.

**The right-stick and trigger key ids are inferred**, from the PDB enum rather
than this build. The enum block is corroborated at three points that *are*
confirmed here (`0x20A`, `0x210`, `0x211`) and all four symbol names exist in the
binary at the expected adjacency. It fails closed: `PostInputEvent` rejects an
unknown key id, so a wrong value is refused and counted rather than doing
nothing quietly.

A queue defect was found and fixed on the way: the input queue drains one event
per frame, deliberately, so a menu press and its release cannot collapse -- but a
two-axis stick produces two per frame, so the ring filled and dropped everything
(`movePosted=0` against `moveDropped=109` in 4 s). Axis events now post directly,
which is safe because the lane already runs on the drain thread. Collapsing is
correct for an axis and wrong for a button, which is why they are separate calls
rather than a policy flag.

### The reconciliation — built

`preyvr::aim::Sample` is one immutable per-frame answer to where the weapon aims,
carrying its own provenance. Three lanes previously answered that question from
three different samples, so the rendered barrel, the ray gameplay uses and the
crosshair could disagree -- and each disagreement looked like a separate bug.

`Usable()` checks all three invalidations together (equip generation, reference
generation, age) so a caller cannot forget one. `Confidence` distinguishes *an
origin and a pointing axis* from *a calibrated barrel*, because an attachment
default is not a barrel axis and the type should say so rather than let a
consumer assume.

**Barrel calibration** closes the last gap. Prey's projectile already leaves the
weapon's authored muzzle helper while the reticle ray starts at the eye, so a
shot flies from the muzzle toward wherever an eye ray landed: the two agree at
exactly one distance and diverge everywhere else. `aim.calibratebarrel` derives
the muzzle's offset from the tracked grip using a real firing-position sample the
passive observer already captures -- the engine's own muzzle and our grip at one
instant, which is the only evidence available and beats any authored guess.
`aim.origin 2` then starts the ray there.

It refuses rather than guesses: a sample that used the native camera fallback is
not the muzzle (a blocked safety ray), a sample from another weapon is rejected
by generation, and an offset over a metre is a bad sample rather than a long
weapon. The fallback to the hand is **counted**, because silently reverting to a
less accurate origin is how a lane looks fine and aims wrong.

### Reticle takeover — built

`aim.reticle 1`. Prey's own crosshair follows the controller, projected through
the live view's actual asymmetric tangents.

It no longer goes stale off screen. It used to return early, leaving the
crosshair over whatever it happened to be over when the aim left the view -- an
indicator that is confidently wrong, which is worse than one obviously at a
limit. It now clamps to the edge it left by.

**One screen position exists, not two.** Prey stores a single cached reticle
position, so this cannot be per eye, and a symbol at the wrong depth will not
fuse binocularly with the thing it is over. `preyvr::aim::ProjectToScreen` is
per-eye and tested, ready for a mod-drawn reticle; using it needs the HUD lane.

### Resolution — measured, and the fix works

**Launching with `+r_Width 2688 +r_Height 2880` gives a backbuffer of exactly
that size.** Confirmed three ways: the engine's own view camera reports
`res=2688x2880`, `xr.resolution` reports `backbuffer=2688x2880 sizeMismatch=0`,
and the capture file is 30,965,808 bytes, which is 2688 x 2880 x 4 plus the
48-byte header, to the byte. The game presented 339 frames at that size.

`Invoke-PreyVRLaunch.ps1 -RenderWidth 2688 -RenderHeight 2880` is the route.
**Startup arguments, not mid-session cvars** -- the launch route is what was
measured, and a mid-session resize after the swapchain is latched is the hazard
the submit guard exists for. Setting one dimension without the other is refused,
because a half-set pair silently changes the aspect the projection is built from.

The 0.933:1 aspect does **not** narrow the headset's field of view: the camera
edit hook overwrites `fov` and `projectionRatio` from the runtime's own tangents,
so the stereo projection comes from the headset rather than the window shape.

Two things it does not fix. The **menu letterboxes** into a 16:9 band. And the
**cost is unmeasured** -- 7.74 Mpixels against 3.69, so 2.1x, on top of
alternating-eye stereo already doubling the frames.

### The deficit this replaced

**Measured: the headset receives 48% of the pixels its runtime asks for.**
2688x2880 wanted per eye, 2560x1440 supplied. Width is 95%; **height is exactly
half**, so the compositor upscales 2x vertically. Not a uniform scale factor and
not a DPI question: the runtime wants each eye taller than it is wide and the
game supplies a 16:9 desktop frame.

`r_Width`, `r_Height`, `r_Supersampling` and neighbours are now allowlisted, and
`Invoke-PreyVRStartup.ps1 -RenderWidth -RenderHeight -Supersampling` applies them
**before `xr.start`**, because the XR swapchain is sized from the backbuffer at
that moment.

This widened a fail-closed list, deliberately. The exclusion existed because a
mid-session resize invalidates the latched swapchain -- D3D11 `CopyResource`
needs matching dimensions and does not rescale, so a resize produced a failed or
corrupt copy with nothing visible. That hazard is now handled where it lives: a
backbuffer whose size no longer matches the session **refuses to submit** and
says so. An exclusion that makes a measured defect unfixable is not a safety
property; a guard at the point of danger is.

**Not measured:** whether Prey draws the scene at 2560x1440 or resolves down to
it from something larger. The ratio bounds what the compositor receives, not what
was drawn.

### HUD dispatch — built; HUD extraction still static only

**The reticle now dispatches, not just writes.** The engine's own reticle reset
(`0x1583A30`) writes `ArkPlayer+0x17EC/+0x17F0` and then dispatches
`reticleXOffset` / `reticleYOffset` on the HUD element, in one function. The
field holds the value; the dispatch is what the movie reads. `ReticleFollow` now
does both, in that order, which closes the reticle report's actual complaint --
that writing the field proved a memory write and not visual movement.

The stored literal for centred X is `0x3F000000`, that is **0.5f**, which settles
the units: these are normalised screen fractions, exactly what the lane already
computed.

**A completed dispatch is not visual acceptance, and that is measured.**
`hud.call SetCrosshairPosition` returned `result=0` against a name that does not
exist anywhere in the binary (F-011). Scaleform silently ignores an absent
function and the dispatcher still reports success. So only names read from a
native call site are used, and the headset remains the judge of whether the
crosshair moves.

### Main menu in the headset — verified, in the submitted image

Menu navigation existed and was already wired to the right stick and buttons; it
is now armable by name (`menu.nav 1`, or `-Controls`).

**Verified by looking.** A `capture` readback of the submitted backbuffer shows
the main menu: title, "Press Any Key", station artwork, 43.6% of sampled pixels
non-black in a clean 2560x1440 frame. The backbuffer is what gets submitted, so
the menu is in the submitted image.

**What this does not cover.** The headset was not connected for that run
(`xr.start status=unavailable(3)`), so it settles the image content and not the
end-to-end compositor path, and not whether the text is legible at headset
scale. Both still need the headset.

At 2688x2880 the menu **letterboxes** into a 16:9 band with black above and
below, because Prey's 2D UI keeps a fixed aspect. The 3D view does fill the
frame.

## What is not in the build

### HUD extraction — static only

The anchors the reticle report asked for are resolved:

| what | where |
|---|---|
| `GetHUDUIElement()` | `0x1665780`, the UI singleton's `+0x60` with `"DanielleHUD"` |
| `GetMarkerUIElement()` | `0x16657A0`, same with `"DanielleMarkers"` |
| **`IUIElement::CallFunction`** | element vtable **`+0x210`**, called as `(element, name, args, 0, 0)` |
| two-float helper | `0x11797C0` takes `(element, name, float, float)` and builds the argument array itself |
| one-float helper | `0x118C970` takes `(element, name, float)`; this is the one the reticle uses |
| confirmed names | `reticleXOffset`, `reticleYOffset`, `reticlePosition`, `alignToPosition`, `mapPan` |

Both helpers are the useful shape: each is a complete call, so moving or hiding
the native reticle does not require constructing `SUIArguments` by hand. **The
floats are in XMM2/XMM3**, and the decompiler's `undefined4` parameters must not
be read as integer arguments.

The two are told apart by their prologues, not by the decompiler: the two-float
entry saves **XMM3 and XMM2**, the one-float entry saves **only XMM2**. Ghidra
renders some one-float call sites as two-argument because it did not recover the
XMM parameter, so its arity cannot be trusted here.

That is where this stops. A HUD-only texture requires redirecting the named
element's rendering into a private transparent target **and** removing the
matching baked-in HUD from the scene images, then submitting a second
composition layer. That is a substantial piece of work with its own failure
modes, and F-014/F-015 already record UI corruption and deadlock from
second-render experiments. Anchors are not a route, and a route is not an
implementation.

## The order to test this in

1. `Invoke-PreyVRStartup.ps1 -Controls` and look at the menu. Does the headset
   show it, and does the right stick move the selection?
2. Load a save. Left stick to walk, right stick to turn. Watch `moveNative`: it
   is zero, or two lanes are driving the same control and the playbook says that
   presents as movement being mysteriously too fast rather than as two inputs.
3. Trigger to fire. `fireRefused` climbing means the trigger key id inference was
   wrong, which is the one thing in the control scheme that could be.
4. `aim.enable 1`, `aim.reticle 1`. Does the crosshair follow the controller?
   Watch `reticleDispatched` climbing against `reticleDispatchFailed` at zero.
   If it does not move while dispatch counts up, the names or the movie are
   wrong rather than the projection; `aim.reticledispatch 0` isolates the write.
5. Fire once with the weapon steady, then `aim.calibratebarrel`, then
   `aim.origin 2`. Do shots land where the crosshair is, at both near and far
   targets? Near targets are the test: that is where an eye-origin ray and a
   muzzle-origin shot disagree most.
6. Resolution needs its own launch:
   `Invoke-PreyVRLaunch.ps1 -RenderWidth 2688 -RenderHeight 2880`. That the
   backbuffer resizes is already measured, so what is left to judge is the
   framerate and the image. Expect a letterboxed menu.
