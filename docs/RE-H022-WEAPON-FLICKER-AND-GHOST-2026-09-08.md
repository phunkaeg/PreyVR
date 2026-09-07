# H-022: weapon flicker and persistent displaced silhouette

Static investigation on 2026-09-08 against repository `3651d12` and Steam
PreyDll SHA-256 `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
No game launch, injection, attachment, capture, runtime writes or mod code
changes. Claude retains runtime ownership. Existing user observations and
documented counters are runtime reports, not newly acquired measurements.

## Finding and priority

**The current near-offset implementation has an eye/frame ownership defect.**
It selects the eye from a mutable game-thread global even though its view-info
can belong to a previously queued render frame. It also reads the right axis
from the global camera rather than the camera that produced that view-info.
These inputs are not a coherent snapshot of the rendered view.

This is a concrete source defect, and a strong candidate for the intermittent
outward jump. It is **not yet a runtime-proven cause of these pixels**. A second
strong candidate is disagreement among the weapon's color/depth/effect paths;
this can account for a persistent silhouette and geometry-dependent occlusion.
Both deserve testing without assuming that the main model is at 1x and a ghost
at 2x, or that the two symptoms necessarily share one cause.

## 1. The eye used for near rendering differs from the eye used for submission

Source chain, current line numbers:

- `CameraEditHook.cpp:1109`: after building a synthetic eye on the game thread,
  stores that eye into `gLastEye` and at line 1114 publishes it to the handoff.
- `CameraEditHook.cpp:1963`: `LastRenderedEye()` just loads `gLastEye`.
- `CameraEditHook.h:204-208` explicitly says this is observation-only and that
  game and render threads are a frame or so apart.
- `NearViewStereo.cpp:259`: calls that observation-only accessor to choose
  the near-matrix offset for **every packer invocation**.
- `XrSessionHost.cpp:600`: identifies the completed image using
  `ConsumeRenderedEye()`, a separate queued value.

An admissible schedule is:

| Event | Latest global eye | Frame being rendered |
| --- | --- | --- |
| Game builds N, left | left | N |
| First pass of N packs constants | left | N |
| Game builds N+1, right | right | N still in flight |
| Another pass of N packs constants | right | N |
| Present submits N using queued tag | right | submitted as left |

Every tag is valid. `nearNoEye`, `nearRefused`, `nearReentered` and
`nearAlreadyOffset` can all remain zero. The same issue can occur between
frames without an intra-frame transition. Scene complexity can change frame
overlap, pass ordering and queue depth; it is not proof of same-buffer races.

The source already avoids latest-global tagging when submitting pixels. The
near offset needs the same **render-view/frame identity**, not a second call
to the queue consumer for each draw. Consuming at each near draw would break
the one-entry-per-frame contract.

`CameraRightAxis()` at NearViewStereo lines 212-223 reads CSystem's global
camera. Even with a corrected eye bit, a newer head orientation can rotate the
offset relative to the older VP. Match eye displacement, orientation, units,
frame and view generation as one immutable record.

## 2. Wrong-eye offsets can jump outward in both eyes

HYPOTHESES.md previously ruled out wrong tagging because it supposedly moves
the weapon the wrong way. That conclusion is false when describing movement
relative to the normally rendered image.

For a simple camera looking forward, positive screen X means right. With
eye X = e, projection is proportional to `(vertexX - e) / depth`.
Left eye e=-h normally moves a fixed point right by h/depth; using +h instead
moves it left by h/depth: an outward jump of **2h/depth**. The right-eye case
is its mirror: an outward jump to the right.

At h=0.032, depth=1 and focal scale=1, the wrong-minus-correct NDC changes are
left -0.064 and right +0.064. This is a synthetic sign/magnitude proof, not a
measurement of Prey's weapon pixels. The user's apparent location match does
not establish a calibrated 1x/2x offset ratio.

## 3. Zero near delta does not eliminate depth or postprocessing disagreement

The existing report identifies the control as `near.zero 1`, leaving world
stereo armed. This is narrower than zeroing world IPD, and the exact command
should be retained in future receipts.

If one path uses M0 and another uses `T(-eyeDelta)*M0`, setting eyeDelta to zero
makes them coincide. This holds whether the second path is color, depth,
normals, an effect mask, shadow sampling or temporal reprojection. A screen-space
effect does not have to redraw the mesh: it can expose the silhouette from a
depth/mask buffer created at a different position.

Therefore the control establishes dependence on the near modification. It
does not prove a double application or rule out a post effect. Nor must a
persistent ghost and a transient solid-mesh jump have the same mechanism.

## 4. A separate native matrix path is confirmed

Prior H-011 established two distinct paths. New reads here confirm another
consumer/cache of the unmodified renderer path:

| Steam RVA | Target-specific evidence |
| --- | --- |
| `0xFB1670` | Reads pipeline owner `+0x2E0` to select CRenderView; native branch uses derived camera `+0x1620`, current CCamera `+0x11A0`, previous CCamera `+0x13E0`; null view selects fallback |
| `0xFB57A0` | Packs view-info near VP `+0xA0` into payload `+0x90`; current hook modifies only this near VP |
| `0xF39E00` | Object flag bit 23 selects nearest camera-state transition |
| `0xF43D70` | Nearest camera switch changes frustum and depth range and calls renderer SetCamera; separate from editing view-info `+0xA0` |
| `0xF42C00` | Builds renderer zero VP at renderer `+0x230` from its zero-view matrix and projection |
| **`0xF18970`** | Copies renderer `+0x230..+0x26C` into per-slot parameter cache at `renderer+slot*0x380+0x8D64..+0x8DA0` |
| `0xF054F0` | A separate near-aware deferred-shadow projection setup reads near FOV at renderer `+0x95B4` and constructs sampling data from a supplied camera |

For the new cache path, instructions at `0xF18ABE` load `[RCX+0x230]`, then
`0xF18AC4` writes `[RSI+RDI+0x8D64]`. RCX and RDI resolve from the renderer
singleton; RSI is slot*0x380 from `[renderer+0x4998]`. The remaining matrix
components are copied similarly. The routine compares cache stamp
`renderer+slot*0x380+0x8CA0` with `renderer+slot*0x328+0x4C94` before updating,
and also has a shader-related gate. This is another reason that many calls to
the modern packer do not prove every native matrix cache sees the same change.

**Limit:** this proves a parallel native matrix/parameter path, not which
visible weapon draws consume it. The shader binding, draw identity, depth
target and pixel contribution still require correlation. Do not globally patch
renderer `+0x230`: it serves broader camera-relative rendering.

Local CryEngine source supplies useful shader vocabulary: Z/depth, material
and effect paths use view-projection parameters; nearest depth has special
conversion; legacy parameter upload can read the renderer zero VP. These are
leads. In particular, this ancestor names `CV_PrevViewProjNearestMatr`, but
**Prey's `viewInfo+0x260` is the bare near projection in the verified H-011
builder**, not permission to transplant that ancestor's previous-near layout.

## 5. Existing counters cannot support the recorded exclusions

| Existing claim | Actual coverage |
| --- | --- |
| `nearNoEye==0` proves correct eye | Only proves global value was 0 or 1, not that it belongs to this render view |
| Many near draws prove all weapon draws covered | Packer invocations are not draw identities; no shader/buffer/mask/pass attribution |
| `nearReentered==0` rules out nesting | Rules out only the tested same-pointer thread-local condition; keep the observed negative, not broader claims |
| `nearAlreadyOffset==0` rules out copied matrices | RowAlreadyWritten requires pointer equality. A copy at another pointer cannot match, by construction |
| Remembered rows prevent concurrent edits | Lookup, matrix snapshot/write, remember and restore are separate operations. Mutex protects the table, not the engine matrix or edit transaction |
| Lineage logs every row per frame | RecordLineage retains first row per pointer/eye, only increments edits afterward, has 12 slots, no frame-key/reset per frame and no overflow counter |
| IK writes equal frame count proves both eyes share a pose | It counts work frequency; alternating frames can legitimately contain different poses. It does not compare actual submitted eye pose samples |

These limitations do **not** resurrect nesting/copy/race as the diagnosis.
They explain why negative instrumentation cannot exclude the unobserved cases.
Avoid another row heuristic as the primary next experiment.

## 6. Recommended bounded next step

### A. Correct view/frame ownership

Publish immutable near-eye metadata from the known main-view camera producer
and bind it to the actual CRenderView plus frame/generation. At the packer,
resolve metadata through its input's proven provenance; native view-info
`+0x08` holds the source current camera pointer on the examined builder route.
This is a lookup/provenance anchor, not a license to identify frames by pointer
alone (views/cameras are reused). Classify fallback and secondary views.

Use that view's eye-minus-cyclops displacement and camera basis, with the same
world scale as its camera. Do not read the latest game-thread global. Preserve
the record through all passes and into eye-image publication.

The original H-011 report already proposed a copied packer input. Prefer
editing a private, appropriately aligned 0x2D0-byte view-info snapshot, after
confirming input-only/synchronous use and pointer-member lifetime, over modifying
shared engine storage across the original call. Packer output/destination
ownership must still be preserved. This reduces interference but alone does
not fix stale eye identity or missing legacy paths.

### B. Cheap discriminator without another speculative fix

For the runtime owner: hold a fixed left eye through the existing
`PreyVR_SetStereoEyeLock(0)` export, allow queued frames/history to settle,
and inspect the live desktop image in a complex scene. Repeat with right (1),
then restore alternation (2). Keep nonzero near displacement and verify frame
progress. **Do not use a held screenshot or judge the stereo headset pair:**
with one eye locked, this mod deliberately retains the other eye's old image.

Control: alternating eyes, same scene and settings. Variable: eye alternation
only. Decision: disappearing flicker supports an eye/frame or eye-history
dependency, but does not distinguish those two by itself. Persistent ghost with
settled eye strengthens the case for a simultaneous pass mismatch. Persistent
flicker means continue checking pass selection and geometry/pose ownership.

### C. Attribute color, depth and effect pixels

Capture or use a bounded draw trace only through a verified supported path.
Prey's F-004 documents NVAPI-related RenderDoc launch failure; 'one keypress'
is not an established safe capture setup for every current configuration.
Existing captures can be analysed independently. One frame can identify the
steady ghost; intermittent flicker needs good/bad frame comparison or a trace
correlated with a bad event.

Record actual weapon VB/IB/shader identity, active constant-buffer ranges and
bytes, render/depth targets, viewport/depth range, view/eye/frame and relevant
model/skin transforms. Inspect depth/normal/velocity/effect buffers at both
silhouette positions, plus final pixel history. A fullscreen effect may use
the weapon's depth without another weapon mesh draw. One renderer screenshot
or a count of packer entries cannot distinguish these cases.

If an effect toggle is needed, change one pass at a time, keep nonzero near
delta, restore it and verify recurrence. Preserve the prior headset-tested AA
mode 3 / motion-blur-off baseline; that earlier acceptance does not prove newly
modified near matrices agree with every depth/history consumer, but it also
does not justify blaming TAA without evidence.

## Validation completed

`C:/Python314/python.exe -X utf8 -B tools/re/verify_h022_near_contract.py` passes:
five exact Steam instruction anchors, a delayed-frame counterexample with
two valid but disagreeing eye tags, outward-jump projection math, different-
pointer row-guard coverage, and zero-delta cross-pass collapse.

These are static/analytic checks, not a rendered reproduction. Raw native
evidence is in [`evidence/h022-near-paths-2026-09-08.json`](evidence/h022-near-paths-2026-09-08.json).
No implementation change or headset fix is claimed complete.
