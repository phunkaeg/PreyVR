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

## F-004 — RenderDoc blocks vendor extensions; Prey null-derefs at NVAPI init

- **Date:** 2026-08-15
- **Target:** `Prey.exe` 1.0.1.0 / `PreyDll.dll` SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- **Intent:** Capture two frames with `e_ArkLookingGlass` toggled, to decide H-007 without apitrace after F-003.
- **Mechanism:** RenderDoc 1.45 Launch Application. Executable `…\x64\Release\Prey.exe`, working directory the game root, environment `SteamAppId=480490`, default capture options.
- **What worked:** The `SteamAppId` variable defeated `SteamAPI_RestartAppIfNecessary`, which had silently killed every earlier direct launch. `Game.log` confirms the game reached `Renderer initialization`, created its window at 2560x1440, and initialised its own crash handler.
- **Result:** `EXCEPTION_ACCESS_VIOLATION` reading address `0x0`, at `0x00007FFFB689024A`, exception module `<Unknown>`. CryEngine wrote `error.dmp` (50 MB), `error.log`, and `Game.log` to the game root. The last successful log line is decisive:

```text
Direct3D driver is creating...
Creating window called 'Prey' (2560x1440)
NVAPI: DepthBoundsTesting supported     <- last success
Begin handle Exception                  <- crash
```

- **Interpretation:** The crash lands immediately after Prey's NVIDIA vendor-extension query during D3D11 device creation. RenderDoc carries a capture option named `AllowUnsupportedVendorExtensions` in `replay\capture_options.cpp`, together with `!!! Vendor Extension enabled: %s`, so it refuses vendor extensions by default. That the crash lands exactly at the vendor-extension step is strong circumstantial evidence, but the dump was not symbolised and `<Unknown>` module was not resolved, so the causal link is inferred rather than proven.
- **Blocked fix:** `AllowUnsupportedVendorExtensions` is **not** among the twelve capture options this RenderDoc UI serialises into `most_recent.cap`, so there is no confirmed way to enable it from the GUI. Do not guess at one. RenderDoc also warns `Capture requires vendor extensions by %s to replay, but no support for that is available.`, so even a forced capture might not replay.
- **Runtime evidence gained:** `NVAPI: DepthBoundsTesting supported` is the first **runtime** confirmation that R-025's vendor-extension branch actually executes, and that the NVIDIA path is the one taken on this machine. The device-creation capture had explicitly left this open. The AMD twin string `AGS: DepthBoundsTesting supported` sits adjacent at `0x181DD1758`.
- **Rule:** Prey takes a vendor-extension device-creation path before plain `D3D11CreateDevice`. Any capture, injection, or wrapping layer must tolerate that path or it will fault during device creation. Check this before reaching for a new graphics tool, not after.
- **Recovery:** No project files were modified. The `Release` directory still holds exactly its original seven files; the crash artefacts are the game's own and sit in the game root. Delete the 50 MB `error.dmp` when finished with it.

## F-005 - A recorded artifact hash did not reproduce; treat artifact hashes as capture labels

- **Date:** 2026-08-15
- **Target:** `PreyVR.dll` `0.3.0-lifecycle-hardening`, built by `tools/Run-Headless.ps1`
- **Intent:** Add a comment to `src/common/EngineMap.cpp` marking a deferred rename, then confirm the
  comment left the artifact hash untouched.

### What happened

| Build | Source state | Kind | `dll_sha256` |
| --- | --- | --- | --- |
| 1 | committed | (from the hardening pass) | `179652AA...` |
| 2 | one added comment | incremental | `80AACFC3...` |
| 3 | reverted, byte-identical to build 1 | incremental | `1D21F6D8...` |
| 4 | unchanged from build 3 | **fresh** (`cmake --fresh`) | `1D21F6D8...` |

### Correction to this entry's first version

This was first written up as "the build is not byte-reproducible", on the strength of builds 1-3.
**Build 4 refutes that headline.** A fresh build and an incremental build from identical source
produced the same hash, so the build *is* deterministic run-to-run in this environment. The original
conclusion was drawn from three points and asserted a mechanism - a regenerating PDB signature GUID -
that was never tested.

A second hypothesis was then raised and also refuted: the `openxr_loader.dll` hash differs between the
build-1 manifest (`6DF5C6EC...`) and the current one (`5E502DFD...`) despite the same pinned commit,
which looked like it might propagate. It cannot. `PreyVR.dll` imports only `bcrypt.dll`, `SHELL32.dll`,
`ADVAPI32.dll` and `KERNEL32.dll`, all with zero import timestamps; the loader is not linked into it.

**What is actually established:** the build is deterministic here, and the hash recorded for build 1
does not reproduce from the committed source. **Why build 1 differs is not established.** The most
plausible remaining explanation is that build 1 was produced from a source or toolchain state that
differs from the commit it was recorded against - for instance, built before a final edit that landed
in the same commit - but that has not been demonstrated and should not be repeated as fact.

### Damage done

[`HANDOVER-2026-08-07-HARDENING.md`](HANDOVER-2026-08-07-HARDENING.md) instructed the next operator to
"Load exactly the DLL whose SHA-256 is shown above" and to validate the resulting log with
`-ExpectedDllSha256 179652AA...`. **That artifact was overwritten by rebuilding and does not come back**
- confirmed, since a fresh build from the committed source yields `1D21F6D8...`, not `179652AA...`.
Behaviour is unchanged and all 11 tests pass, but the specific binary that procedure named is gone.

### Rule

Regardless of the unresolved cause, the operational rule stands and is what matters:

**Compute the hash of the DLL immediately before loading it, and record that value alongside the
capture.** Do not rely on a hash written into a document earlier, and do not rebuild between recording
and loading. A documented artifact hash is a historical label attached to a capture, never a target to
rebuild toward. Whether a given rebuild reproduces it is not something to assume in either direction.

### Not affected

The fail-closed gate is unharmed. The 22 landmark signatures, the `supported_preydll_sha256` game
baseline and the build doctor all validate against the *game* binary, not against the mod's own hash.

### Current reference artifact

`1D21F6D8DE722926187D4377E6F1CAEDC33456F665CFABD03BB57A9CFC3BE614`, from a **fresh** build of the
committed source at `c062097`, 11/11 passing, manifest and on-disk file agreeing. Recorded as the
reference on 2026-08-15. Do not rebuild before the pending supported-host load.

### Loose end worth its own look

The bundled `openxr_loader.dll` hash changed from `6DF5C6EC...` to `5E502DFD...` across builds despite
`openxr_commit` staying pinned at `64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec`. That loader is shipped
with the mod, so its reproducibility is a real question even though it is provably not the cause here.
