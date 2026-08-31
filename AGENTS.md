# PreyVR


> Also read `CLAUDE.md` in this folder — it carries project rules that are **not** repeated here,
> and they apply whichever tool you are. Claude Code auto-loads `CLAUDE.md` and Codex auto-loads
> this file, so neither sees the other's unless it goes and reads it. Treat the pair as one
> instruction set.

VR modding project for Prey (2017). Target: `Prey.exe` (64-bit, CryEngine/Arkane, D3D11).

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

## Cross-engine docs graph (added 2026-08-27)

This project is **not** in that graph. It covers SS2VR, BioshockVR and SOMAVR — Dark/KEX,
Unreal 2.5 Vengeance and HPL3, across D3D11 and OpenGL — so query it as **prior art**, for
problems those three already hit.

Separate from this project's own `graphify-out/`. Use it for **"has another project hit this?"**:

```
graphify query "<question>" --graph "D:\Dev Debug\VR Modding\cross-engine-graph\graphify-out\reconciled-graph.json" --budget 900
```

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
