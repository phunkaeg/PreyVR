# The VR scheme: what was built 2026-09-08, and what was not

Six lanes were asked for. Five are built and one is static only. Nothing below
has been tested in a headset; the build is `398ecc0`.

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

### Resolution — levers built, the fix is not

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

### Main menu in the headset — partially

Menu navigation existed and was already wired to the right stick and buttons; it
is now armable by name (`menu.nav 1`, or `-Controls`).

**Whether the menu is visible in the headset was not verified and must not be
assumed.** It renders into the backbuffer, which is what gets submitted, so it
should be -- but "should be" is exactly the reasoning this project has been
burned by. One look settles it.

## What is not in the build

### HUD extraction — static only

The anchors the reticle report asked for are resolved:

| what | where |
|---|---|
| `GetHUDUIElement()` | `0x1665780`, the UI singleton's `+0x60` with `"DanielleHUD"` |
| `GetMarkerUIElement()` | `0x16657A0`, same with `"DanielleMarkers"` |
| **`IUIElement::CallFunction`** | element vtable **`+0x210`**, called as `(element, name, args, 0, 0)` |
| two-float helper | `0x11797C0` takes `(element, name, float, float)` and builds the argument array itself |

The helper is the useful shape: it is a complete call taking two floats, so
moving or hiding the native reticle does not require constructing `SUIArguments`
by hand. **The floats are in XMM2/XMM3**, and the decompiler's `undefined4`
parameters must not be read as integer arguments.

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
5. Fire once with the weapon steady, then `aim.calibratebarrel`, then
   `aim.origin 2`. Do shots land where the crosshair is, at both near and far
   targets? Near targets are the test: that is where an eye-origin ray and a
   muzzle-origin shot disagree most.
6. Resolution last, because it needs a restart:
   `-RenderWidth 2688 -RenderHeight 2880`. Check `xr.resolution` reports
   `pixelRatioPercent` near 100 and `sizeMismatch=0`, then judge the image.
