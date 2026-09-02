# Handover to the VR Modding playbook — PreyVR, 2026-09-02

Findings from PreyVR that generalise beyond this target. Written for the playbook
maintainer; PreyVR-specific detail is left in this repo and only the transferable
part is here.

Everything below was measured on Prey (2017), Arkane's CryEngine fork, D3D11,
injected mod, Quest 3 on VirtualDesktopXR.

---

## 1. FAIL-STR-033 confirmed on a production runtime — closes an open question

**The playbook currently says**, in `09-d3d11-openxr-injection.md#first-pixels-gamma`:

> A substitute runtime in this fleet treats a format-28 swapchain as linear and
> encodes it for display; whether the production runtime agrees is a separate
> measurement.

**It agrees.** VirtualDesktopXR on a Quest 3 double-encodes exactly the same way.
Symptom, cause and fix were the playbook entry verbatim.

Concrete numbers for the entry: swapchain created as `format=28`
(`R8G8B8A8_UNORM`) with `colour_conversion=no`; the game presents already
sRGB-encoded bytes into a plain `_UNORM` surface; the compositor treats them as
linear and encodes again. Naming the `_SRGB` variant (`format=29`) fixed it, and
a wearer confirmed correct gamma immediately.

**Suggested addition:** the format-28 warning can be upgraded from "whether the
production runtime agrees is a separate measurement" to "confirmed on VDXR as
well as the substitute".

**A selection-order trap worth adding.** Our format chooser preferred an *exact
match* to the source format before considering the sRGB variant. That is a
reasonable-looking rule which guarantees the bug: the exact match is precisely
the wrong answer when the source holds already-encoded bytes. The sRGB branch
existed and simply never ran.

---

## 2. Motion blur is a hard blocker for alternate-eye. Temporal AA is not.

**This appears to be new**, and it is counterintuitive enough to be worth its own
entry.

**Symptom:** near objects look soft or out of focus in the headset. Far geometry
is fine. Easy to misdiagnose as depth of field, as a wrong frustum, or as a
resolution problem.

**Cause:** motion blur is driven by velocity buffers. Under alternate-eye the
camera jumps a full IPD between consecutive frames, so the velocity buffer reads
that as large apparent motion and smears accordingly. Near geometry carries the
most parallax, so it smears worst — which is why the artefact is
distance-dependent and reads as focus rather than as blur.

**Fix:** disabling motion blur is a *requirement* of alternate-eye rendering, not
a quality preference.

**The part that surprised us:** temporal AA is fine. We predicted TAA would fail
for the same reason — it accumulates across frames which are now different eyes —
and warned about it on that reasoning. A wearer cycled all four AA modes in
headset and chose the temporal one. With motion blur off it does not soften near
geometry. **The reasoning was sound and the conclusion was wrong**, so the entry
should say "velocity-buffer effects" rather than "temporal effects" — the two are
not equivalent here.

---

## 3. Alternate-eye: carry the eye identity with the frame, do not wait for it

A technique that took our per-eye refresh from ~11 Hz to ~45 Hz, i.e. from
slideshow to usable.

**The naive approach**, and the one we shipped first: identify which eye is on
screen by *asking and waiting* — set an eye lock, hold it N frames so the game
thread's camera edit can reach the render thread, then take the image. Correct,
and unambiguous, which is why it is a good first implementation. But the wait is
the entire cost: a full left-right cycle takes `2 * N` frames, so at N=4 and 90
fps each eye refreshes at about 11 Hz.

**The better approach:** an engine's game/render thread split *delays* work but
does not *reorder* it. The camera built for frame N is rendered before the camera
built for frame N+1. So the eye never needs to be waited for — only carried. The
game thread pushes the eye it just built onto a small lock-free ring; the render
thread pops one per finished frame. Identity is exact, every rendered frame
updates an eye, and a pair completes every 2 frames.

**Make the ordering assumption measurable.** Pushes minus pops is the pipeline
depth in frames and should sit at a small constant. Drift means the 1:1
correspondence has broken and eye identity is no longer trustworthy. We export
that lag plus starved and dropped counts. Measured live: **lag 0–1 across 30,833
frames, 0 dropped**.

**Verify it off-headset first.** The failure mode is swapped eyes, which is
miserable to diagnose while wearing one. With nothing consuming the queue, lag
equals the publish count, so comparing lag growth against rendered-frame growth
directly tests the risk that the camera hook fires more than once per frame. Ours
measured **1.0019 publishes per rendered frame over 514 frames**.

---

## 4. Injected mods must declare the game's frustum, and the validator will fail

The playbook already carries the principle. This adds a worked instance with
numbers, which may be useful as the concrete example.

An injected mod cannot change what the game renders, so its pixels come from the
game's frustum. Declaring the runtime's preferred FOV over them is a statement
about the image that is not true, and **the runtime cannot detect it** — it
reprojects to whatever is claimed, so the error appears as wrong depth and wrong
scale rather than as an error.

Measured: Prey renders 120° horizontal by 88.507° vertical, zero asymmetry. The
Quest 3 reports `l -54.0, r +40.0, u +44.0, d -55.0`. Declaring honestly makes the
right edge differ by **20°**, and `submitted_fov_matches_located` fails.

**That failure is the pass.** For an injected mod, that check *succeeding* means
something declared the runtime's FOV and the image is a lie that satisfies the
validator. Worth stating in the imperative, because the natural instinct on
seeing a red check is to make it green.

---

## 5. Verify a derived value against a number the engine never supplies

A general anti-self-reference technique, prompted by getting caught by this twice.

Deriving a value from engine fields and then checking it by recomputing from the
same fields proves only that the arithmetic is consistent. It cannot catch a
wrong *convention*. We had a project-wide error where the engine's **vertical**
FOV was recorded as though horizontal; every internal check passed.

What settled it was checking the derived horizontal figure against **the player's
in-game FOV slider** — a number the engine never hands you, produced by a
different path entirely. Derived 120.000°, slider read 120. The engine's fields
never contain 120, so agreement is only possible if the convention is right.

**Generalisation:** for any derived quantity, find one external witness that does
not share the derivation's assumptions — a settings menu, a config file, a
physical measurement, a human observation. One such check is worth many internal
ones.

---

## 6. Instrument artefacts that impersonate bugs

Three that cost us real time, all in the "your measurement is broken, not your
code" family. Candidates for the debugging-methodology chapter.

**Capture cadence quantised to an even frame stride.** Six consecutive captures
under alternate-eye all landed exactly 28 frames apart — even, therefore same
parity, therefore the same eye. Six identical images, no alternation visible, and
it reads convincingly as broken alternation. The capture request-to-completion
round trip has its own period and **cannot sample both eyes at its natural
cadence**. Staggering the delays turned the artefact into the experiment: strides
30, 32, 34 gave the noise floor and stride 33 gave full parallax. Frame-stride
parity predicted the eye change four times out of four, which is stronger
evidence than the original test would have produced.

**Freezing the simulation on a menu background.** `t_Scale 0` while the game sat
on its menu backdrop — which the game renders blurred and dimmed by design.
Result: near-zero frame differences and a tiny cross-eye difference, because a
blurred depthless backdrop has almost no parallax to show. It reads exactly like
a dead stereo path. Cost us two runs. **Fix: a scene-content guard** — two
unfrozen captures a moment apart which must differ before anything is frozen. A
live scene animates; a menu backdrop does not. One capture, and the whole failure
class disappears.

**A control that silently disarms the thing under test.** Our stereo API treats an
IPD of zero as *disarm*. The obvious control — "set IPD to zero, expect no
difference" — would therefore have produced two identical frames because the
stereo path was never armed, and read as a clean control while proving nothing.
The working control was **the same eye captured twice at full IPD**: everything
identical including the path being live. Generalisation: a control must hold the
mechanism *on* and vary only the quantity.

---

## 7. Small practical notes

**Do not bind headset-mod hotkeys to Ctrl+Alt+Arrow.** That is Intel's
display-rotation shortcut where the driver's hotkeys are enabled. Rotating the
desktop under someone who is wearing a headset and cannot see it happen is a bad
outcome. We moved to PageUp/PageDown and Home/End.

**In-headset A/B controls pay for themselves immediately.** Changing a value by
having the wearer take the headset off, describing the change, and asking what it
looked like is a round trip slower than the thing being measured, and it destroys
the comparison — by the time the second value is applied the first is a memory. In
our session two consecutive settings were reported as indistinguishable, almost
certainly because of the gap rather than the settings. Cycling from inside the
headset made the same comparison trivial. **The wearer asked for this**, which is
worth noting: it was not obvious from the outside.

The known limitation is feedback — the wearer cannot read the current value. A
press counter is worth exporting, because "the poll cannot see your keys" and "the
setting does nothing" are otherwise indistinguishable.

---

## 8. CryEngine stereo, for the engine profiles

From reading Crytek's own source (`CryGame` tree, CryEngine 3, closest to Prey):
**their stereo traverses the scene once and submits twice**
(`CD3DStereoRenderer::RenderScene`).

This matters because it identifies a whole class of attempt as being at the wrong
seam. Doubling the *traversal* — calling the render path twice per frame — is not
what the engine does, and our attempts at it failed consistently: with the
secondary-pass flag off the engine leaks and dies within 13–19 frames; with it on
the second pass survives but costs only ~7% of a frame, i.e. it is hollow; with it
on plus a real per-eye camera it crashes.

Also from the source, and independent confirmation of something we had
reverse-engineered: the asymmetry fields (`wL/wR/wB/wT`) are **frustum-edge
offsets in near-plane units**, not angles. Converting an edge offset to a tangent
is therefore a division by the near plane. Our own test of this had been
self-referential, so the outside confirmation mattered.

---

## Status of the source project

Working stereo through a Quest 3: depth solid, scale believable, ~90 fps, gamma
correct, judder acceptable. Head tracking is scoped but not started — the pose is
submitted from the runtime while the image still comes from the game's camera, so
looking around does not yet move the world.

`unitsPerMetre` for Prey remains **assumed 1 and unmeasured**. That a 0.064 m eye
offset looked correct on a static view is weak evidence for the assumption, not a
measurement of it, and positional tracking is where an error there would show.
