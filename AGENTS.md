# PreyVR

> Read `CLAUDE.md` in this folder once as well; the pair is one instruction set.
> Current user scope and the project-specific restrictions below apply throughout.

VR modding project for Prey (2017). Target: `Prey.exe` (64-bit, CryEngine/Arkane, D3D11).

## Engine source for this target

**CryEngine source is on this machine:** `D:\Dev Debug\source code\CRYENGINE`. Four CryEngine trees are on this machine; **`CryGame` is CryEngine 3 and closest to Prey**. `CCamera::SetAsymmetry` gives the engine native asymmetric frusta, and `IStereoRenderer.h`/`D3DStereo.cpp` show how Crytek did per-eye rendering. Prey is an Arkane fork, so this is structure and vocabulary, never offsets. See playbook ch00 `#local-engine-sources`. **Read ch00 `#cryengine-asymmetry-contract` first:** the source builds the projection as `Frustum(wL + GetAsymL(), ...)` - the same `wL/wR/wB/wT` your RE named `fWL/fWR/fWB/fWT`. It also shows the near/viewmodel pass rescaling every shift by `DRAW_NEAREST_MIN / nearPlane`, and `CD3D9Renderer::RT_EndFrame` - the function you hook - is declared in `RenderDll/Common/Renderer.h`.

## RE workflow and tool discovery

Read [the shared RE workflow](<D:/Dev Debug/VR Modding/AGENT_RE_WORKFLOW.md>) once when choosing an RE approach.
Use `vr-re-workflow` for the method and `re-mcp-toolkit` for the selected tool's
preflight and caveats. Read their `SKILL.md` files directly if there is no skill invoker.
The session's discoverable tools and schemas determine what is callable; a configured
server, a responding host and the correct open program/process are separate checks.

Useful tool families: Ghidra for static contracts; ReGenny / `cheatengine` for live
layouts; Frida / x64dbg or x32dbg for execution; RenderDoc / apitrace for graphics
evidence. `local-llm` is optional bounded assistance: probe once, fall back if offline,
and verify its output against the original evidence. A second model is not proof.
Choose tools by the question, not by a fixed server count or a two-tool quota.

Respect current user scope and exclusive runtime ownership. A closed app or missing
launch script is not proof that automation is impossible: check the existing runbook,
launcher and supported CLI/API within that scope. Keep static work moving when live
work is unavailable. Static contracts can be proved from bytes; runtime acceptance
and headset quality require their own evidence.

## xr-sim and xr-tape

`D:/Dev Debug/xr-sim` supplies a headset-free OpenXR runtime;
`D:/Dev Debug/xr-tape` records submitted poses, FOVs, layers and timing. Read their
current launcher scripts for process-scoped setup; this target needs the **x64**
loader/layer. Select runtime and layer per process, preserving shared machine state.
Use existing project launch/injection paths within the user's runtime authorization.
A trace checks the submission contract; it does not prove the game rendered those
views, accepted an input event, or looks correct in a headset. Correlate frames with
mod logs and graphics evidence.

`preyvr_xr_session_probe` and `HEADLESS_TESTING.md` are starting points. Quote the current trace and applicable checks, not a historical pass count.

## Target tool constraints

`Prey.exe` is Prey 2017, **x64 / CryEngine-derived / D3D11**. Use x64dbg only
within runtime scope. D3D11 support alone does not establish safe capture: read the
toolkit's Prey NVAPI/RenderDoc failure notes before a new capture attempt. Existing
captures and offline byte verifiers remain useful independently of that failure.
For static work, [the investigation guide](docs/RE-INVESTIGATION-GUIDE.md) and its
H-021 examples cover receiver bases, concrete virtual callees, writers and consumers.

## Project knowledge graph

For architecture, ownership and design questions, query the existing project graph
before broad source browsing: `graphify explain "<concept>"`, `graphify query
"<engine noun phrase>"`, or `graphify path "<A>" "<B>"`. Use `rg` first for exact
symbols, byte patterns and known filenames; verify graph leads in their source.
Check age and coverage. Dirty graph files alone do not invalidate a query. If the
graph or tool is unavailable, state that limitation once and use scoped source/doc
searches. Do not turn graph setup into a prerequisite for an unrelated fix.
Refresh after substantial indexed changes using the project's update procedure;
avoid rebuilding third-party corpora for a small edit. Read a full graph report
only when a broad architecture review needs it.

Here the graph is **`graphify/graphify-out/graph.json`**, not at the repo root.
Run queries from `graphify/`. Refresh with `Graphify-Update-All.ps1` or
`Graphify-Update-CodeOnly.ps1` when relevant indexed content changes materially.

## Fleet prior art

Before choosing a seam or repeating an experiment, route through
`D:/Dev Debug/VR Modding/docs/bottleneck-map.md` for this project's gate, or
`failure-atlas.md` / `symptom-index.md` for its symptom. Read only the relevant row
and linked section. The project's current receipts outrank a stale fleet summary.
State hypothesis, control, variable and decision rule before experimental code.
Record transferable results in the playbook when they change the route.

Use `D:/Dev Debug/VR Modding/cross-engine-graph/graphify-out/fleet-graph.json`
to find sibling documents: `graphify explain "Frustum and Culling Adjustment" --graph
"<absolute fleet-graph.json path>"` (one command). For queries use engine nouns, not
a long question; try near-duplicate concept names. Inspect graph age and coverage.
Cross-project links are `INFERRED` leads, not confirmation for this target. Open the
named source; its path may be relative to that project's `docs/`, and its label may
be a summary rather than literal text. A miss is not an exhaustive negative result:
fall through to `cross-project-index.md`, scoped `rg`, and sibling failure registries.
External prior art is indexed in the playbook's `sources.yml` / `docs/coverage.md`.

For scripts use `D:/Dev Debug/VR Modding/tools/graphio.py` (`load_graph`): node-link
relationships live under `links`; a defaulted lookup of `edges` can hide real data.
Check the interpreter before running helpers. Rebuild instructions live in
`cross-engine-graph/README.md`; do not rebuild the whole fleet for a routine lookup.
