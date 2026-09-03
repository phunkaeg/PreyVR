# PreyVR

> Also read `CLAUDE.md` in this folder — it carries project rules that are **not** repeated here,
> and they apply whichever tool you are. Claude Code auto-loads `CLAUDE.md` and Codex auto-loads
> this file, so neither sees the other's unless it goes and reads it. Treat the pair as one
> instruction set.

VR modding project for Prey (2017). Target: `Prey.exe` (64-bit, CryEngine/Arkane, D3D11).

## Engine source for this target

**CryEngine source is on this machine:** `D:\Dev Debug\source code\CRYENGINE`. Four CryEngine trees are on this machine; **`CryGame` is CryEngine 3 and closest to Prey**. `CCamera::SetAsymmetry` gives the engine native asymmetric frusta, and `IStereoRenderer.h`/`D3DStereo.cpp` show how Crytek did per-eye rendering. Prey is an Arkane fork, so this is structure and vocabulary, never offsets. See playbook ch00 `#local-engine-sources`. **Read ch00 `#cryengine-asymmetry-contract` first:** the source builds the projection as `Frustum(wL + GetAsymL(), ...)` - the same `wL/wR/wB/wT` your RE named `fWL/fWR/fWB/fWT`. It also shows the near/viewmodel pass rescaling every shift by `DRAW_NEAREST_MIN / nearPlane`, and `CD3D9Renderer::RT_EndFrame` - the function you hook - is declared in `RenderDll/Common/Renderer.h`.

## Your full MCP roster — 12 servers

Codex cannot load a skill, so the complete list lives here rather than only in `re-mcp-toolkit`.
**Preflight before use; if a server is not live, say so rather than working around it silently.**

| Server | Prefix | Reach for it when |
|---|---|---|
| Ghidra | `mcp__ghidra__` | static analysis, decompilation, signatures, struct layouts |
| ReGenny | `mcp__regenny__` | live struct layout against a running process |
| Frida | `mcp__frida__` | prove a function fires, hook without a debugger, in-process scripting |
| Cheat Engine | `mcp__cheatengine__` | find a value live, pointer chains, AOB scans |
| x64dbg / x32dbg | `mcp__x64dbg__` | breakpoints, stepping, register/stack inspection |
| RenderDoc | `mcp__renderdoc__` | one deep frame: draws, cbuffers, pipeline state |
| apitrace | `mcp__apitrace__` | the whole run's call stream; D3D8/9, legacy GL |
| ILSpy | `mcp__ilspy__` | .NET assemblies |
| **local-llm** | `mcp__local-llm__` | offload bounded grunt work — ranking grep hits, digesting decompiler output, boilerplate. Probe `local_status` first; fall back silently |
| **codex** | `mcp__codex__` | a second, independent opinion on a bounded question |
| 3ds Max | `mcp__3dsmax-mcp__` | asset work — meshes, rigs, materials |
| After Effects | `mcp__after-effects__` | release or comparison video |

Non-MCP fleet tooling: **xr-sim** (`D:\Dev Debug\xr-sim`, headset-free OpenXR runtime) and
**xr-tape** (`D:\Dev Debug\xr-tape`, records and checks what this mod submits).

**Two tools beat one.** A capture localises, a debugger explains; a static finding is a hypothesis until
something live confirms it. See the `re-mcp-toolkit` skill's pairing table.

## xr-tape — record and check what this mod submits to OpenXR

`D:\Dev Debug\xr-tape` is an OpenXR **API layer** that records both eye poses, both projections, the
submitted layer set and frame timing to a trace, plus 20 checks over it. It needs **no code change in
this repo**, and it is selected per process, so nothing machine-wide is touched.

```powershell
& 'D:\Dev Debug\xr-tape\tools\Install-XrTape.ps1' -Architecture x64
& 'D:\Dev Debug\xr-tape\tools\Invoke-XrTape.ps1' -Executable <app.exe> -Check
```

It pairs with the RE MCPs by **localising**: a failing check names the frame and the value, and
RenderDoc/apitrace/Ghidra then explain that one frame. See the `re-mcp-toolkit` skill.

**Here specifically:** `preyvr_xr_session_probe` already passes **18 of 18 applicable checks** under xr-tape, so this is a working baseline today. `HEADLESS_TESTING.md` asks that every runtime probe emit a replayable fixture - a trace **is** that fixture, produced automatically. The open format-28 gamma question is a trace diff between xr-sim and VirtualDesktopXR.

## Reverse-engineering MCPs

Ghidra, ReGenny, Frida, Cheat Engine, x64dbg/x32dbg and RenderDoc are all available as MCP tools.
**Load the `re-mcp-toolkit` skill before using any of them** — it carries the preflight calls,
per-tool caveats, and the pairing workflows (Ghidra static offsets -> ReGenny live layout, etc.).
The **`vr-re-workflow`** skill is the other half — the METHOD rather than the tools: the
new-game onboarding checklist, the per-engine approach, and the recipe for finding the camera,
view matrix, FOV and player structs. Load it when you are deciding *what to look for*;
`re-mcp-toolkit` tells you *what to look with*.

1. **Never assume a tool's host app is running or attached.** The tools always appear in your tool
   list; that says nothing about whether the app behind them is live. Preflight first: Ghidra
   `list_instances`, ReGenny `regenny_status`, Cheat Engine `ping` (then `get_opened_process_id`),
   Frida `enumerate_processes`, x64dbg `get_debugger_status`.
2. **If a tool isn't live, ask the user to launch/attach it.** Don't guess, don't silently skip the
   step, and never fabricate a result you couldn't actually read. If the user has said this session
   that something is running, trust that until a call fails.
3. **Ghidra and ReGenny must be started before Claude Desktop.** Their tools still register when the
   apps are closed, but every call fails. Once running you can connect whenever. Ghidra keeps the
   relevant exes pre-loaded — but a program must be *opened* in the CodeBrowser before
   program-scoped tools work (`No context found for request` = nothing open).
4. **Cheat Engine and x64dbg/x32dbg are not running by default** — they must be requested. CE also
   has to be *attached to the running game*, which `ping` alone does not prove.
5. **RenderDoc only analyses existing `.rdc` captures — it cannot capture or inject.** Request a
   capture for a *named* use case (not a bare "take a capture"); captures live in `RenderDoc/` or
   `Captures/`.
6. Target is `Prey.exe` (Prey 2017, CryEngine-derived, **64-bit** -> use **x64dbg**).
7. RenderDoc works here — Prey renders via **Direct3D 11**.

## Check the graph BEFORE you decide (added 2026-09-03)

**`graphify/graphify-out/graph.json`** — note the `graphify/` prefix, it is not at
the repo root. Query it with `cd graphify && graphify query "<question>"`.

**Query it before committing to an approach**, not only when asked what the project
knows. Before choosing a seam, scoping a milestone, designing a mechanism, or
asking the user a design question.

The rule exists because on 2026-09-03 an agent, in one session, missed: this
README's own first paragraph stating the 6DoF end state; H-005's limb-IK
reconnaissance; a `RESEARCH_LOG` line already naming the `SViewParams` callback as
the planned route; and a fully tested `MotionController.h` carrying
`WeaponPoseFromController`, `AimFromController`, `TwoHandedWeaponPose` and
`SnapTurn`. Work was scoped that already existed, and a design question was put to
the owner whose answer was in the README.

**Grep matches literal strings, so it fails exactly when you do not already know
the vocabulary** — which is the situation whenever you are deciding what to build.
The graph is indexed by meaning. One query costs a single call; the failure it
prevents costs hours.

Refresh with `Graphify-Update-All.ps1` (code + semantics) or
`Graphify-Update-CodeOnly.ps1`. Check the graph's date before trusting it.

## Blocked? Read the fleet playbook FIRST (added 2026-09-01)

**`D:\Dev Debug\VR Modding\docs` — start at `bottleneck-map.md`.**

PreyVR **is** a tracked project in that playbook, by name, with a per-project row
and named bottleneck classes. Its `bottlenecks.yml` gives every class a
`fast_test` (the cheapest experiment that classifies it) and an `exit_proof`
(what is required to close it), plus the state of the same class on every sibling
project.

Its own agent protocol, condensed:

1. Read the project row for this target.
2. Read **only** the bottleneck definition and route for the current gate.
3. State hypothesis, control, test variable and decision rule **before code**.
4. Preserve ambiguous outcomes as ambiguous.
5. Update project evidence and fleet routing when the critical path changes.

It exists to stop two agent failures, and on 2026-09-01 this project committed
the second: **repeating an experiment another project already paid for.** A4 was
built on the native-scene-re-entry rung without checking BN-STE-001, which
records that three of the four shipped in-house projects priced that rung and
deliberately chose a lower one. The `bottleneck-map.md` + `bottlenecks.yml` read
that would have surfaced it costs about two thousand tokens.

The cross-engine graph below is a **different and narrower** resource. Read the
playbook first; use the graph second, for "which document minted this fact".
Note the wording below -- "this project is not in that graph" -- is true of the
*graph* and must not be read as "PreyVR is not in the fleet docs". It is.

## Cross-engine docs graph (added 2026-08-27)

> **Blocker? Search for prior art with ripgrep, not by reading candidate docs (or an agent).**
> `rg -i -c -e 'signalA|signalB|concept' "D:\Dev Debug\ss2vr-work" "D:\Dev Debug\somavr" "D:\Dev Debug\BioshockVR" | sort -t: -k2 -rn`
> (and `rg` the graph JSON explicitly — it is under a gitignored dir). Use the blocker's *real signals*
> — API/identifier names, error codes, concept words. Optionally hand the small hit-list to `coder-next`
> (MCP `local_ask`, `model="coder-next"`) to rank `<path> - <why>`; then open only the top 1-2 here.
> **Pointers, not answers** — a cross-project hit is INFERENCE until confirmed against these bytes.
> (Claude: full recipe in the `local-delegation` skill §3c.)

This project is **not** in that graph. It covers SS2VR, BioshockVR and SOMAVR — Dark/KEX,
Unreal 2.5 Vengeance and HPL3, across D3D11 and OpenGL — so query it as **prior art**, for
problems those three already hit.

Separate from this project's own `graphify-out/`. Use it for **"has another project hit this?"**:

```
graphify query "<question>" --graph "D:\Dev Debug\VR Modding\cross-engine-graph\graphify-out\reconciled-graph.json" --budget 900
```

> **⚠ An empty result from this graph is NOT a negative result. Verified 2026-09-01:**
> it contains **1,328 nodes and ZERO edges**, and covers **SS2VR, BioshockVR and SOMAVR only**.
> `Swat4-VR`, `FarCry2-vr`, `PreyVR`, `DishonoredVR` and `Sims4VR` return **zero hits because they
> are not indexed** — as do `9On12`, `D3D12`, `adapterLuid` and `xr-sim`. A DishonoredVR session spent
> five days on an architecture argument that Swat4-VR had already settled by experiment, and the query
> that would have found it returns nothing here.
>
> **So: silence from this graph means "not indexed", never "nobody hit this."** When it comes back
> empty, fall through to `D:\Dev Debug\VR Modding\docs\symptom-index.md` and
> `cross-project-index.md`, and then to the sibling project's own `docs/FAILURE_REGISTRY.md` — which
> is where the answers actually live.

Rules:

- **It tells you WHERE a problem was solved, never what the answer was.** Every node carries the document
  that minted it. Treat a hit as a lead, open that document, and grade what you find there yourself.
- **It indexes documentation, not code.** A symbol is absent because nobody wrote it down, not because it
  does not exist. It is also a snapshot — anything written since 2026-08-27 is missing.
- **Cross-project edges are tagged `same_concept_as`** with the concept name and a one-line reason. Read
  the reason; reject the link if it does not hold for your engine.
- **Cross-project findings are `INFERENCE` for this target** until confirmed against these bytes. Another
  engine solving a problem is a lead and a vocabulary, not a result.
- Use `..\VR Modding\graphify-key.bat` if the query needs an API key in the environment.

Rebuild and extension instructions: `D:\Dev Debug\VR Modding\cross-engine-graph\README.md`.
