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

## F-005 - The build is not reproducible: no `/Brepro`, so the PE timestamp is the link time

- **Date:** 2026-08-15, mechanism established 2026-08-16
- **Target:** every binary produced by `tools/Run-Headless.ps1`, including `PreyVR.dll` and the shipped
  `openxr_loader.dll`

### Established mechanism

`CMakeLists.txt` sets no `/Brepro`, and neither output carries an `IMAGE_DEBUG_TYPE_REPRO` debug entry.
Without that flag MSVC writes the real wall-clock link time into `IMAGE_FILE_HEADER.TimeDateStamp`:

| Binary | PE TimeDateStamp | decodes to | file mtime |
| --- | --- | --- | --- |
| `PreyVR.dll` | `0x6A813D67` | 2026-08-16 04:32:39 UTC | 04:32:39 |
| `openxr_loader.dll` | `0x6A813D16` | 2026-08-16 04:31:18 UTC | 04:31:21 |

The timestamps match the file mtimes and sit 81 seconds apart, in dependency order. **Every relink
writes a new timestamp, so every relink changes the SHA-256.** This is a property of the build
configuration, not of any one target, which is why the OpenXR loader "not reproducing" was never a
loader-specific mystery - nothing in this build reproduces.

Helpfully, neither binary has a `CODEVIEW` debug entry, so no PDB GUID or path is embedded. The
timestamp is plausibly the *only* source of non-determinism here, which means `/Brepro` alone may be
enough to make these builds reproducible. That has not been tested.

### Correction history, because this entry was wrong twice

1. **v1** concluded "the build is not byte-reproducible" from three builds. The conclusion was right;
   the mechanism given - a regenerating PDB signature GUID - was a guess, and is wrong. There is no
   CODEVIEW entry to regenerate.
2. **v2 withdrew the conclusion**, on the grounds that a `cmake --fresh` build reproduced the previous
   hash. **That was wrong.** `cmake --fresh` wipes the CMake cache and reconfigures but does **not**
   force a relink; existing outputs stay up to date. `PreyVR.dll` is timestamped 04:32:39 while the
   fresh `CMakeCache.txt` is 04:59:18 - the DLL is 27 minutes older than the reconfigure that
   supposedly produced it. Build 4 emitted no new binary, so the matching hash was the same file, not
   a reproduced one.
3. **v3, this version**, reinstates the conclusion with a mechanism that is checkable from the binary
   alone and needs no rebuild to confirm.

**The recurring error was reasoning from hash equality or difference without first establishing
whether a build had actually occurred.** A hash comparison is only evidence about determinism if a
relink demonstrably happened; check the PE timestamp or the file mtime before drawing either
conclusion.

### Damage done

[`HANDOVER-2026-08-07-HARDENING.md`](HANDOVER-2026-08-07-HARDENING.md) instructed the next operator to
load the DLL with hash `179652AA...`. That artifact was overwritten by rebuilding and cannot be
recreated, because a rebuild cannot reproduce any prior hash.

### Rule

**Compute the hash of the DLL immediately before loading it, and record that value with the capture.**
Never rebuild between recording a hash and loading that binary - not even for a comment, since any
relink changes it. A documented artifact hash is a historical label attached to a capture, never a
target to rebuild toward.

### Not affected

The fail-closed gate. The 22 landmark signatures, the `supported_preydll_sha256` game baseline and the
build doctor all validate against the *game* binary, not the mod's own hash.

### Current reference artifact

`1D21F6D8DE722926187D4377E6F1CAEDC33456F665CFABD03BB57A9CFC3BE614`, linked 2026-08-16 04:32:39 UTC from
the committed source at `c062097`, 11/11 passing, manifest and on-disk file agreeing. **Do not rebuild
before the pending supported-host load** - doing so destroys it exactly as `179652AA...` was destroyed.

### RESOLVED 2026-08-22

`/Brepro` was added to the MSVC link options and **the build is now reproducible**. Verified by the
test this entry called for, one that forces a genuine relink rather than relying on a `cmake --fresh`
that rebuilds nothing:

| Build | Source state | Relinked | `dll_sha256` |
| --- | --- | --- | --- |
| A | committed | baseline | `E9A82DB5...` |
| B | one comment added | yes | `E9A82DB5...` |
| C | comment reverted | yes | `E9A82DB5...` |

All three identical, with both relinks confirmed by a changed file mtime. A comment turns out to be
hash-neutral as well, because comments do not affect codegen and these binaries embed no `CODEVIEW`
debug entry. Both `PreyVR.dll` and `openxr_loader.dll` now carry content-hash `TimeDateStamp` values
instead of wall-clock link times, which also closes the separate loader-reproducibility loose end
noted below — `add_link_options` reached the FetchContent-built loader too.

**The operational rule is relaxed, not withdrawn.** A recorded artifact hash is now reproducible from
the same source, so it is a meaningful identity again. Computing the hash immediately before loading
remains good practice, since it costs nothing and still catches the case where the binary on disk is
not the one you think you built.

### Original option, now taken



Adding `/Brepro` to the linker flags would replace the timestamp with a content hash and may make these
builds reproducible outright, given no PDB signature is embedded. It has not been done, because
changing the build would destroy the reference artifact again. Worth doing deliberately after the live
load, together with a test that actually forces a relink.

## F-006 - Ghidra `emulate_function` silently succeeds with all-zero registers on a bad format

- **Date:** 2026-08-16
- **Target:** `mcp__ghidra__emulate_function` against R-032 `CRenderer::GetRenderViewForThread`
- **Intent:** Test R-032's index formula by execution instead of by reading five instructions.
- **Mechanism:** `registers` was passed as `RCX=0x10000000,RDX=0x0,R8=0x0` - the obvious key-value form.
- **Result:** The call returned `success: true`, `hit_return: true`, `stop_reason: "return"`, and
  `steps_executed: 5`, which is exactly right for this five-instruction function. Every indicator said
  the emulation worked. **But the inputs were silently discarded and the emulator ran with all-zero
  registers.** Four runs across the full input matrix all returned `RAX = 0x0`, which reads naturally
  as "the pool slots are null" - a plausible, completely false finding.
- **How it was caught:** The function never writes `RCX`, so `RCX` was added to `return_registers` and
  read back. It returned `0x0` after being set to `0x180000000`. That is unambiguous proof the inputs
  were dropped rather than the memory being empty.
- **Correct format: JSON.** `{"RCX":"0x180000000","RDX":"0x1","R8":"0x1"}` applies correctly; the
  registers read back with the values supplied. A semicolon-separated variant also fails silently.
- **Rule.** Before trusting any `emulate_function` result, **include an input register that the function
  never modifies in `return_registers`, and confirm it reads back the value you supplied.** A malformed
  `registers` string does not error - it produces a confident, well-formed, entirely wrong answer. The
  same check applies to `memory`.
- **Why this belongs here.** It is the same silent-failure class as F-002's ReGenny overlay, which also
  returned plausible numbers from a misconfigured input. A tool that rejects bad input is safe; a tool
  that accepts it and answers anyway is the dangerous kind. Verify the harness before the hypothesis.
- **Recovery:** No lasting damage. The bad results were discarded before anything was recorded, and the
  re-run with JSON produced the verified matrix now recorded against R-032 and R-033.

## F-007 - Hand-rolled x86 operand scanning to find struct-field consumers

**Status:** unreliable for negatives. Use only with controls.

**What was attempted.** To find what reads `g_detachCamera` (R-056), I wrote a scanner that decodes
x86 memory operands out of `.text` and reports accesses at a given struct offset - first by raw
4-byte search, then by decoding ModRM properly, then by chaining through a base register loaded from
a global or from `[CGame+0xF8]`.

| Attempt | Result | Why it failed |
| --- | --- | --- |
| Raw 4-byte search for the offsets | 3,759 hits | `0x284` and its neighbours are common offsets in unrelated structs, and the bytes also occur as immediates and as data |
| Proper ModRM decode, `mod=10` | 3,245 hits | Correct decoding, but still every struct in the image rather than the one wanted |
| Chain through a base loaded from a global | Top hit `0x18243A688` | That is the **3DEngine cvar block** already identified in R-045; `+0x284` there is an unrelated cvar |
| Chain through `[CGame+0xF8]`, 256-byte window | **0 hits, including controls** | Real code caches the pointer far from the use, across spills and long spans |
| Chain through a cached global | 2 loads, 0 hits | The candidate global was not the widely used accessor |

**The decisive detail.** The last two attempts returned zero for the *control* cvars as well -
including `g_difficultyLevel`, which Prey unquestionably consumes. Had the controls not been in the
input, the zero for the detached-camera family would have read as a clean negative and been believed.

**Rules.**

1. **Never report a negative from this technique without controls in the same run** - a known-consumed
   field in the same struct. If the controls come back empty, the run says nothing about anything.
2. Prefer Ghidra's own xref and decompiler analysis, which resolves indirection properly. Hand-rolled
   scanning is good for questions like *is this byte pattern unique in the image*, which it answered
   correctly all session, and poor for *what reads this field*.
3. A struct offset is meaningless without its base object. Chasing an offset before pinning the
   object's address is the mistake underneath every row above.

Same family as F-006: a method that answers confidently while silently operating on the wrong input.
The defence is identical - put a known answer in, and check it comes back out.

## F-008 - x64dbg `loadlib` freezes Prey by leaving the hijacked thread suspended

**Status:** do not use. Use Frida.

**What happened.** With x64dbg attached to a running Prey and the debuggee paused,
`loadlib "D:\Dev Debug\PreyVRuild\headless\Release\PreyVR.dll"` returned success. It did not
load the DLL. The game froze, and every subsequent `pause` was refused, with x64dbg's status bar
reading *"The active thread is suspended, switch to a running thread to pause the process"*.

`loadlib` works by hijacking a thread in the debuggee to call `LoadLibraryA`. On this target it
suspended the main thread and left it suspended. The process could not be paused *through* a
suspended thread, so the debugger's own recovery paths were unavailable.

**Recovery.** Resuming the thread by id (`pause_resume_thread(tid, 'resume')`, main thread from the
session title) unfroze it, but the game had to be restarted anyway.

**Diagnostic notes worth keeping.**

- The command's return value **is** meaningful: a deliberately bogus command returned `Success: False`
  while `loadlib` returned `True`, which is how we established the command was accepted and the
  failure was inside the debuggee rather than a typo. Seeding with a known-wrong input is what made
  that distinguishable.
- `eval_expression("PreyVR.dll:0")` failing was only trustworthy because
  `eval_expression("PreyDll.dll:0")` succeeded and returned the right base. Same rule: check the probe
  against a known answer before believing its negative.
- A spaced path was the initial hypothesis and was **wrong** -- the path never got as far as mattering.

**Use Frida instead.** `LoadLibraryW` through a Frida `NativeFunction` injected on the first attempt,
`lastError=0`, with no pause and no thread hijack. Frida runs the call on its own thread, which is
exactly why it suits a live game. Cheat Engine's `inject_dll` (`CreateRemoteThread`) is the equivalent
fallback.

## F-009 - Two PreyVR exports raise `system error` when called through Frida

**Status:** partially understood. Data is reachable another way.

`PreyVR_CaptureRenderViews` and `PreyVR_SetFrameObserverEnabled` both raise `system error` when
invoked via a Frida `NativeFunction`. The trivial getters (`PreyVR_GetSmokeStatus` and friends) work
normally.

**The enable call took effect anyway** -- observer status moved `1 -> 2` and frames began counting --
so the error is raised around a call that at least partially completes. The most likely cause is
Frida's exception handler reacting to MinHook's `VirtualProtect` and write into `PreyDll.dll`'s
`.text`, rather than a fault in our code. `CaptureRenderViews` produced no log output, so it did fault
before reaching its logging.

**Consequence, and the more serious half.** `PreyVR_SetFrameObserverEnabled(0)` raised the same error
twice and **did not disable the observer**: status stayed `2` and the prologue at `+0xF7E210` remained
`E9 BF 2D 07 FF ...` (MinHook's `JMP rel32`) instead of being restored to
`40 57 48 83 EC 60 ...`. The hook is our own and functioning, and the module is pinned until process
exit by design, so this is stable rather than dangerous -- but the documented *"disable restores the
target entry bytes"* behaviour **has never been observed on a live host** and must not be described as
proven.

**Workaround used.** The render-view walk was performed directly in Frida JS against the same offsets,
which produced better data than the export would have (raw values, full control, and the residual as a
number). Where an in-process export is awkward to call, reproducing its reads externally is often
cheaper than debugging the call path.

## F-010 - `XR_CURRENT_API_VERSION` is rejected by the installed runtime

**Status:** understood, and the workaround is one line. Found before it could cost a live session.

The project pins OpenXR-SDK `1.1.60`, so `XR_CURRENT_API_VERSION` expands to **1.1.60**. Passing that
as `XrApplicationInfo::apiVersion` makes `xrCreateInstance` fail against this machine's runtime:

```
attempt api_version=1.1.60 result=XR_ERROR_API_VERSION_UNSUPPORTED
attempt api_version=1.0.60 result=ok
runtime name="VirtualDesktopXR" version=1.0.10
```

`VirtualDesktopXR 1.0.10` implements OpenXR **1.0** and refuses a 1.1 instance outright. The loader
reports this only as `xrCreateInstance failed`; the useful code is in the return value.

**Fix:** request `XR_API_VERSION_1_0` rather than `XR_CURRENT_API_VERSION`, or try newest-first and
fall back. `preyvr_xr_adapter_probe` does the latter and prints which version was accepted, so the
answer is re-measured on whatever machine it runs on instead of being hardcoded from this one.

**Why this was nearly expensive.** Every other OpenXR precondition looked green: the preflight
reported `status=ready`, the loader is present, x64, and exports `xrGetInstanceProcAddr`, and the
runtime advertises 31 extensions including `XR_KHR_D3D11_enable`. None of those checks touch
`xrCreateInstance`, so the first real XR call in the project would have failed on a headset-day
session with a message pointing at nothing in particular. It cost nothing to find here because the
probe runs out of process, with no Prey and no headset.

**Two smaller lessons, both now fixed in the probe.** The loader refuses `xrResultToString` without a
live `XrInstance` -- exactly the case where a failure most needs naming -- so a fallback table for the
common negative results is worth the twenty lines. And a probe that gives up on its first failure
wastes the run: enumerating the adapters anyway is free and is half the answer.

### F-010 addendum, 2026-08-30 — the fallback validated against a second runtime

`xr-sim` **accepts** `XR_CURRENT_API_VERSION` (1.1.60), where VirtualDesktopXR
rejects it. The same unmodified `preyvr_xr_adapter_probe` binary now works
against both, and only because it tries newest-first and falls back rather than
hardcoding a version:

```
xr-sim                : attempt api_version=1.1.60 result=ok
VirtualDesktopXR 1.0.10: attempt api_version=1.1.60 result=XR_ERROR_API_VERSION_UNSUPPORTED
                         attempt api_version=1.0.60 result=ok
```

Worth recording because the fallback was written as a reaction to a single
runtime, and a one-runtime reaction is indistinguishable from a workaround until
a second runtime disagrees in the other direction. Had the probe been "fixed" by
hardcoding 1.0 — the smaller change, and the tempting one — it would have worked
on this machine's headset and been wrong the moment it met xr-sim. See
`docs/XRSIM_INTEGRATION.md`.

---

## F-011 - Two effects measured in one image: the frustum shear buried the eye offset

**Status:** understood and fixed. The bug was mine, in the test design, not in the code under test.

The first two A2 runs produced a stereo pair that could not be judged. The numbers looked
emphatic -- mean absolute difference 30.18, 81.7% of pixels changed, against an A1 noise floor of
0.0167 -- and they were useless, because they measured two independent things at once and could not
say how much of the difference came from either.

`SetSyntheticStereo` builds a deliberately asymmetric per-eye frustum (outer 55 degrees, inner 45)
so the asymmetry path gets exercised rather than sitting untested until a headset arrives. That is
worth doing. Doing it *in the same image* as the 64 mm eye separation is not: the asymmetry moves
every pixel sideways by a constant, and it swamped the thing the test existed to measure.

**How much:** a horizontal shift scan finds a single uniform offset of **-462 px** that drops the
residual from 30.18 to 9.34. Roughly 69% of the difference was shear.

**The magnitude was predictable, which is what makes the explanation trustworthy.** The frusta span
`tan(55) + tan(45) = 2.428` tangent units across 2560 px, and the eyes' centres differ by
`tan(55) - tan(45) = 0.428` of that, so `0.428 / 2.428 * 2560 = 451 px` predicted against 462 px
measured -- 2.4% off, from a calculation done before the scan was run.

**Two fixes, and they are different in kind.**

`SetStereoAsymmetry(outerScale)` lets the asymmetry be dialled to 1.0, making the frusta symmetric so
the eye offset is the only difference between the images. That fixes *this* test.

`FindHorizontalShift` in `preyvr::framedump` fixes the class. `Compare` answers "do these differ",
which a shear and real parallax both satisfy loudly. The scan asks whether **one** offset re-aligns
the pair: a shear says yes, parallax says no, because disparity varies with depth. Without that, the
two are indistinguishable from the summary numbers, and "the images differ a lot" reads like success.

**A first attempt at the arithmetic was also wrong, in a way worth recording.** The initial estimate
of the shear was ~256 px, from `25.6 px/degree * 10 degrees`. That treats the projection as linear in
angle; it is linear in *tangent*. The error is 45%, it is invisible unless the prediction is checked
against a measurement, and it would have made the shear look like a partial explanation rather than
the dominant one. The scan is what caught it.

**Lesson.** A measurement that cannot attribute its result to a cause is not evidence, however large
the number is. Both effects were expected, both were real, and the run still had to be thrown away.
When two effects can appear in one instrument, either separate them in the experiment or build the
instrument that can separate them afterwards -- and prefer the first, because it is cheaper and it
does not depend on the second being correct.

The zero-IPD control in A2b comes from the same reasoning: with symmetric frusta and no eye offset
the two eyes are literally the same camera, so that row must collapse to the noise floor. It is the
row that fails if the difference being measured is really temporal AA or a scene that is not as
frozen as assumed.

---

## F-012 - A test harness that "failed" four times while measuring nothing

**Status:** understood and fixed. Same class as F-011, caught faster because F-011 taught the shape.

The first run of the controller aim check reported four failing cases. All four were real numbers, all
four disagreed with the prediction, and not one of them measured anything: the commands that were
supposed to establish the ground truth never reached the simulator, so every case read back xr-sim's
untouched default pose.

**The tell was in the output and is worth learning.** All four cases returned the *identical*
direction `(0.0000, 0.7660, -0.6428)`. Four different commanded rotations cannot produce one identical
result; a constant answer across varied inputs means the input never arrived. The harness now says so
explicitly rather than leaving it to be noticed.

**Two causes, both in the harness.**

xr-sim logs `ignoring a command.txt written before this run started` -- it deliberately refuses stale
commands, which is correct behaviour and defeats any scheme that writes the command file before
launching.

Worse, the liveness check was satisfied by a `state.json` **left behind by the previous case**. So the
harness confirmed a live session, sent commands, and had them acknowledged by a directory rather than
a process. Deleting the state file before launch is what makes its reappearance mean something.

**Fixes.** `state.json` is removed before the run so staleness is impossible; all cases now run inside
one session; and every controller reading is stamped with the frame it came from, so a reading taken
before a command cannot be attributed to it. The correlation is by frame number, not by ordering.

**A third thing this exposed, in the probe rather than the harness.** The first version filled only
`positionTracked` and `orientationTracked` on `PoseValidity`, leaving `positionValid` and
`orientationValid` false, so `AimFromController` refused every pose. That was the module behaving
*correctly* -- it fail-closed on a pose that claimed to be untracked -- and it looked like a bug in the
module for exactly as long as it took to read the four field names. Valid and tracked are different
bits in OpenXR with different meanings, and the probe now maps all four from the runtime's own flags.

**Lesson, and it is the F-011 lesson again from the other side.** F-011 was a measurement that could
not attribute its result to a cause. This was a measurement with no cause at all. Both produced
confident numbers. The defence is the same in both cases: know what the result should be *before*
running, and make the harness prove it actually did the thing it claims to have done -- here, by
stamping every reading with the frame that produced it.

---

## F-013 - Re-entering CSystem::Render works once and wedges the engine when sustained

**Status:** understood well enough to stop doing it. A3's question is answered, and the answer is no.

The double render calls the original `CSystem::Render` twice inside one invocation, once per eye.
Run live 2026-09-01:

| budget | result |
| --- | --- |
| 1 frame | **works.** `done=1`, budget exhausted cleanly, game continued at ~142 fps, restore verified |
| 300 frames | **wedges.** One frame logged, ~4 completed, then the observed frame count stopped advancing and never resumed |

**The symptom names the cause.** The level disappeared and only the skybox remained. The skybox is
drawn unculled, so "everything except the skybox is gone" is occlusion culling rejecting all world
geometry -- not a crash, and not a camera that pointed the wrong way.

That is consistent with what the pipeline is. `CSystem::Render` fills a `CRenderView` and the
coverage buffer, both per-frame structures that are filled once and consumed once. Re-entering it
without an intervening frame boundary -- no `RT_EndFrame`, no present -- leaves the second pass
culling against state the first pass already consumed. One re-entry survives because nothing has
been consumed yet. Sustained re-entry does not, because every frame now starts from state the
previous frame corrupted.

**The frame budget did not save it, and that is the design lesson.** The budget bounds how many
frames the mode *attempts*, and it self-disarms when the budget reaches zero. But the engine wedged
at roughly frame 4 of 300, so no further frames completed, so the budget was never consumed and the
mode never disarmed itself. Disarming by hand afterwards did not recover it either: the damage was
already in engine state, not in our flag.

**A budget expressed in frames cannot bound a failure that stops frames from completing.** A time
budget serviced from a thread that is not the render thread would have. That is the fix if this is
ever retried.

**Two leads, both already in the project, and both better founded than another attempt at re-entry.**

`e_CoverageBufferDebugFreeze` and `e_CameraFreeze` are already on the console allowlist, recorded as
"the engine-honoured override found in UpdateRenderingCamera: together they switch the render camera
to GetViewCamera() while freezing culling". Culling is precisely the subsystem this failure
implicates, so the next experiment is the double render with culling frozen -- and it needs no policy
change to run.

`e_Recursion` is also allowlisted, noted as "the recursive render views are allocated and idle".
CryEngine renders mirrors and portals through recursive render views, which is the engine's *own*
mechanism for drawing the world more than once in a frame. That is a far more promising architecture
for native stereo than re-entering the top-level render function, and it is the direction to take
next.

**Cost:** one hung game session, killed from outside. No writes to the installed game, and
`restoreFailures` stayed 0 throughout -- the camera was always restored byte for byte. The damage was
entirely in the engine's own per-frame state.
