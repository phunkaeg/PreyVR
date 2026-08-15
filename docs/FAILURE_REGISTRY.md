# Failure registry

Record failed experiments with enough detail that a later session does not casually repeat them.

## F-001 — Frida CModule callback at DXGI Present

- **Date:** 2026-07-31
- **Target:** `Prey.exe` 1.0.1.0 / `PreyDll.dll` SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- **Intent:** Count Present calls and capture the immediate return address without running JavaScript on the render thread.
- **Mechanism:** A temporary Frida `CModule` callback attached to the system `IDXGISwapChain::Present` implementation while the interactive execution thread waited.
- **Result:** `Prey.exe` crashed with exception `0xC0000005`; Windows reported an unknown faulting module / `StackHash_1030`. A local dump was written to `C:\Users\meise\AppData\Local\CrashDumps\Prey.exe.485216.dmp`.
- **Likely mechanism:** Callback or listener lifetime became invalid while the interactive Frida execution/session was blocked or torn down. This is an inference from the unknown/freed-code fault region, not a symbolized dump result.
- **Rule:** Do not use temporary CModule/Promise-based hooks through this MCP's interactive execution surface for frame-frequency functions. Use an externally-owned persistent tracer or Cheat Engine's non-breaking logging hardware breakpoint.
- **Recovery:** Relaunch Prey; no on-disk game files or save files were modified.

## F-002 — ReGenny `+N` is a relative delta, not an absolute offset

- **Date:** 2026-08-01
- **Target:** `regenny/PreyVR.genny` against `Prey.exe` / `PreyDll.dll` SHA-256 `7D6E322F…B05311A7`
- **Intent:** Overlay the renderer singleton and the per-frame view block using absolute struct offsets taken from Ghidra.
- **Mechanism:** Fields were written as `IDXGISwapChain* swapchain +0xAE88`, matching the absolute-offset reading implied by ReGenny's own `AGENT.md` example.
- **Result:** In this ReGenny build, `+N` places a field **N bytes after the previous field's end**. Offsets accumulate. The original `PreyRendererGraphics` therefore resolved `swapchain` to `+0xAE90`, `device` to `+0x15DC0`, `context_related_interface` to `+0x20D08`, `device_mirror` to `+0x2BCA8`, and `swapchain_mirror` to `+0x36C58`. An overlay read of a camera position returned `(-1635.07, -7952.76, 6753.18)` where direct arithmetic at the same offset returned `(783.58, 1573.48, 17.10)`.
- **Why it went unnoticed:** This defect was present in the notebook from the start. Every earlier finding came from x64dbg, Cheat Engine, or explicit Lua address arithmetic, so no overlay value was ever read back and compared. `regenny_list_types` also reports a struct size that is the sum of all field ends, which looks anomalous but was not investigated.
- **Rule:** Write every `.genny` field offset as a delta from the previous field's end, keep fields in ascending absolute order, and record the intended absolute offset in a trailing comment. **Never trust an overlay value until it has been diffed against a direct `p:read_*` at the same absolute address.** A silently wrong overlay is worse than no overlay, because it produces plausible-looking numbers.
- **Recovery:** All three structs were rewritten with correct deltas and a prominent syntax warning at the top of the file, then verified field-by-field: 21 of 21 overlay reads matched direct arithmetic exactly. No finding in `RESEARCH_LOG.md` was affected, because none was obtained through an overlay.

## F-003 — apitrace `--mhook` DXGI tracing crashes Prey in `ntdll`

- **Date:** 2026-08-15
- **Target:** `Prey.exe` 1.0.1.0 / `PreyDll.dll` SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- **Intent:** Capture two D3D11 call streams with `e_ArkLookingGlass` toggled, then `diff_traces` them to decide H-007 — whether Prey's native second-scene path can be driven from a second camera.
- **Mechanism:** apitrace 14.0 win64, `trace --api=dxgi --mhook`, invoked through Steam launch options wrapping `%command%`. `--mhook` was chosen deliberately: `Prey.exe` imports no graphics DLL, and R-025 proves `PreyDll.dll` acquires `d3d11.dll`/`dxgi.dll` through `LoadLibrary` at runtime, so IAT patching has nothing to patch at startup.
- **Result:** Prey crashed during startup. Windows Application Error reported faulting module `ntdll.dll` 10.0.26100.8972, exception `0xC0000005`, fault offset `0x164A57`. Dump at `C:\Users\meise\AppData\Local\CrashDumps\Prey.exe.75828.dmp` (17.8 MB).
- **Confirmed the wrapper did load:** `prey-lookingglass.trace` was created at the crash timestamp but is **0 bytes**. This distinguishes the failure from the earlier no-Steam attempt, whose log ended `dxgitrace.dll was never used`. The Steam launch-option route works; the process died before any call was recorded.
- **Controls:** apitrace is healthy on this machine — Far Cry 2 and SWAT 4X traces were captured the same day at 68 MB, 1.05 GB and 3.3 GB. Prey itself is stable uninstrumented; it sustained an extended live ReGenny probe session earlier the same day. The crash correlates with `--mhook` on this target, not with apitrace generally or with the game.
- **Likely mechanism (inference, not proven):** `--mhook` relocates instructions by disassembling the target prologue. An earlier run logged `ANOMALY: use of REX.w is meaningless (default operand size is 64)` at `0x7FF9D5522064`, an address in the system-DLL range, showing mhook's length decoder parsing system code and meeting a redundant REX prefix. A mis-measured instruction length there yields a corrupt relocation and an access violation inside `ntdll`. This is the same redundant-REX hazard catalogued in [`BUILD_BASELINE.md`](BUILD_BASELINE.md), one layer down: there it threatens signature matching, here it threatens a hook engine's relocation. The dump was not symbolised, so treat the mechanism as a hypothesis.
- **Rule:** Do not re-run apitrace `--mhook` against Prey without first establishing why `ntdll` faults. `--method=iat` is materially safer — it rewrites import pointers rather than patching instructions in place — and is the only apitrace variant worth retrying, accepting that dynamic loading may leave it with nothing to hook. For D3D11 frame structure questions, prefer RenderDoc, which is mature on this API and already the project's sanctioned capture route.
- **Note:** `install_wrapper` cannot serve as a fallback here. The MCP rejects it outright: *"D3D10/D3D11 tracing requires trace_launch(api='dxgi'); a manual DXGI wrapper install is not supported."*
- **Recovery:** No game files were modified; the release directory still holds exactly its original seven files. Clear the Steam launch options before playing normally, or every launch retries the crash.
