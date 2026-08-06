# PreyVR — Claude guide

> Also read `AGENTS.md` in this folder — anything there (graphify rules, project notes) applies to
> Claude too; it lives in one file to avoid two copies drifting apart.

## Reverse-engineering MCPs

Ghidra, ReGenny, Frida, Cheat Engine, x64dbg/x32dbg and RenderDoc are all available as MCP tools.
**Load the `re-mcp-toolkit` skill before using any of them** — it carries the preflight calls,
per-tool caveats, and the pairing workflows (Ghidra static offsets -> ReGenny live layout, etc.).

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
