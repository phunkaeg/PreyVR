# Live inventory depth: confirmed native 3D planes

The running Steam Prey inventory contains authored layered 3D transforms, not
only a flat picture rotated by the mouse. This is a rendering-data finding;
stereo UI presentation has not been implemented or accepted in the headset.

Update September 13: an opt-in stereo prototype and native arithmetic verifier
are described in [the prototype report](INVENTORY-STEREO-PROTOTYPE-2026-09-13.md).
The eight measured planes remain valid. Their negative native offsets approach
the native camera; the later depth-mapping helper's opposite sign was corrected.
Runtime replay and headset acceptance of the prototype remain pending.

## Target and bounded observation

User authorized inspecting running PID 18188 and opened inventory. Steam
PreyDll SHA256 7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7,
base 0x7FFD20450000. Loaded mod path was
build/packages/PreyVR-selfserve-20260911b/PreyVR.dll, SHA256
784db78e5638d8516e31e4b9f93161078e5ef3147d34eeda06f4b3a6d8d985a4.

Frida 17.12.0 attached without pausing, first for 20 seconds, then 4 seconds.
No game functions were invoked, arguments/returns changed, input posted, or
targets redirected. Temporary observation hooks were removed and the sessions
detached. PID 18188 remained running and Responding afterward. Initial sandboxed
attachment was denied; the explicitly authorized elevated observer succeeded.

DaniellePDA was identified through GetUIElementByName RVA 0x2CE410, checking
element vtable 0x1CAB358 and player vtable 0x1DB56D8. player+0xB0 supplies movie
root vtable 0x1EB4B60. Its Display slot +0x130 resolves to 0x18AB710.
Only calls inside that root's Display scope were counted. Steam SetWorld3D
0xDBE300 and SetView3D 0xDBE1E0 were located by unique matching donor tails and
verified in Steam decompilation: 64-byte matrix copy, queued opcodes 12/13 and
11 respectively, with immediate renderer state at +0x1F0 / +0x1F8.

Live element 0x1FB4AD98AA0, player 0x1FAF47672E0, movie root 0x1FB346D9000;
visible=1. Addresses identify this session only.

## Result and control

The first probe observed 1,270 inventory Display calls, but its first 120 unique
matrices all belonged to one plane. Raw varying Z coordinates were explained by
common tilt and were NOT sufficient proof of layer depth.

The second probe captured 403 unique matrices in each of Display frames 1, 61
and 121. Subtracting common tilt by projecting origins onto the shared local-Z
normal produced the same eight relative plane offsets in all three frames:

`0, -1000, -2000, -2500, -5000, -7000, -9000, -15000` native UI units.

These units are not metres. Counts per plane were 309, 34, 52, 2, 2, 1, 2, 1
respectively. A tilted-plane control confirms translating within the panel
does not create a false extra depth plane. Raw views also contain a genuine
3D camera transform (Z translation approximately -36882.86 native UI units).

This establishes geometry at multiple depths within the inventory renderer.
It does not identify which plane belongs to a specific visible widget: some
may serve background, effects or masks. The second batch was stationary across
its samples, so it does not independently prove mouse position caused the
earlier different tilt. User observation supplies that behavioral lead.

## Integration implication

Preserve the movie's native 3D scene, replace pointer-driven viewpoint movement
with headset-relative eye transforms, and produce distinct eye images. Maintain
one UI advance/input update and correct native render-command ownership; do not
blindly call the release-bearing Flash callback twice. Mouse/beam selection must
use matching transforms. Native depth needs an explicit conversion and comfort
scale, and the world-behind-paused-inventory work remains separate.

Evidence and reproducible analysis are in
`evidence/inventory-hologram-2026-09-11/`: live-depth-01.json, live-depth-02.json,
probe_depth.py (final four-second version), analyze_depth.py, depth-analysis.json.
No mod source, package or launch settings were changed for this investigation.
