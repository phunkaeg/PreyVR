# Research log

## 2026-07-30 — Baseline established

- **Evidence:** Installed release enumerated at the path in [`BUILD_BASELINE.md`](BUILD_BASELINE.md); both launch and engine module hashes recorded.
- **Evidence:** Ghidra import and auto-analysis completed for `PreyDll.dll` at `/Prey/PreyDll.dll`.
- **Interpretation:** The launcher is too small to own most game/renderer logic. Begin static reconnaissance in `PreyDll.dll`, while treating DXGI/D3D11 acquisition as a runtime question.
- **Next question:** Determine the renderer API, graphics-device lifecycle, and whether CryEngine exposes a re-entrant multi-view/stereo path before pursuing per-draw duplication.

## 2026-07-30 — Initial rendering and input anchors

- **Evidence:** `PreyDll.dll` imports `D3DDisassemble` and `D3DReflect` from `D3DCOMPILER_47.DLL`; it has no direct `D3D11CreateDevice` or DXGI import-table entry.
- **Evidence:** A read-only inspection of `.rdata` at `PreyDll.dll+0x1D93408` found the contiguous ASCII loader names `CreateDXGIFactory1`, `dxgi.dll`, `D3D11CreateDevice`, and `d3d11.dll`. The direct Ghidra xref query currently finds no code reference to the individual strings, so the owning initialization routine is not identified yet.
- **Evidence:** The same module directly imports `DirectInput8Create`, `XInputGetState`, `XInputSetState`, and `XInputGetCapabilities`; the installed root `system.cfg` selects the `GameSDK` game folder, and `GameSDK/game.cfg` exposes gameplay camera/UI cvars including `pl_cameraNearZ` and `g_reticleYPercentage`.
- **Interpretation:** A D3D11/DXGI renderer route is strongly indicated and appears to use a dynamic-loader path. This is a renderer-family finding, not yet a Present/device/hook finding. Existing XInput support gives a native input seam to investigate, but does not establish the gameplay action layer.
- **Next question:** Resolve the function or table that consumes the dynamic-loader strings, then validate the actual device/swapchain and frame boundary in a read-only live trace.

## 2026-07-31 — First live renderer observation

- **Evidence:** Frida enumerated the running `Prey.exe` process and confirmed `PreyDll.dll`, `dxgi.dll`, `d3d11.dll`, `D3DCOMPILER_47.dll`, and `nvapi64.dll` were loaded. No OpenXR loader was present.
- **Evidence:** The out-of-process `preyvr_dxgi_probe` resolved this Windows build's `IDXGISwapChain::Present` implementation to `dxgi.dll+0xDAD0` and `ResizeBuffers` to `dxgi.dll+0x388C0`.
- **Interpretation:** The live renderer is D3D11/DXGI. `dxgi.dll+0xDAD0` is a temporary operating-system-build trace anchor, not a shipping Prey address and not suitable as a distributable signature.
- **Failure:** An in-process Frida CModule callback experiment at Present ended in a `0xC0000005` crash in an unknown/freed-code region. See [`FAILURE_REGISTRY.md`](FAILURE_REGISTRY.md). Do not repeat that sampling mechanism.
- **Next question:** Capture the Present caller with Cheat Engine's non-breaking logging hardware breakpoint at `dxgi.dll+0xDAD0`, including registers and stack, then map the first `PreyDll.dll` frame back into Ghidra.

## 2026-07-31 — ReGenny and headless baseline

- **Evidence:** ReGenny attached read-only to the live `Prey.exe` process and successfully read the mapped `PreyDll.dll` PE header. The project-local type notebook is `regenny/PreyVR.genny`.
- **Evidence:** The first headless Release run built from a clean `build/headless` directory and passed all three CTest targets: `vr_math`, `dxgi_method_probe`, and `build_doctor` in 1.06 seconds of test time.
- **Interpretation:** Pure pose/input policy, binary compatibility checks, and DXGI method discovery can stay out of the headset loop. Live tests should be reserved for ownership, timing, lifecycle, and visual evidence that cannot be represented by deterministic fixtures.

## 2026-07-31 — Renderer frame boundary resolved

- **Evidence:** Cheat Engine's non-breaking hardware breakpoint at the locally resolved `IDXGISwapChain::Present` entry captured a stable swapchain pointer with `SyncInterval=1` and flags `0`. The raw stack repeatedly contained return address `PreyDll.dll+0xFE9D27`.
- **Evidence:** The preceding instruction at `PreyDll.dll+0xFE9D21` dispatches vtable slot `0x8D0` on the module-global singleton pointer at `PreyDll.dll+0x2B3E8E0`. The live slot resolved to `PreyDll.dll+0xF7E210`.
- **Evidence:** `PreyDll.dll+0xF7E210` directly references the diagnostic `EndScene without BeginScene`. Its adjacent slot `0x8C8` resolves to `PreyDll.dll+0xF7D710`, the paired BeginScene path. A simultaneous one-second CE capture recorded 103 EndScene entries and 109 Present entries. All logging breakpoints were removed successfully; no process memory was changed.
- **Evidence:** Ghidra now labels the paired functions `BeginRendererScene` and `EndRendererScene` with evidence comments. The exact-build prologues and EndScene dispatch bytes are enforced by the headless build doctor.
- **Evidence:** Inside `EndRendererScene`, `PreyDll.dll+0xF7E48A` loads `renderer+0xAE88` and calls COM vtable slot 8. The live pointer exactly matched Present's `RCX`, identifying the field as `IDXGISwapChain*`. The DXGI-failure branch at `PreyDll.dll+0xF7E4DA` loads `renderer+0xAF28` and calls vtable slot 39, identifying the field as `ID3D11Device*` through `GetDeviceRemovedReason`. ReGenny found mirror fields at `+0xAFA8` and `+0xAF98`.
- **Interpretation:** `EndRendererScene` is the first durable engine frame-boundary candidate and exposes the real game-owned swapchain and device needed by `XR_KHR_D3D11_enable`. It is not yet approved for a shipping detour: thread ownership, re-entrancy, immediate-context acquisition, and clean passthrough still require bounded validation.
- **Capture:** [`../captures/traces/2026-07-31-prey-endscene-present-cadence.md`](../captures/traces/2026-07-31-prey-endscene-present-cadence.md)
- **Next question:** Acquire the immediate context through the live device and prove a no-op EndScene observation can initialize and shut down cleanly before adding OpenXR state.

## 2026-07-31 — PDB bridge and native camera/aim split

- **Prior-art boundary:** The public fholger discussion located so far contains a stereo-rendering proof of concept and his explanation that independent gun aiming was the blocker, but no public source/research dump was found. Treat that as an absence in the searched public record, not proof that no private notes ever existed.
- **Evidence:** Chairloader's generated `Common/Prey` headers come from the released PDB for a canonical EGS build. Its version database lists the exact installed Steam hash and an official EGS conversion diff. A temporary canonical image was reconstructed and hash-verified as `0485C85B…99F0A63`, then imported under `/Prey/reference/PreyVR-PreyDll-EGS-0485c85b.dll`. The installed game remained untouched.
- **Evidence:** Cross-build signature mapping resolved Steam `ArkPlayerCamera::UpdateView` at RVA `0x148B820`, `ArkPlayerCamera::SetCustomViewFunction` at `0x1456460`, and the ArkPlayer wrapper at `0x15CA580`. A live entry hit showed `RCX = ArkPlayer+0x12A0` and `RDX = SViewParams*` with plausible position, quaternion, near plane, and FoV fields. The camera's custom callback pointer at `+0x148` was currently null. Static control flow calls that callback with `SViewParams&` and skips the normal camera-mode branch when installed.
- **Evidence:** Steam `ArkPlayer::UpdateCachedReticleViewPosAndDir` mapped to RVA `0x1585320` and hit immediately in live gameplay. It unprojects the independent `Vec2` at ArkPlayer `+0x17EC` into a cached world origin at `+0x17D4` and normalized direction at `+0x17E0`.
- **Experiment:** While stopped at the updater with the camera matrix frozen, only reticle X was changed from `0.500` to `0.625`. The cached direction changed from approximately `(-0.946669,-0.245525,-0.208650)` to `(-0.968890,+0.156329,-0.191867)`. The original 32-byte origin/direction/reticle block was restored and verified before the breakpoint was cleared and execution resumed.
- **Evidence:** The PDB-mapped Steam functions `CArkWeapon::GetReticleInfoForFiring` (`0x1694890`), `GetReticlePosition` (`0x1694A20`), and `FindIronsightsTarget` (`0x16930A0`) all consume the same cached ray for the local player. `GetReticleInfoForFiring` constructs a point along the ray and performs a physics query returning `{IEntity*, Vec3 position}`. This closes the main feasibility question left by the earlier prototype: Prey already has a camera-independent aim representation reaching native weapon logic.
- **Interpretation:** We do not need to invent a parallel aiming simulation. The likely implementation is HMD pose through the native `SViewParams` camera callback and controller aim through the reticle/ray producer, followed later by a separate viewmodel transform. The live shot-impact proof and safe callback ABI/lifecycle prototype remain mandatory.
- **Regression:** The complete headless Release loop passed 3/3 tests after adding the new landmarks. The build doctor reported 17 passes, 0 warnings, and 0 failures; the staged Graphify corpus now contains 11 PreyVR documents.
- **Capture:** [`../captures/traces/2026-07-31-prey-detached-aim-probe.md`](../captures/traces/2026-07-31-prey-detached-aim-probe.md)
- **Next question:** Observe `CArkWeapon::GetReticleInfoForFiring` during an actual shot, record the returned entity/position, then repeat with a bounded synthetic reticle offset while keeping the camera fixed.

## 2026-07-31 — Inert smoke DLL and replayable aim evidence

- **Implementation:** Added `PreyVR.dll` version `0.1.0-research-smoke`. Its worker logs DLL/host identity, validates the exact mapped `PreyDll.dll` landmarks, exports `PreyVR_GetSmokeStatus`, and then remains inert. It contains no detour library, renderer mutation, or OpenXR call. The gate was subsequently extended from 14 to 18 landmarks by the wrench-query trace below.
- **Safety:** A separate test host loads the real DLL without Prey present. The DLL reports `unsupported` because `PreyDll.dll` is absent, confirms `hooks=disabled`, and unloads cleanly. The in-game supported-build path has not been run while the user is away.
- **Regression fixture:** The original and shifted 32-byte ArkPlayer reticle blocks from the reversible probe are now compiled into a deterministic test. They decode into the typed `CachedReticleState`, preserve screen X `0.500 -> 0.625`, and reproduce a direction change inside the measured 20–30 degree envelope.
- **Verification:** At this milestone, Release configure/build and all seven then-existing CTest targets passed, including the read-only smoke-log parser. The build doctor reported 20 passes, 0 warnings, and 0 failures; the later wrench-query milestone supersedes these counts. The staged Graphify corpus contained 12 PreyVR documents.
- **Next question:** Perform one inert in-game load and inspect `Documents\PreyVR\PreyVR.log` for the current exact landmark count and `status=verified`; only after that should a no-op EndScene observer be introduced.

## 2026-07-31 — Native wrench query follows the detached ray

- **Evidence:** Temporary x64dbg hardware breakpoints followed a primary wrench input through `ArkWeaponWrench::OnActionAttackPrimary` (`0x16B1FD0`), animation impact `ArkWeaponWrench::OnHit` (`0x16B2BC0`), and `ArkWrenchComponent::GetHits` (`0x13BD620`). The live weapon was `0x271F157DB50`; its wrench component was at `+0x4C8`.
- **Evidence:** Paired decompilation proves `GetHits` obtains origin and direction through `IArkPlayer::GetReticleViewPositionAndDir`, then constructs the melee broadphase and `RayWorldIntersection(Game)` queries from that ray. The component's live range was `2.75`.
- **Evidence:** The completed wall query returned four `0x50`-byte `ray_hit` records. Their raw `0x140` bytes and the `0x58`-byte component snapshot are now a typed deterministic fixture; nearest contact was `0.531901` units away.
- **Interpretation:** Firearm and wrench paths share Prey's native camera-independent gameplay ray. Controller aim can therefore target the existing action systems rather than reproduce melee collision in the mod. A fixed-camera synthetic before/after impact is still required for A0b.
- **Safety:** All x64dbg hardware breakpoints were cleared and Prey resumed normally. No code or persistent gameplay state was modified.
- **Verification:** The Release build passes all eight CTest targets, including `wrench_query_fixture`. The engine map and inert DLL gate now contain 18 exact runtime landmarks; the build doctor reports 24 passes, 0 warnings, and 0 failures.
- **Capture:** [`../captures/traces/2026-07-31-prey-wrench-hit-query.md`](../captures/traces/2026-07-31-prey-wrench-hit-query.md)
- **Next question:** Run the bounded A0b synthetic-ray experiment against a static target, then proceed to the first no-op frame observer and rudimentary OpenXR bootstrap.

## 2026-07-31 — A0b reduced to a stack-local two-swing test

- **Static evidence:** Steam `ArkWrenchComponent::GetHits` calls `IArkPlayer::GetReticleViewPositionAndDir` at RVA `0x13BD6F3` with a six-float destination at `[RSP+0x58]`. Immediately afterward, the copied direction occupies `[RSP+0x64..0x6F]`. The caller returns from `GetHits` at `0x13BFA9B` and has loaded the result vector begin/end by `0x13BFAA9`, before hit effects are processed.
- **Safety improvement:** The synthetic A0b pass can rotate only the twelve-byte direction in the current function's stack frame. It no longer requires writing or restoring ArkPlayer's persistent cached ray, and cannot change the camera or UI reticle fields.
- **Implementation:** `MakeBoundedYawProbe` creates a Z-up yaw while preserving origin and screen state, rejects magnitudes over 15 degrees, and emits exactly twelve little-endian direction bytes. For the captured downward-pitched ray, the selected 10-degree azimuth change is a `9.0643`-degree 3D ray change and has a nominal equal-distance endpoint separation of `0.4346` world units at the observed 2.75 range.
- **Protocol:** [`LIVE_A0B_WRENCH_PROTOCOL.md`](LIVE_A0B_WRENCH_PROTOCOL.md)
- **Next question:** Capture unchanged and 10-degree result vectors in one stationary session and promote the pair into the headless fixture.

## 2026-07-31 — Native interaction selector mapped offline

- **Source bridge:** The reconstructed EGS PDB image and generated headers from [Chairloader](https://github.com/thelivingdiamond/Chairloader) supplied canonical class/member order and function RVAs. Every promoted Steam address was then established by a unique byte pattern and matching control flow; no EGS RVA was reused directly.
- **Layout evidence:** Steam `ArkPlayer::Update` at `0x1584FE0` forms `ArkPlayer+0xAC8` and calls `ArkPlayerInteraction::Update` at `0x1594E20`. That object owns `ArkPlayerTargetSelector` at `+0x190`, so the selector is ArkPlayer `+0xC58`; native usable entity id is ArkPlayer `+0xF34`.
- **Selection evidence:** `ArkPlayerTargetSelector::UpdateCandidates` at `0x159A660` obtains `IArkPlayer::GetReticleViewPositionAndDir`, scales the copied direction by its interaction distance, and performs Prey's native ray/broadphase/line-of-sight candidate work. The transient post-getter direction is exactly twelve bytes at `[RSP+0x5C..0x67]`; candidate records are `0x70` bytes with entity id at `+0x68`.
- **Execution evidence:** `ArkPlayerInteraction::Interact` at `0x1593690` reads the usable entity, tests the per-mode `ArkInteractionInfo`, and calls native `PerformInteraction` at `0x1593980`. The mod should steer selection and retain this execution path.
- **Viewmodel lead:** EGS `CArkWeapon::SetupAttachment` resolves the configured `sAttachmentName`, and the unique Steam match for `CArkWeapon::AttachToHand` is `0x16914F0`. Paired Steam decompilation confirms it binds the equipped weapon through the `IAttachment*` at weapon `+0x2B0`. This identifies the ownership layer but not yet a safe late-frame transform override.
- **Implementation:** Added three exact interaction signatures and typed member/record offsets to the fail-closed engine map. The inert DLL now validates 21 landmarks. Added a no-button stack-local interaction A0 protocol and a `preyvr_ray_probe` helper that emits tested little-endian direction bytes from live floats.
- **Verification:** The clean Release headless loop passes all eight CTest targets. The build doctor independently matches all 21 promoted signatures in the installed Steam DLL and reports 27 passes, 0 warnings, and 0 failures. The ray helper reproduced the prior captured direction's bounded +10-degree probe and emitted a twelve-byte payload.
- **Runtime status:** Prey and x64dbg were closed throughout this pass. All findings in this section are static-only until the changed-use-target protocol is captured live; no game memory was accessed or modified.
- **Next question:** Complete wrench A0b, then run the no-button interaction A0 pair and promote its candidate/usable-entity results into a captured headless fixture.

## 2026-08-01 — Wrench detached-aim A0b passed

- **Baseline:** With the player stationary against a broad wall, the unmodified stack-local ray was origin `(781.590454, 1569.453735, 17.084219)`, direction `(-0.108836, -0.983913, -0.141668)`. `ArkWrenchComponent::GetHits` returned one `0x50`-byte hit on collider `0x2231174EDF0`, distance `0.696134`, at `(781.514709, 1568.768799, 16.985590)`.
- **Synthetic pass:** The second swing's actual ray was captured before mutation. Its origin differed from baseline by only `0.004195` units and its normalized direction by `1.231205°`. The tested helper applied a relative positive 10-degree Z-up yaw, producing direction `(0.070487, -0.990096, -0.121412)` and exact bytes `AA 5B 90 3D F3 76 7D BF B1 A6 F8 BD`.
- **Safety:** Only the twelve direction bytes at `[RSP+0x64..0x6F]` in that `GetHits` stack frame were written and read back. No camera, code, heap, UI-reticle, or persistent ArkPlayer memory was changed. The write expired with the stack frame; all hardware breakpoints were cleared and Prey resumed normally.
- **Result:** The same collider and wall normal remained at distance `0.695485`. The contact moved to `(781.640869, 1568.768677, 17.001547)`: delta `(+0.126160, -0.000122, +0.015957)`, or `0.127165` units total. The new point equals `origin + writtenDirection * distance` within float tolerance.
- **Interpretation:** This is direct live proof that controller-owned direction can steer Prey's native melee collision independently of the camera. H-004 and test stage A0b pass. It does not yet prove projectile or use-target behavior, nor visual weapon placement.
- **Headless promotion:** Both raw hit records were added to `wrench_query_fixture`, which now asserts the same collider/depth, lateral displacement, and endpoint geometry.
- **Capture:** [`../captures/traces/2026-08-01-prey-wrench-a0b.md`](../captures/traces/2026-08-01-prey-wrench-a0b.md)
- **Next question:** Run the no-button interaction A0 changed-target test, then begin the inert frame observer/OpenXR bootstrap while continuing the viewmodel attachment trace offline.

## 2026-08-01 — Native interaction detached-aim A0 passed

- **Baseline:** At the selection edge of a keypad, ArkPlayer remained `0x223EF1FCB20` and selector `0x223EF1FD778`. The unmodified native candidate vector contained two `0x70`-byte records, both for keypad entity `0xFDE0`; the usable entity at ArkPlayer `+0xF34` was also `0xFDE0`.
- **Useful null:** A bounded +15-degree edit propagated into the exact 2.5-unit native-query vector and reduced the candidate vector from two records to one, but retained selected and usable entity `0xFDE0`. This directly proved candidate-set sensitivity without yet changing the committed target.
- **Changed target:** On the opposite -15-degree pass, the actual source direction was `(-0.225410, -0.917259, -0.328370)`. Exact bytes `33 07 E9 BE C4 E1 53 BF 1F 20 A8 BE` produced direction `(-0.455133, -0.827664, -0.328370)`. The native scaled vector was `(-1.137833, -2.069159, -0.820925)`, the sole candidate became entity `0x1117`, and the interaction update committed usable entity `0x1117`.
- **Safety:** Only twelve bytes in the current `UpdateCandidates` stack frame were written and immediately read back. Persistent ArkPlayer ray bytes stayed unchanged within each pass; camera, heap, code, UI-reticle state, and action execution were untouched. All breakpoints were cleared and Prey resumed normally.
- **Interpretation:** This is direct live proof that controller-owned direction can steer Prey's native highlight/use selection independently of the camera while preserving `ArkPlayerInteraction::Interact` as the action path. Changed-melee-impact and changed-use-target now both pass; projectile runtime behavior and visual weapon ownership remain.
- **Headless promotion:** Added a typed candidate-record decoder and `interaction_query_fixture` covering the exact baseline, useful-null, changed-target, direction-byte, scaled-query, and truncation cases.
- **Verification:** The expanded clean Release headless loop passes all nine CTest targets, including the new interaction fixture and the existing build doctor/DLL/DXGI safety lanes.
- **Capture:** [`../captures/traces/2026-08-01-prey-interaction-a0.md`](../captures/traces/2026-08-01-prey-interaction-a0.md)
- **Next question:** Start the inert frame observer/OpenXR bootstrap, while tracing the late-frame weapon/viewmodel transform and reserving a later projectile-direction proof.

## 2026-08-01 — Default-off frame observer and OpenXR preflight passed

- **Static evidence:** Ghidra decompilation confirmed `EndRendererScene` at `PreyDll.dll+0xF7E210` has the hookable `void(renderer*)` ABI. Observer planning is exact-signature gated and rejects a changed prologue, truncated image, null module base, or address overflow.
- **Implementation:** `PreyVR.dll` advanced to `0.2.0-observer-bootstrap` with pinned MinHook `v1.3.4` and OpenXR loader `1.1.60`. Startup still installs nothing: it plans a default-off observer and checks the active runtime manifest plus adjacent loader architecture without loading or calling OpenXR.
- **Supported-host load:** All 21 landmarks matched in live mapped memory. OpenXR preflight found the HKLM64 Virtual Desktop runtime manifest and the packaged x64 loader with `action=none`.
- **Observer result:** The explicit enable export installed one `EndRendererScene` trampoline. The callback observed renderer `0x7FFE20544E80` on thread `780692`, reached the bounded 120-hit milestone, and was explicitly disabled after 301 callbacks.
- **Correction:** The first trial revealed that telemetry reset occurred immediately after publishing the hook, allowing a duplicate `count=1` line. Initialization was moved before `MH_EnableHook`; the full 10/10 headless loop and live proof were repeated with exactly one first-hit event.
- **Safety:** Disable removed the hook and uninitialized MinHook. Target bytes exactly matched `40 57 48 83 EC 60 83 B9 F8 AE 00 00 00 48 8B F9`; all debugger breakpoint lists were empty; `FreeLibrary` removed the DLL; and Prey remained running.
- **Verification:** The corrected Release loop passes all ten CTest targets. Artifact SHA-256 is `8FDF164B2EC566E5E5E0AB2D22DF1A17F2B81B547C17818DFCA71A3CE5523150`.
- **Capture:** [`../captures/traces/2026-08-01-prey-frame-observer-smoke.md`](../captures/traces/2026-08-01-prey-frame-observer-smoke.md)
- **Next question:** Add a default-off, fully reversible OpenXR instance/system-only smoke test. Do not create a session or bind D3D11 until instance lifecycle and desktop preservation pass.

## 2026-08-01 — No retained engine stereo path (H-001 stereo half)

- **Method note:** `PreyDll.dll` has defined strings only in the PE export-name region, so `search_strings` and `get_xrefs_to` return nothing for engine literals — the same limitation already recorded for R-001. This pass used raw byte-pattern search over the whole 48 MB image. A `"Enables "` control query returned 152 untruncated hits, confirming the small counts below are real totals rather than a capped head.
- **Evidence:** `Stereo` occurs exactly 7 times in the entire image and `stereo` exactly 3 times. Every occurrence is accounted for: two Flow Graph node names (`Stereo:ReadStereoParameters`, `Stereo:StereoParameters`), `r_VolumetricCloudsStereoReprojection`, `sys_flash_stereo_maxparallax`, two help strings, and two audio channel configurations (`Stereo`, `Auro_222_Stereo`).
- **Evidence:** Zero matches for `r_Ster`, `StereoMode`, `Oculus`, `OpenVR`, `OSVR`, `SteamVR`, `Vive`, `HMD`, `Hmd`, `LeftEye`, and `RightEye`. `RenderView` appears once, in the job name `JobRenderViewPostWrite` at `0x181DCAF7B`.
- **Decisive contradiction:** The surviving `Stereo:StereoParameters` node's port names sit adjacent in `.rdata` at `0x181CD4E8C`: `CurrentEyeDistance`, `CurrentScreenDistance`, `CurrentHUDDistance`. In stock CryEngine those write `r_StereoEyeDist`, `r_StereoScreenDist`, and `r_StereoHudScreenDist`. None of those cvars exist here. The node registration survived; its cvar targets were stripped.
- **Interpretation:** Arkane shipped Prey with CryEngine's stereo device/output layer (`CD3DStereo` and the CryVR plugin set) removed. The remaining stereo strings belong to subsystems that register their own cvars independently. There is no retained engine stereo mode for the mod to enable, so the per-eye route must be mod-owned. This is a genuine narrowing of the design space, not a blocker: it removes the cheapest hypothetical route before effort is spent on it.
- **Scope limit:** This resolves the stereo half of H-001 only. Whether `CRenderView` retains more than one `SRenderViewInfo` is a structural question that literal search cannot answer and remains open.
- **Safety:** Read-only. No process attached, no memory written, no Ghidra annotation created or saved.
- **Capture:** [`../captures/traces/2026-08-01-prey-stereo-path-recon.md`](../captures/traces/2026-08-01-prey-stereo-path-recon.md)
- **Next question:** Determine `CRenderView`'s view-info count structurally, then compare a mod-owned scene re-entry at the `BeginRendererScene`/`EndRendererScene` boundary against direct view-info population.

## 2026-08-01 — Full Ghidra analysis pass; R-001 resolved and device creation mapped

- **Database delta:** A full auto-analysis pass took `/Prey/PreyDll.dll` from 79,306 to 86,434 functions, 80,393 to 124,871 symbols, and 42 to 1,101 data types. Defined strings and xrefs now resolve for engine literals.
- **Caveat — string coverage is still partial:** `search_strings` finds 4 `Stereo` hits where exhaustive byte search finds 10; the Flow Graph node names and audio literals were never promoted to defined strings. The preceding stereo finding was established by byte search and is unaffected, but string search alone must not be treated as exhaustive on this image.
- **Caveat — two annotations were lost:** `BeginRendererScene` (`0x180F7D710`) and `EndRendererScene` (`0x180F7E210`) reverted to `FUN_` names while all 24 `Ark*`/weapon/camera annotations survived. [`GHIDRA_SYNC.md`](GHIDRA_SYNC.md) still records the renderer renames as applied; they need re-applying.
- **Evidence:** `CreateDXGIFactory1` at `0x181D93408` has exactly two code references — `0x180F50057` inside `FUN_180F50000` (RVA `0xF50000`) and `0x180D87761` inside `FUN_180D87710`. This closes R-001's "no code xref yet" gap.
- **Evidence:** `FUN_180F50000` decompiles as the device/adapter/swapchain creator. It dynamically loads `dxgi.dll` and `d3d11.dll`, creates the factory, walks `EnumAdapters1` until `DXGI_ERROR_NOT_FOUND`, and creates the device through one of two vendor-extension paths or plain `D3D11CreateDevice` at feature level `0xB000` with `D3D11_SDK_VERSION` 7. All COM vtable slot arithmetic was checked against the SDK layouts.
- **Evidence:** Decompiling `EndRendererScene` re-confirmed R-006 and R-007 by the same slot arithmetic and exposed the Present argument producers at `renderer+0xB1EC` (vsync branch selector), `+0xB1C0`/`+0xB1C4`, and `+0xB1F0` (Present flags). No per-eye or per-view iteration exists in the function; the only doubled structure is render-thread command double-buffering at `+0xAC30`.
- **Interpretation:** `XR_KHR_D3D11_enable` requires the app device to sit on the adapter LUID reported by `xrGetD3D11GraphicsRequirementsKHR`, so R-025's `EnumAdapters1` loop is where adapter agreement would have to be enforced if the active runtime ever demands a non-default adapter. This is recorded so the OpenXR lane does not rediscover it; nothing here is proposed as a hook yet.
- **Safety:** Read-only. No process attached, no memory written, no Ghidra annotation created or saved.
- **Capture:** [`../captures/traces/2026-08-01-prey-device-creation-map.md`](../captures/traces/2026-08-01-prey-device-creation-map.md)
- **Next question:** Re-apply the two lost renderer renames, promote a byte signature for R-025, and classify the second `CreateDXGIFactory1` consumer at `0xD87710`.

## 2026-08-01 — Analysis pass completed; R-025 promoted to the fail-closed gate

- **Correction:** The preceding entry's readings were taken while the analysis pass was still running. Final state is 86,434 functions, **557,719** symbols, and 1,114 data types, not the 124,871 symbols sampled mid-pass. The caveat that string definition remained partial is **withdrawn** — it was an artefact of the incomplete pass.
- **Cross-validation:** With the index complete, `search_strings` returns exactly 8 stereo-bearing literals, which reconcile 1:1 with the 10 raw byte hits from the earlier exhaustive pass (two byte hits fall inside `Stereo:ReadStereoParameters` and `Stereo:StereoParameters`). A second query confirmed zero matches for `r_Stereo`, `StereoMode`, `Oculus`, `OpenVR`, `OSVR`, `SteamVR`, `HmdDevice`, and `IHmd`. Two independent methods now agree, so the H-001 stereo negative is corroborated rather than resting on byte search alone.
- **Restoration:** The `BeginRendererScene` and `EndRendererScene` renames lost to the pass were re-applied with full evidence plate comments carrying build hash, confidence, and validation recipe. The program was saved.
- **Promotion:** `0x180F50000` was named `InitializeD3D11DeviceAndSwapChain` and given a plate comment. Its 32-byte prologue `48 89 5C 24 18 88 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F9 48 81 EC B0 00 00 00` is unique in the image and is now the 22nd landmark in the fail-closed engine map, the smoke-log contract, the build manifest, and the build doctor.
- **Renderer RTTI is absent:** `CRenderView` and the renderer classes never surface by name, which is why the multi-view half of H-001 needs structural analysis rather than symbol lookup. This corroborates R-005's existing note.
- **Verification:** The clean Release headless loop passes all ten CTest targets with the expanded gate. The build doctor independently matches all 22 promoted signatures in the installed Steam DLL and reports 28 passes, 0 warnings, and 0 failures. The rebuilt artifact SHA-256 is `32881EDAA8C79F5650B354DE9E8E2D6B7EF8EE49E89F0FFC84B00C1C8819E30B`, superseding `8FDF164B…5523150`; the manifest now reports `runtime_landmarks=22`.
- **Scope limit:** The 22-landmark gate has **not** been live-loaded into Prey. R-025 stays `static-only`: it has never been observed executing, and its object layout is inferred from COM slot usage rather than a runtime read.
- **Next question:** Attach ReGenny to the live renderer singleton at `PreyDll.dll+0x2B3E8E0` and resolve whether the render view carries more than one view info, which is the remaining half of H-001.

## 2026-08-01 — H-001 closed: the renderer is single-view

- **Session:** Read-only ReGenny attach to `Prey.exe` PID `705808` in a loaded save. `PreyDll.dll` mapped at `0x00007FFE31730000` with size `48,353,280`, matching the runtime baseline. The renderer singleton resolved to `PreyDll.dll+0x2B24E80`, module-resident as R-005 records.
- **Gate:** The swapchain and device vtables resolved into `dxgi.dll` and `d3d11.dll`, `scene_nesting` read `1`, and `frame_slot` alternated. The statically-derived Present fields also matched the earlier Cheat Engine capture exactly — `+0xB1EC = 1` and `+0xB1F0 = 0` against `SyncInterval=1, flags=0` — independently confirming the R-003 Present argument map.
- **Refuted hypothesis:** The 2-entry ring at `+0xAC30`, nominated by the protocol as the leading `CRenderView` candidate, is not a view structure. Both entries share one `d3d11.dll` vtable with 11 contiguous entries, which is `ID3D11Query`-shaped rather than `ID3D11DeviceContext`-shaped (~145). They are double-buffered GPU timing queries. Recorded as R-027 so the candidate is not re-investigated.
- **Evidence:** Scanning for an orthonormal 3x3 rotation followed by a plausible world position located the real view block at renderer `+0x4A08`, stride `0x328`, two slots indexed by `+0x499C`. Layout is promoted as R-026: rotation at `+0x240` (all row norms and pairwise dots verified within `1e-3`), position at `+0x264`, frustum L/R/B/T at `+0x270`, near/far `0.1 / 8000.0` at `+0x280`. The frustum is internally consistent — `R/T = 1.77778` equals the block's own aspect field.
- **Liveness:** While the player walked, position moved `0.67244` world units over 100 frames and the Z component varied by roughly `0.01` around `17.10`, which is head bob. Rotation tracked separately: `rot[0]` was `0.999929` before the player turned and `0.947947` after. This is the live render view, not a stale copy.
- **Result:** The orthonormal-camera census returned exactly **one** match per slot, at the same offset, in both stationary and moving states. The two slots are frame double-buffers rather than eyes, proven by their being byte-identical with separation `0.00000` while the player was stationary — an eye pair must differ by a constant lateral IPD offset at all times.
- **Interpretation:** H-001 closes fully negative. There is neither a retained stereo control surface nor a multi-view render structure, so the per-eye route must be mod-owned. This removes the cheapest hypothetical route before effort was spent building on it.
- **Unplanned positive:** The frustum is stored as four independent edge values rather than a single FOV scalar. OpenXR requires per-eye asymmetric projection where `L != -R`, so asymmetric frusta are directly representable in this block without restructuring the camera. Together with the R-009 `ArkPlayerCamera::UpdateView` callback, the project now has two candidate pose-injection points; which is authoritative is not yet established.
- **Tooling defect found and fixed:** Promoting the layout into `regenny/PreyVR.genny` exposed that ReGenny's `+N` is a delta from the previous field's end, not an absolute offset. The pre-existing `PreyRendererGraphics` had been silently wrong since it was written — `swapchain` resolved to `+0xAE90` and `swapchain_mirror` to `+0x36C58`. It went unnoticed because every prior finding came from x64dbg, Cheat Engine, or explicit address arithmetic rather than an overlay read. All three structs were rewritten with correct deltas and verified field-by-field: 21 of 21 overlay reads now match direct arithmetic. The check also confirmed live that `device_mirror` equals `device` and `swapchain_mirror` equals `swapchain`, as R-006 and R-007 record. No prior finding was affected. See [`FAILURE_REGISTRY.md`](FAILURE_REGISTRY.md) F-002.
- **Safety:** Read-only throughout. No memory written, no breakpoints set, no code modified. Prey remained running and responsive.
- **Capture:** [`../captures/traces/2026-08-01-prey-multiview-probe.md`](../captures/traces/2026-08-01-prey-multiview-probe.md)
- **Next question:** Determine whether the R-026 view block is authoritative for rendering or a downstream copy of the R-009 gameplay camera, since that decides where an HMD pose must be injected.

## 2026-08-07 — Bootstrap lifecycle, preflight, and headless baseline hardened

- **Review correction:** The 22-landmark artifact was still versioned like the historical 21-landmark live proof, making it too easy to transfer evidence between different binaries. The current artifact is now `0.3.0-lifecycle-hardening`, logs its own SHA-256 and compiled landmark count, and is explicitly marked live-validation pending.
- **Lifecycle:** The bootstrap worker acquires its own module reference before doing work. After the exact engine gate, observer plan, and no-call OpenXR preflight succeed, the supported module pins itself until process exit. Observer disable restores the target patch but retains the inactive MinHook entry/trampoline, preventing an in-flight render callback from returning through freed code. Supported-host hot unload is no longer part of the contract.
- **Render-thread boundary:** The observer callback now performs atomic telemetry only and calls the original function. First/milestone renderer and thread data are emitted once by the control thread during disable; no filesystem or formatting work occurs in the callback.
- **OpenXR preflight:** All filesystem checks are bounded and fail closed. Runtime manifest shape is checked without a full parser, and the adjacent loader is parsed on disk to require AMD64, `IMAGE_FILE_DLL`, PE32+, and a named `xrGetInstanceProcAddr` export. No loader call or `LoadLibrary` occurs.
- **Single source of truth:** The duplicate PowerShell list of 22 signatures was removed. A `SEC_IMAGE_NO_EXECUTE` helper now validates the installed module through `engine::ValidateLandmarks`, and both the build manifest and doctor obtain their count from the compiled C++ map.
- **Headless loop:** Configure is fresh by default; the game baseline can be optional or required. A new Win32 file-preflight test brings the fresh Release loop to 11/11. With the researched installation present, the doctor reports 7 passes, 0 warnings, and 0 failures, including one compiled-map result covering all 22 landmarks.
- **Dependency identity:** FetchContent now uses immutable commits: MinHook `c3fcafdc10146beb5919319d0683e44e3c30d537` and OpenXR SDK `64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec`.
- **Scope correction:** H-001 remains a strong negative for an exposed stock stereo control surface and the probed resident R-026 eye-pair candidate. The evidence does not justify claiming that every possible internal scene re-entry or dynamically constructed multi-view route is absent.
- **Live state:** No game process was injected during this hardening pass. The current DLL must complete the supported-host load/observer protocol before its binary is called live-proved.

## 2026-08-15 — Chairloader PDB headers imported; R-026 identified as `CRenderCamera`

- **Source:** Chairloader's public source repository was cloned into `tools/Chairloader-src` (sparse, `Common/` only: 1,131 headers, 16 MB). These are the generated headers derived from the PDB shipped with the canonical EGS build `0485C85B…99F0A63`, which Chairloader's own `Versions.xml` describes as the one version "released along with PDB files for it". The binary release at `tools/Chairloader-1.3.4` contains no PDB and no headers; both directories are now gitignored.
- **R-026 identified.** `CryRenderer/IRenderer.h` defines `class CRenderCamera` with members `Vec3 vX, vY, vZ; Vec3 vOrigin; float fWL, fWR, fWB, fWT; float fNear, fFar;` — 72 bytes. Imported into the EGS reference program's type manager and confirmed by Ghidra at `vOrigin +0x24`, `fWL +0x30`, `fNear +0x40`. Against R-026's base of `+0x240` those resolve to `0x264`, `0x270`, `0x280`, matching every offset measured in the live probe. **The R-026 camera block is a `CRenderCamera`.**
- **Correction:** the registry previously described `+0x240` as a `Matrix33` whose "row 1 is the up axis, matching CryEngine's forward=Y/up=Z convention". That conflated two things. The nine floats are the camera's own basis vectors `vX`, `vY`, `vZ`; `vY` is the camera up vector and read near world Z only because the player happened to be level. Corrected in the registry and in `regenny/PreyVR.genny`.
- **H-001 corroborated from source.** `RenderDll/Common/RenderView.h` line 203 declares `CRenderCamera m_renderCamera;` — a single camera per `CRenderView`, not an array. The multi-view negative now rests on two independent methods: a live orthonormal-camera census, and the PDB-derived class definition.
- **New H-007 lead.** `IRenderView::EViewType` enumerates `eViewType_Default`, `eViewType_Recursive`, `eViewType_Shadow`. The engine has a first-class **recursive** view type, which is the conventional CryEngine mechanism for portals, mirrors and reflections — the most likely implementation behind `e_ArkLookingGlass`'s second scene, and directly relevant to the mod-owned scene re-entry route.
- **Scope:** types were imported into the EGS reference program only. The Steam program was not modified, and no game binary was converted. Running Chairloader would require converting the install to the EGS build, which remains an open decision.
- **Next question:** read `RenderView.h` and `DriverD3D.h` in full to name the renderer singleton class (R-005's provisional type) and locate the frame-slot block at `+0x4A08` within it, then trace how `eViewType_Recursive` views are created and whether one can be driven from an arbitrary camera.

## 2026-08-15 — Renderer singleton identified as `CD3D9Renderer`

- **Method note:** `CD3D9Renderer : public CRenderer : public IRenderer, public CRendererCVars` — multiple inheritance with virtuals, which Ghidra's CParser cannot reproduce. A wholesale import would have produced a struct that parses and lies. Instead the member declarations were read from `RenderDll/XRenderD3D9/DriverD3D.h` and their offsets computed by hand, then checked against offsets already measured live.
- **Verification chain.** Walking forward from `m_pSwapChain`, which the live probe had already placed at `+0xAE88`: `unsigned long m_dwPresentStatus / m_dwCreateFlags / m_dwWindowStyle` occupy `+0xAE90..+0xAE9B`, `char m_strDeviceStats[90]` occupies `+0xAE9C..+0xAEF5`, and `int m_SceneRecurseCount` lands on the next 4-aligned address, `+0xAEF8`. Continuing: `SRenderTileInfo` (4 floats, 16 bytes) at `+0xAEFC`, `TArray<S2DImage>` (`T* + uint32 + uint32`, 16 bytes, 8-aligned) at `+0xAF10`, `unsigned m_nConnectedMonitors` at `+0xAF20`, `bool m_bDisplayChanged` at `+0xAF24`, and `CCryDeviceWrapper m_DeviceWrapper` on the next 8-aligned address, `+0xAF28`.
- **Result:** three offsets measured independently and in advance — `+0xAE88` (R-006), `+0xAEF8`, `+0xAF28` (R-007) — are joined by a member chain that closes byte-exactly with no slack. **R-005's class is `CD3D9Renderer`** and is no longer provisional, despite RTTI being absent from the binary. It also establishes that the EGS and Steam layouts agree across this region, which is what makes the headers usable against the Steam target here.
- **R-007 refined:** `+0xAF28` is `m_DeviceWrapper`, a `CCryDeviceWrapper` rather than a bare pointer. Its first member is the real `D3DDevice*`, which is why reading a qword at that address and calling vtable slot 39 on it worked.
- **New R-028 and a way past the missing console.** `+0xAEF8` is `m_SceneRecurseCount`. The PDB name is a *recursion* depth, not a flag, so `BeginRendererScene`/`EndRendererScene` nest. If the engine re-enters the scene to draw a Looking Glass window, that counter must exceed 1 during the frame. It is a plain int in a known location, so H-007 becomes a read-only observation — **no dev console, no cvar, no capture tooling**. That matters because Prey ships without a console and the last three attempts to answer H-007 through capture tooling all failed (F-003, F-004).
- **Scope:** documentation and type-notebook changes only. No Ghidra annotation was written for this pass, no game binary was converted, and nothing was written to game memory.
- **Next question:** Sample `m_SceneRecurseCount` at `renderer+0xAEF8` across many frames with a Looking Glass window in view and again with none in view, and compare the maximum observed depth.

## 2026-08-15 — Prey renders a real second scene; H-007 answered positive

- **Method:** Prey ships no developer console, which had blocked this experiment across three sessions and two failed capture-tooling attempts (F-003, F-004). It was never the blocker it appeared to be. Cvars are plain `int`s in a heap object, so `e_ArkLookingGlass` can be written directly. Registration was read from `FUN_180235400` (`LEA R8,[RBX+0x5C4]`, default `1`), and the object resolves through the module-relative slot at `PreyDll+0x243A688`. Promoted as R-029.
- **Result:** With mode `3` — "show only the second scene" — the display rendered the **exterior view alone, full-screen, with none of the apartment interior**. Passing through mode `0` first showed the level's real geometry, described by the observer as a film studio. Both cvars were restored to their registered defaults and verified by read-back.
- **Interpretation:** Two distinct scenes exist; the second is a complete 3D render rather than a texture, video or skybox; the engine can render it alone to the full framebuffer with the main scene absent; and mode `1` composites both within one frame. **A native path exists that renders a complete scene from a non-default viewpoint to the full framebuffer.** That is the capability the committed mod-owned per-eye route needs, present in the shipping build.
- **Correction to R-028.** The scene-recursion counter was the wrong instrument and is now marked as such. `m_SceneRecurseCount` never exceeded `1` in any mode at roughly 4,400 samples per frame, *including while the second scene was demonstrably rendering full-screen*. Prey produces the second scene without nesting `BeginRendererScene`/`EndRendererScene`. The field is correctly identified and its 0/1 oscillation does track the scene pass; only the inference drawn from it was wrong.
- **Withdrawn measurement.** Time spent at depth 1 was offered as a secondary signal and is noise. At mode 1 alone, at one location, it measured `54.0%`, `41.3%` and `82.2%`. A mode-3 reading of `22.8%` was reported as meaningful before that spread was known; it is not, and no conclusion rests on it.
- **Method lesson:** both instruments purpose-built for this question failed — a counter that cannot see the event, and a timing share too noisy to read. The answer came from writing one cvar and looking at the screen. Three prior attempts to settle H-007 went through capture tooling and produced two crashes and no data.
- **Limits:** the second scene is a different *environment*, not the main world from a second camera, and stereo needs the latter. Nothing here shows the second scene's camera is settable by a mod, and the cost of a second scene render is unmeasured because the timing data was unusable.
- **Safety:** Two bounded `int` writes to display-mode cvars, both restored and verified. No hooks, no injection, no wrapper, nothing written to the game directory; Prey was launched normally from Steam and remained running throughout.
- **Capture:** [`../captures/traces/2026-08-15-prey-lookingglass-second-scene.md`](../captures/traces/2026-08-15-prey-lookingglass-second-scene.md)
- **Next question:** Determine whether the second scene's camera is settable and whether it can be pointed at the main world, which is what separates "renders two environments" from "renders one world twice".

## 2026-08-15 — `CRenderView` camera is settable; 14,622 PDB addresses available

- **Answer to H-007's remaining half: the camera is settable.** `CRenderView` declares `virtual void SetCamera(const CCamera& cam)` as a public virtual on `IRenderView`, alongside `SetPreviousFrameCamera`. It owns `CCamera m_camera`, `CCamera m_previousCamera`, and the `CRenderCamera m_renderCamera` already promoted as R-026. Its constructor is `CRenderView(const char* name, EViewType type, CRenderView* pParentView, ShadowMapFrustum*)`, so views are created with a type and a **parent**, forming a hierarchy.
- **Why R-028 could never have worked.** Looking Glass is a per-view flag — `m_bLookingGlassEnabled`, `m_bLookingGlassMaskEnabled`, `m_bLookingGlassScene1Only`, set through `virtual void EnableLookingGlass(bool)` — and recursion is expressed as a **view hierarchy via `m_pParentView`**, not as nested `BeginRendererScene`/`EndRendererScene`. The scene-recursion counter was structurally incapable of observing it. `m_bLookingGlassScene1Only` corresponds to the cvar modes exercised in the previous entry.
- **Scale finding.** The generated headers carry **14,622 `PreyFunction<...>(0x…)` entries across 633 files, plus 1,070 global data addresses**. These are PDB-derived EGS RVAs. `BUILD_BASELINE.md`'s rule stands — none may be used directly against the Steam build — but they supply the one input the existing cross-build translation method never had: a complete `name -> EGS RVA` table. Translation becomes mechanical rather than exploratory: read bytes at the EGS RVA in the reference program, find a unique signature, locate it in the Steam program, verify.
- **Relevant addresses (EGS, translation required):** `CRenderView::SetCamera` `0xEBB960`, `SetPreviousFrameCamera` `0xEBBDC0`, `CRenderView::CRenderView` `0xEB2DF0`, `EnableLookingGlass` `0xEB85E0`, `CollectLookingGlassInformation` `0xEB7FB0`.
- **Limits.** A callable `SetCamera` is one step of a per-eye render, not the whole thing; driving a second eye also requires obtaining or constructing a view, populating its render items, and getting it submitted. And Looking Glass's second scene is a different *environment* — nothing yet shows that pointing a view's camera at the main world produces a correct second render of that world, which is what stereo actually needs.
- **Next question:** Translate `CRenderView::SetCamera` from EGS `0xEBB960` to a Steam address by signature, as the first test of whether the 14,622-entry table converts cleanly.

## 2026-08-15 — Recursive views are a pooled pair, not constructed on demand

- **The question was mis-framed.** Looking for callers that construct a view with `eViewType_Recursive` assumes views are made on demand. They are not. `SRenderPipeline` declares `_smart_ptr<CRenderView> m_pRenderViews[2][2]`, a permanent pool, and `CRenderer::GetRenderViewForThread(int nThreadID, bool bRecursive)` is a 20-byte leaf that simply indexes it.
- **Evidence:** R-032's disassembly is `movsxd rdx, edx` / `movzx eax, r8b` / `lea rax,[rax+rdx*2]` / `mov rax,[rcx+rax*8+0x6F38]` / `ret` — that is exactly `return m_pRenderViews[nThreadID*2 + bRecursive]`. The pool base is `CRenderer+0x6F38`, promoted as R-033.
- **Three translations, all unique byte matches:** `CRenderView::SetCamera` EGS `0xEBB960` -> Steam `0xEE7E80` (R-030), `CRenderView::CRenderView` EGS `0xEB2DF0` -> Steam `0xEDF390` (R-031), `CRenderer::GetRenderViewForThread` EGS `0xFB8FE0` -> Steam `0xFE5620` (R-032). Their deltas are `0x2C520`, `0x2C5A0` and `0x2C640` — three more distinct values, reinforcing that no global delta exists.
- **Signature note:** the constructor's signature stops at 39 bytes, immediately before a `LEA RAX,[RIP+...]` vtable load. RIP-relative displacements are build-specific and would guarantee a miss. This is the same class of hazard as the leading-REX problem already recorded in `BUILD_BASELINE.md`, and worth applying to every future translation from the 14,622-entry table.
- **Free layout fact:** the constructor's `44 89 41 14` stores its `EViewType` argument to `this+0x14`, so `CRenderView::m_viewType` is at `+0x14`.
- **What this means for the per-eye route.** The path is short and entirely native: `GetRenderViewForThread(threadID, true)` returns the recursive view, `SetCamera` re-aims it. No construction, no allocation, no lifetime management.
- **Limits.** Everything here is static; none of these functions has been observed executing. R-033 has never been read live. Above all, **obtaining a recursive view is not the same as getting it rendered** — the pool existing says nothing about whether a mod can cause the engine to submit that view, populate its render items, or composite its output. That is the next real question, and it is larger than anything answered today.
- **Next question:** Read R-033 live and confirm four non-null pointers with `[t][1]` distinct from `[t][0]`; then find what actually consumes the recursive view during a frame.

## 2026-08-15 — Unattended static pass: signature audit, `CCamera`, view-pool encapsulation

Three static findings, no game running and no input required.

- **Signature audit.** Three of the 22 promoted signatures embed a `[RIP+disp32]` operand and are
  therefore build-specific: `renderer.dispatch` (R-004), `player.get_instance` (R-008) and
  `movement.get_state` (R-016). Decode verified — R-004's displacement resolves to exactly
  `0x2B3E8E0`, the R-005 renderer singleton. **No runtime risk**, because signatures are only applied
  after the exact module hash matches; the exposure is cross-build translation, where all three would
  fail. Recorded in [`BUILD_BASELINE.md`](BUILD_BASELINE.md) alongside the REX hazard.
- **`CCamera` carries first-class asymmetric-frustum support.** `CryMath/Cry_Camera.h` declares
  `SetAsymmetry(float l, float r, float b, float t)` backed by `m_asymL/R/B/T`. This is precisely the
  `L != -R` projection OpenXR requires per eye, already present as a purpose-built setter.
  **Critical caveat, from the engine's own comment: "not used for culling atm."** Asymmetry shifts
  projection but may not propagate to the culling frustum, which collides directly with
  [`ARCHITECTURE.md`](ARCHITECTURE.md)'s non-negotiable that the engine must own culling for
  translation to reveal new geometry. Treat asymmetric projection as available but culling-unsafe
  until tested.
- **Usable `CCamera` surface for a per-eye route:** `SetMatrix(const Matrix34&)` (asserts
  orthonormal), `SetPosition(const Vec3&)`, `SetFrustum(w, h, fov, near, far, pixelAspect)`,
  `SetZRange(min, max)`, `SetAsymmetry(l, r, b, t)`. `m_fov` is vertical FOV in radians.
- **The view pool is fully encapsulated.** The `+0x6F38` displacement appears **exactly once** in the
  entire image, at `0x180FE562B` inside R-032 itself. No other code reaches `m_pRenderViews`
  directly, so `GetRenderViewForThread` is the single point of control for view acquisition — useful
  if the mod ever needs to intercept it.
- **Not done, and why.** Tracing what *consumes* the recursive view stalled: `GetRenderViewForThread`
  is virtual and its only two xrefs are vtable slots at `0x181DD2FA0` and `0x181DD7CB0`. Consumers
  dispatch through those, so finding them needs vtable analysis rather than xrefs. That is a larger
  job and was left rather than half-done. `CD3D9Renderer::RT_RenderScene(CRenderView*, int,
  SThreadInfo&, void(*)())` is the obvious next thread.
- **Next question:** Resolve the two vtables at `0x181DD2FA0`/`0x181DD7CB0` to identify their owning
  classes, then find the call sites that pass `bRecursive = true`.

## 2026-08-15 - The recursive pass takes an arbitrary camera

- **The decisive find.** `Cry3DEngine/I3DEngine.h` defines
  `SRenderingPassInfo::CreateRecursivePassRenderingInfo(const CCamera& rCamera, uint32 nRenderingFlags)`.
  It sets `m_nRenderStackLevel = 1`, calls `passInfo.SetCamera(rCamera)`, acquires the recursive view
  through `GetRenderViewForThread(passInfo.ThreadID(), true)` (R-032), and tags render items with
  `SRendItemSorter::eRecursivePassMask`. The sibling construction at line 3007 is identical but passes
  `false`, for the normal pass.
- **Why this matters.** Every prior entry carried the caveat that Looking Glass renders a *different
  environment* rather than the main world from a second camera, and that stereo needs the latter. This
  function takes **any** `CCamera` as its first parameter. The engine's own recursive-pass entry point
  is parameterised on the camera, so rendering the world again from a supplied camera is a native,
  first-class operation rather than something to be improvised.
- **Why R-028 was structurally doomed.** R-036 `CRenderView::Job_PostWrite` is the sole caller of R-034
  `CollectLookingGlassInformation`. Looking Glass data is gathered in a **post-write job on the render
  view**, so it never nests `BeginRendererScene`/`EndRendererScene`. This is the same
  `JobRenderViewPostWrite` string found at `0x181DCAF7B` during the very first stereo reconnaissance,
  where it was noted as revealing nothing about view count. It was the thread to pull.
- **Three more translations**, each a unique byte match with a documented signature cut point:
  `CollectLookingGlassInformation` EGS `0xEB7FB0` to Steam `0xEE44D0` (R-034), `EnableLookingGlass`
  EGS `0xEB85E0` to Steam `0xEE4B00` (R-035), and `Job_PostWrite` EGS `0xEB9EA0` to Steam `0xEE63C0`
  (R-036). All three deltas are `0x2C520`, matching R-030 - expected, since all four occupy the same
  contiguous stretch of `CRenderView` code, and not evidence of a global delta.
- **Reverse translation works.** R-036 was identified by reading its Steam bytes, matching them in the
  EGS image, then looking that EGS RVA up in the header table: Steam address to name, mechanically.
  The 14,622-entry table is usable in both directions.
- **Free layout fact:** `CRenderView::m_bLookingGlassEnabled` sits at `+0xFC0`, from R-035's single
  instruction.
- **Not resolved.** The two renderer vtables at `0x181DD2FA0` and `0x181DD7CB0` were **not** identified.
  Locating their bases requires a backward scan for the RTTI pointer, and that was abandoned as poor
  value once the headers answered the question directly. `CRenderView`'s vtable was located
  incidentally at `0x181DCAEB8`.
- **Limits.** `CreateRecursivePassRenderingInfo` is `inline`, so it likely has **no standalone address
  to hook** - it compiles into each caller, and the practical seam is those call sites, which have not
  been enumerated. Everything here is static and nothing has been observed executing. Separately,
  `e_RecursionViewDistRatio` divides the zoom factor in recursive passes, so a recursive view renders
  at reduced view distance by design, which is a quality consideration if this route is ever used for
  an eye.
- **Next question:** Enumerate the call sites of `CreateRecursivePassRenderingInfo` in the Steam binary
  by locating the inlined `GetRenderViewForThread(..., true)` pattern, and determine what submits a
  recursive view for rendering.

## 2026-08-15 - Both renderer vtables resolved

- **Method, since RTTI is unavailable.** The renderer has no RTTI, so there is no Complete Object
  Locator at `base-8` and no class name to read - `0x181DD2E00` holds an ordinary text pointer. The
  bases were instead recovered by arithmetic from a live-verified slot. R-004 captured, in a running
  process, that the renderer singleton's vtable is called at `+0x8D0` and resolves to R-003. The
  pointer to R-003 occurs **exactly once** in the whole image, at `0x181DD36D8`, so the base is
  `0x181DD36D8 - 0x8D0 = 0x181DD2E08`. R-002's pointer at `0x181DD36D0` gives the same base via
  `- 0x8C8`, independently.
- **R-037, `CD3D9Renderer` vtable at `0x1DD2E08`.** This is the vtable of the live renderer object,
  not an inference - R-004's dispatch was observed executing. `GetRenderViewForThread` (R-032) sits at
  `+0x198`, slot index 51.
- **R-038, `CRenderer` vtable at `0x1DD7B18`.** The second vtable holding R-032. Assuming the same
  slot index gives that base, and at `+0x8C8` and `+0x8D0` it holds `0x181B9AE62` **twice**. That
  address disassembles as `FF 25 90 8E 0D 00`, a `jmp qword ptr [rip+...]` IAT import thunk, i.e.
  `_purecall`. Two adjacent pure-virtual slots exactly where the concrete vtable has R-002 and R-003
  identifies this as the abstract base class. `GetRenderViewForThread` is implemented in `CRenderer`
  itself, which is why it appears in both vtables.
- **A cross-check that failed, and why it does not matter.** The obvious confirmation - that the slot
  following `GetRenderViewForThread` should match the next virtual declared after it in `Renderer.h` -
  does **not** hold. The vtable belongs to `IRenderer`, so slot order follows the interface's
  declaration order, not `CRenderer`'s. The identification rests on the `_purecall` pair and on
  R-004's live capture, both of which are sound without it.
- **Naming discovery.** `BeginRendererScene` and `EndRendererScene` return **zero** matches across all
  633 PDB-derived headers. Those are names this project invented during the first renderer
  reconnaissance, not PDB symbols. Their real names are recoverable by reverse translation, the same
  technique that identified R-036: read the Steam bytes, match them in the EGS image, look that RVA up
  in the header table. Worth doing, since R-002 and R-003 are the most-referenced entries in the
  registry and currently carry invented names.
- **Limits.** R-038's base assumes the shared slot index, which is sound for a single-inheritance
  vtable prefix but was not independently measured, so it is recorded as `observed` rather than
  `reproduced`. Neither vtable was verified against a live object this session, since Prey was closed.
- **Next question:** Recover the real PDB names for R-002 and R-003 by reverse translation, and rename
  them throughout the registry and the engine map.

## 2026-08-15 - R-002 and R-003 had invented names; both corrected

- **The correction.** R-002 and R-003 are `CD3D9Renderer::RT_BeginFrame` and `CD3D9Renderer::RT_EndFrame`,
  declared adjacently at `RenderDll/XRenderD3D9/DriverD3D.h:1049-1050`. They have been called
  `BeginRendererScene` and `EndRendererScene` since 2026-07-31. Those names were never PDB symbols;
  they return zero matches across all 633 generated headers.
- **How the wrong names arose.** `RT_EndFrame` was named after the diagnostic string it references,
  `EndScene without BeginScene`. That is precisely the string-only association
  [`GHIDRA_SYNC.md`](GHIDRA_SYNC.md)'s annotation policy rule 3 forbids: *"Do not rename an unknown
  function after a string-only association."* The project wrote that rule and then broke it in its
  first annotation batch. The diagnostic's wording is legacy CryEngine vocabulary and describes the
  *check*, not the function.
- **Method.** Reverse translation, the technique that identified R-036. Steam bytes matched in the EGS
  reference gave `0xF52150` for R-003, unique, which the header table names `FRT_EndFrame`.
- **R-002 needed disambiguation.** Its prologue `48 8B C4 55 53 48 8D 68 A1 48 81 EC B8 00 00 00` is a
  generic MSVC frame setup carrying no member offsets, and it matches **twice** in EGS, at `0xF51650`
  and `0x141CAD0`. The correct match was selected by spacing: `RT_EndFrame` sits exactly `0xB00` after
  `RT_BeginFrame` in both builds (Steam `0xF7D710`/`0xF7E210`, EGS `0xF51650`/`0xF52150`). The rejected
  candidate is `ArkCystoid::ProcessNearbyCystoids`, which confirms the choice. Both deltas are
  `0x2C0C0`.
- **Free ABI confirmation.** The PDB signature is `void RT_EndFrame(CD3D9Renderer* _this)`. PreyVR's
  frame observer assumed `void(__fastcall*)(void* renderer)` from Ghidra decompilation alone; the PDB
  now confirms it independently.
- **Lesson worth keeping.** Naming a function after a string it references buys the *string's*
  vocabulary, not the function's identity. R-002 also shows the second half: a prologue with no member
  offsets is a weak cross-build anchor, and here it was genuinely ambiguous. Prefer interior anchors,
  as `BUILD_BASELINE.md` already advises for the REX and RIP-relative hazards.
- **Scope of the change.** Registry, Ghidra names and plate comments updated; the program was saved.
  **The compiled engine map still uses the old description strings.** Changing them alters the DLL
  hash, and the 22-landmark artifact has never been live-loaded, so that rename is deliberately left
  as a separate decision rather than compounding an unverified build.
- **Next question:** Decide whether to rename the descriptions in `src/common/EngineMap.cpp` and the
  build doctor, accepting a new artifact hash, or defer until after the pending supported-host load.

## 2026-08-15 - The build is not byte-reproducible (F-005)

- **How it surfaced.** The R-002/R-003 rename was deferred, so a comment was added to
  `src/common/EngineMap.cpp` marking the deferral where an editor would see it, then the loop was run
  to confirm the comment was hash-neutral. It was not. Reverting the comment did not restore the
  original hash either.
- **Evidence.** Three builds, three hashes: `179652AA...` (committed source), `80AACFC3...` (one added
  comment), `1D21F6D8...` (reverted, byte-identical source to the first). Builds one and three came
  from identical source and differ. A further rebuild with no source change left the hash alone,
  because nothing was recompiled. The likely mechanism is the PDB signature GUID regenerating on each
  link into the PE debug directory.
- **Consequence, and it is not theoretical.** The hardening handover told the next operator to load
  the DLL with hash `179652AA...` and to validate the smoke log against it. **That artifact was
  destroyed by the rebuild and cannot be regenerated.** Behaviour is unchanged - identical source,
  11/11 passing - but the specific binary named in the procedure is gone.
- **The methodological error underneath.** A recorded artifact self-hash was being treated as an
  identity for the *source*. It is not; it identifies one build output. Every document naming a
  previous hash is invalidated by any rebuild, for any reason.
- **Corrected procedure.** The handover now instructs the operator to compute the DLL's hash
  immediately before loading it and record that value with the capture, rather than matching a hash
  written down earlier. A documented artifact hash is a historical label on a capture, never a target
  to rebuild toward.
- **Not affected.** The fail-closed gate is unharmed. The 22 landmark signatures, the
  `supported_preydll_sha256` game baseline and the build doctor all validate against the *game*
  binary, not against the mod's own hash.
- **Current artifact:** `1D21F6D8DE722926187D4377E6F1CAEDC33456F665CFABD03BB57A9CFC3BE614`, from the
  committed source at `c062097`, 11/11 passing.
- **The comment was reverted** and the deferral note lives in documentation only, which cannot affect
  the artifact.

## 2026-08-15 - F-005 corrected; reference artifact re-recorded from a fresh build

- **Correction.** The preceding entry claimed the build is not byte-reproducible. **That is withdrawn.**
  A fresh `cmake --fresh` build of the committed source produced `1D21F6D8...`, identical to the
  incremental build from the same source. The build is deterministic run-to-run in this environment.
  The original claim rested on three data points and asserted an untested mechanism.
- **A second hypothesis, also refuted.** The bundled `openxr_loader.dll` hash differs between the
  build-1 manifest (`6DF5C6EC...`) and now (`5E502DFD...`) despite an unchanged pinned commit, which
  looked like a candidate cause. It is not: `PreyVR.dll` imports only `bcrypt.dll`, `SHELL32.dll`,
  `ADVAPI32.dll` and `KERNEL32.dll`, all with zero import timestamps. The loader is not linked into it.
- **What remains true.** The hash recorded for build 1, `179652AA...`, does not reproduce from the
  committed source, and that artifact was destroyed by rebuilding. Why build 1 differs is **not
  established** and is now recorded as an open question rather than an explained one.
- **The operational rule is unchanged and is the part that matters.** Compute the DLL's hash
  immediately before loading it and record that with the capture; never rebuild between recording and
  loading; treat a documented artifact hash as a historical label on a capture rather than a target.
- **Reference artifact re-recorded:** `1D21F6D8DE722926187D4377E6F1CAEDC33456F665CFABD03BB57A9CFC3BE614`,
  fresh build of `c062097`, 11/11 passing, manifest and on-disk file agreeing. Not to be rebuilt before
  the pending supported-host load.
- **New loose end.** The shipped `openxr_loader.dll` is not reproducing from a pinned commit. It is
  provably not the cause of the PreyVR.dll difference, but it ships with the mod and its
  reproducibility deserves its own investigation.

## 2026-08-16 - Why nothing in the build reproduces: no `/Brepro`

- **Question asked:** why the shipped `openxr_loader.dll` was not reproducing from a pinned commit.
- **Answer: it is not loader-specific.** `CMakeLists.txt` sets no `/Brepro`, and neither `PreyVR.dll`
  nor `openxr_loader.dll` carries an `IMAGE_DEBUG_TYPE_REPRO` entry. MSVC therefore writes the real
  link time into `IMAGE_FILE_HEADER.TimeDateStamp`. `PreyVR.dll` reads `0x6A813D67`, 2026-08-16
  04:32:39 UTC; the loader reads `0x6A813D16`, 04:31:18 UTC, 81 seconds earlier and in dependency
  order. Both match their file mtimes. Every relink writes a new timestamp, so every relink changes the
  hash - for every target in the build.
- **This overturns the previous entry's correction.** That entry withdrew the non-reproducibility
  finding because a `cmake --fresh` build reproduced the prior hash. **`cmake --fresh` does not force a
  relink** - it wipes the CMake cache and reconfigures, leaving existing outputs up to date.
  `PreyVR.dll` is timestamped 04:32:39 while the fresh `CMakeCache.txt` is 04:59:18, so the DLL is 27
  minutes older than the reconfigure that supposedly produced it. No new binary was emitted; the
  matching hash was the same file.
- **The recurring mistake, stated plainly.** Twice now a conclusion about determinism was drawn from
  comparing hashes without first checking whether a build had actually happened. A hash comparison
  says nothing about reproducibility unless a relink demonstrably occurred. The PE timestamp and the
  file mtime both answer that in seconds.
- **Useful detail:** neither binary carries a `CODEVIEW` debug entry, so no PDB GUID or path is
  embedded. The link timestamp is plausibly the only source of non-determinism, which means `/Brepro`
  alone might make these builds reproducible. Untested, and deliberately not attempted now, since
  changing the build would destroy the reference artifact.
- **No rebuild was performed for this investigation.** Everything above was read out of the existing
  binaries and file metadata, precisely so the reference artifact
  `1D21F6D8...` survives to the supported-host load.
- **Next question:** After the live load, add `/Brepro`, then verify reproducibility with a test that
  forces a genuine relink - touch a source file, build, revert, build again, and compare.

## 2026-08-16 - R-032's index formula tested by emulation

- **Prompted by** a fleet-wide audit noting that every RE tool is used through a narrow slice of its
  surface, and flagging Ghidra's `emulate_function` as the highest-leverage unused call for a project
  doing this much static work ahead of a live hook. That is a fair characterisation of this project.
- **Analysis health checked first**, per the same audit's warning that Ghidra can report
  `analyzed: true` on a near-empty database. Steam `PreyDll.dll` has 86,434 functions across 48,338,776
  bytes; the EGS reference has 87,026 across 48,289,760. About one function per 557 bytes each,
  consistent with each other. Neither is under-analysed, so no downstream result needed revisiting.
- **Ghidra had never created a function at R-032.** At 20 bytes it is a leaf that auto-analysis skipped,
  which is why `get_function_callers` returned nothing useful for it earlier and why `emulate_function`
  initially refused it. Created as `CRenderer_GetRenderViewForThread`; the resulting body size of 20
  matches the byte count exactly, which is itself a boundary confirmation.
- **Result: the index formula is confirmed by execution.** With `RCX` set to the image base so the pool
  overlays known file bytes, the four input combinations returned:

| `nThreadID` | `bRecursive` | returned | slot | index |
| ---: | ---: | --- | --- | ---: |
| 0 | 0 | `0x8D4801C092E20D8D` | `+0x00` | 0 |
| 0 | 1 | `0x05C74801CA738305` | `+0x08` | 1 |
| 1 | 0 | `0x0000000002248CF8` | `+0x10` | 2 |
| 1 | 1 | `0xE902248CD9058948` | `+0x18` | 3 |

  The `nThreadID=1, bRecursive=0` case is the decisive one: it returns slot **2**, ruling out the
  alternative ordering `nThreadID + bRecursive*2`. R-032 and R-033 are updated to record that the
  arithmetic is tested rather than read.
- **A trap found on the way, recorded as F-006.** The first four runs used the obvious
  `RCX=0x...,RDX=0x0` key-value form for `registers`. Every run reported `success: true`,
  `hit_return: true` and the correct five-step count - and returned `RAX = 0x0` in all four cases,
  which reads naturally as "the pool slots are null". The inputs had been **silently discarded** and the
  emulator ran with zeroed registers. It was caught by adding `RCX`, which the function never modifies,
  to `return_registers` and finding it read back `0x0`. The correct format is JSON. Verify the harness
  before the hypothesis; this is the same silent-failure shape as F-002.
- **Scope:** static only. No game running, no rebuild, and the reference artifact `1D21F6D8...` is
  untouched.
- **Next question:** Emulation is now a proven technique here. The obvious next candidate is a camera or
  projection builder, where the peer's framing applies directly - it converts "this decompilation looks
  like a view-matrix builder" into a tested claim before a build is spent on it.

## 2026-08-22 - FC2VR prior art assessed

- **What it is.** `FC2VR_DISCORD_TEST_v1.0.1R4` for Far Cry 2 / Dunia. Its "native stereo" is **not** an
  engine stereo mode: `NATIVE_VIEW_BEGIN`/`END` bracket one eye rendered through the engine's own camera
  and view-builder path, and "native stereo" means both eyes done that way and submitted as an
  `XrCompositionLayerProjection`. Their recurring failure, "Projection/Quad flapping", is the host
  falling back to a flat `XrCompositionLayerQuad`. So it is an **existence proof of the mod-owned
  per-eye route** PreyVR committed to after H-001 — not evidence that any engine kept a stereo switch.
  The lineage is close: Dunia forks CryEngine 1, Prey is CryEngine 3.x-era.
- **Reuse is blocked by bitness, measured not assumed.** The bridge `d3d9_fc2vr.dll` and both outers are
  **x86**; `fc2vr-host.exe` and their OpenXR loader are **x64**. Far Cry 2 is 32-bit and the host is
  deliberately out-of-process so a 32-bit game can reach a 64-bit runtime. Prey is 64-bit, so the
  in-process bridge cannot load, and only binaries are shipped — no source, and no documented
  shared-memory protocol. The value is the design and the failure analysis, not the binaries.
- **The most valuable transfer is a bug we have not hit yet.** Their R2 evidence is bit-exact: while the
  LEFT eye was being built, the game's own camera observer kept running and overwrote the stored primary
  camera with LEFT's transient value; RIGHT then rejected a correctly-restored stock camera against that
  contaminated reference. **PreyVR has a mapped analogue** — R-011 recomputes a cached world ray from the
  camera every frame into ArkPlayer `+0x17D4`/`+0x17E0`, and that ray is what the A0b wrench proof and
  the interaction A0 proof both steer. Driving the camera per eye would feed it whichever eye ran last.
  Tracked as H-008.
- **Second transfer: validate pose, not matrix coefficients.** Their original guard compared 16 raw View
  coefficients against a fixed `0.010` threshold and produced yaw-dependent false rejects, because
  world-space translation amplifies rotation rounding at large map coordinates. They now validate the
  rebuilt physical pose (rotation error `< 0.0025`, position error `< 0.010`) and keep the raw value as
  telemetry. R-026's camera was measured at roughly `(786, 1572, 17)` — the same coordinate range that
  caused their problem.
- **Also worth inheriting:** separate persistent stereo ownership from per-frame transfer state (their
  R3); hold the last complete L/R pair rather than letting a mono frame overwrite eye slots (their R4);
  transactional camera restore with a *visible* fail-back (their R1).
- **They have deterministic fresh rebuilds** that reproduce the shipped SHA-256 exactly, which PreyVR
  currently does not — an existence proof supporting the `/Brepro` option recorded in F-005.
- **An injection vector we had dismissed.** FC2VR reaches the game with a `d3d9.dll` **proxy** in the
  game folder. R-025 established that `PreyDll.dll` calls `LoadLibraryA("dxgi.dll")` with a bare name,
  and bare-name `LoadLibrary` searches the application directory first — so a `dxgi.dll` proxy beside
  `Prey.exe` would be loaded by Prey itself. apitrace's refusal to install a DXGI wrapper (F-003) was an
  apitrace limitation, not a Windows one. This gives a lifecycle PreyVR's manual injection cannot: the
  mod is present before any device exists. It also writes to the game directory, which the project has
  avoided so far — a real tradeoff, not a free win.
- **Scope:** read-only inspection. Nothing was executed, nothing copied, and the PE bitness table was
  measured from the shipped headers rather than taken from their own validation file.
- **Next question:** Before any camera write, resolve H-008 — sample R-011's and R-016's outputs across a
  frame to establish which camera consumers recompute and when.

## 2026-08-22 - H-008 resolved: the aim ray is rebuilt from the global view camera

- **Answer: the contamination risk is real, and now specific.** R-011
  `ArkPlayer::UpdateCachedReticleViewPosAndDir` has exactly **one** caller, `CArkUIHUD::OnPreRender`
  (EGS `0x1639B40`, resolved by reverse translation). The cached aim ray is therefore rebuilt during
  *render preparation*, not during the gameplay tick.
- **Which camera it reads.** Decompiling R-011 shows it calls vtable `+0x388` on the global at
  `PreyDll.dll+0x224DA60` and unprojects through the returned object. Every field it touches maps to a
  named `CCamera` member: `m_Matrix` at `+0x00`, `m_fov` at `+0x30` fed to `tanf(x*0.5)`,
  `m_Width`/`m_Height` at `+0x38`/`+0x3C`, `m_ProjectionRatio` at `+0x40`, and the two that clinch it -
  `GetNearPlane() = m_edge_nlt.y` at `+0x4C` and `GetFarPlane() = m_edge_flt.y` at `+0x64`, exactly as
  `Cry_Camera.h` defines them. `ISystem::GetViewCamera()` is declared at `CrySystem/ISystem.h:1309`.
  Promoted as R-039.
- **Consequence.** Setting the **system view camera** once per eye would feed R-011 an eye-specific
  camera, corrupting ArkPlayer `+0x17D4`/`+0x17E0`. That is the ray the A0b wrench proof and the
  interaction A0 proof steer, and the one every native weapon, melee and interaction consumer reads
  through R-013. This is FC2VR's R2 contamination expressed in Prey's own data flow, established by
  decompilation rather than by analogy.
- **Prey has an escape they did not.** F.E.A.R. and FC2VR inject *upstream*, writing the game camera
  object itself - `g_client->SetObjectTransform(camera, eyeTransform)` in
  `fear-vr/src/gameclient_loader/stereo_hook.cpp` - which is precisely why their own observers saw the
  eye cameras. Prey offers a **downstream** point: `CRenderView::SetCamera` (R-030) sets a render
  view's camera, not `ISystem`'s, so `GetViewCamera()` keeps returning the unmodified gameplay camera
  and R-011 is unaffected. That should be the preferred injection point on this evidence.
- **If the system view camera must also be set** - which culling correctness may require, given
  `CCamera::SetAsymmetry`'s "not used for culling atm" comment - then the F.E.A.R. pattern applies:
  snapshot before the eye loop, restore after, and ensure `CArkUIHUD::OnPreRender` is not re-entered
  inside it.
- **New acceptance criterion.** Any future per-eye camera protocol must sample ArkPlayer
  `+0x17D4`/`+0x17E0` before and after and require them unchanged. That converts this from a predicted
  bug into a testable gate.
- **Not fully closed.** R-016 `ArkPlayerMovementController::GetMovementState` writes view vectors at
  movement-state `+0x54`/`+0x60` and was not traced. Its registry entry records that the `GetAimDir` /
  `GetHeadDir` script wrappers are not per-frame hooks, so it is lower risk, but it is unexamined.
- **Scope:** static only. Ghidra plus the PDB headers; no game running, no rebuild, nothing written.
- **Next question:** Confirm at runtime that `CRenderView::SetCamera` does not disturb
  `ISystem::GetViewCamera()`, by reading ArkPlayer `+0x17D4`/`+0x17E0` across frames while the render
  view camera is observed - still read-only, before any write is attempted.

## 2026-08-22 - R-016 closed: same camera, but no cache to corrupt

- **It reads the same camera.** `ArkPlayerMovementController::GetMovementState` calls vtable `+0x388` on
  the global at `PreyDll.dll+0x224DA60` — byte-for-byte the same `ISystem::GetViewCamera()` call that
  R-011 makes, promoted as R-039. So it is exposed to exactly the same per-eye camera.
- **But it caches nothing, and that is the difference that matters.** Every write in the function lands
  in the caller-supplied `SMovementState&`: `+0x54`, `+0x5C`, `+0x60`, `+0x68` and `+0xB0`. Nothing is
  written to a global or to the ArkPlayer object. It is a pure on-demand query that hands a fresh
  snapshot to whoever asked and keeps none of it.
- **Contrast with R-011**, which is why that one is dangerous and this one is not: R-011 writes into
  *persistent* ArkPlayer fields `+0x17D4`/`+0x17E0` that the entire aim lane later reads through R-013.
  A contaminated value there outlives the eye loop. R-016 has no such residue — a per-eye camera can
  only affect a call made *while* the eye camera is set, and the restore closes that window.
- **What the two vectors actually are.** From the view camera's `Matrix34`: `+0x54` receives the
  translation column `{+0x0C, +0x1C, +0x2C}`, i.e. camera position; `+0x60` receives column 1
  `{+0x04, +0x14, +0x24}`, the Y axis, which is forward under CryEngine's forward=Y convention. The
  field *names* remain unconfirmed — `SMovementState` is only forward-declared in the Chairloader
  headers, so this is content established by decompilation, not names taken from a PDB.
- **A limit worth stating plainly.** Callers could not be enumerated: `GetMovementState` is a pure
  virtual on `IMovementController`, so every call dispatches through a vtable and no direct xref
  exists. The inherited registry claim that the `GetAimDir`/`GetHeadDir` script wrappers "are not
  per-frame hooks" was therefore **not** re-verified. The low-risk conclusion rests on the absence of
  caching, which is a stronger argument than call frequency anyway — it holds regardless of how often
  the function runs.
- **The acceptance gate is unchanged**, and now for a stated reason: it samples ArkPlayer
  `+0x17D4`/`+0x17E0` because those are the only *persistent* fields either consumer writes. R-016 has
  no persistent state to sample, so a before/after check cannot cover it; catching an R-016 problem
  would require hooking the call itself.
- **H-008 is now fully closed.** Both known per-frame camera consumers are traced, both read
  `ISystem::GetViewCamera()`, and only one of them caches. The mitigation is unchanged: prefer
  `CRenderView::SetCamera` (R-030), which never alters what `GetViewCamera()` returns.
- **Scope:** static only. No game running, no rebuild, nothing written.

## 2026-08-22 - Findings folded into the build: 28 landmarks, unique R-002, reproducible artifacts

- **Independent verification first.** Every address translated this session was re-checked directly
  against the installed `PreyDll.dll` by parsing its PE sections and converting RVA to file offset -
  no Ghidra involved. All nine checked signatures are byte-correct at their stated RVA. Eight are
  unique in the image; **R-002 was not**.
- **R-002's signature was ambiguous and is now fixed.** Its 16-byte prologue is a generic MSVC frame
  setup occurring twice in the Steam image, at `0xF7D710` and `0x1449620` - mirroring the EGS
  collision with `ArkCystoid::ProcessNearbyCystoids` found during the rename. The gate was never
  wrong, because it compares bytes at a fixed RVA rather than scanning, but the signature could not
  identify the function on its own. The two diverge at byte 16, so it is now **23 bytes**,
  instruction-aligned and unique. Rule recorded in `BUILD_BASELINE.md`: choose signature length by
  measuring uniqueness against the image, not by taking a fixed number of prologue bytes.
- **Gate expanded from 22 to 28.** The six `CRenderView` / `CRenderer` functions translated this
  session are promoted: `SetCamera`, the constructor, `GetRenderViewForThread`,
  `CollectLookingGlassInformation`, `EnableLookingGlass` and `Job_PostWrite`. These are the per-eye
  route's own functions, so the gate now covers the seam the mod will actually use rather than only
  the frame boundary and gameplay lanes.
- **R-002/R-003 renamed in the compiled table** to `CD3D9Renderer::RT_BeginFrame` / `RT_EndFrame`. The
  rename had been deferred to avoid churning the artifact before the live load; that deferral is
  superseded, since this change set alters the artifact regardless and leaving Ghidra, the registry
  and the DLL's own log output disagreeing is worse than one more hash change.
- **F-005 closed: builds are reproducible.** `/Brepro` added to the MSVC link options. Verified with
  the test the entry called for - one that forces a genuine relink instead of a `cmake --fresh` that
  rebuilds nothing. Baseline, comment-added and reverted builds all produced
  `E9A82DB5...`, with both relinks confirmed by changed file mtimes. `add_link_options` reached the
  FetchContent-built loader too, so the separate `openxr_loader.dll` reproducibility loose end is
  closed in the same change; both binaries now carry content-hash timestamps rather than wall-clock
  link times.
- **A self-inflicted loss worth recording.** During the first reproducibility attempt,
  `git checkout -- src/common/EngineMap.cpp` was used to remove a probe comment. Those changes were
  uncommitted, so it discarded the R-002 fix, the renames and all six new landmarks, and the next
  build silently fell back to 22 landmarks. Recovered by re-running the generating script. The rule is
  simple: commit before running any test that reverts files, and treat `git checkout --` as
  destructive to uncommitted work.
- **Single source of truth held.** Only `EngineMap.cpp` and its test needed count changes. The
  smoke-log parser, build doctor and build manifest all derive the landmark count from the compiled
  table, so the hardening pass's deduplication did its job.
- **Verification:** fresh Release loop 11/11; build doctor 7 pass, 0 warn, 0 fail with all 28
  landmarks matched against the installed `PreyDll.dll`. Current artifact
  `E9A82DB50B82B95F373A83AA96A801726C6C3A78D203F4B6E7B28A8DCBEE49A5`, and it now regenerates from the
  same source.
- **Next question:** classify the second `CreateDXGIFactory1` consumer at `PreyDll.dll+0xD87710`, the
  last open item from R-001/H-002.

## 2026-08-22 - The system view camera is a member, and its setter does nothing else

Closing the last open item from R-001 turned into the most useful static result of the session.

- **The unclassified second `CreateDXGIFactory1` consumer is machine-spec telemetry** (R-046), not a
  second render or present path. It enumerates adapters, probes each with a throwaway
  `D3D11CreateDevice` over six feature levels, requires a connected output, keeps the best
  `DXGI_ADAPTER_DESC1`, and releases everything. Its caller (R-047) caches the result behind a
  did-run flag; that caller's caller is `ISystem::AutoDetectSpec` (R-045). **R-001 is fully
  classified.** Useful byproduct: the engine honours an `r_overrideDXGIAdapter` cvar that forces one
  adapter index, which is a native, supported lever if the HMD ever needs Prey pinned to a specific
  GPU.
- **`AutoDetectSpec` was the lever for everything that followed.** It is the one function in that
  cluster the Chairloader headers actually name, so its vtable slot could be computed from
  `ISystem.h` and subtracted from its address to locate the **`CSystem` vtable** (R-043).
- **That independently verified R-039's slot index**, which its own registry entry had flagged as
  unverified. Two methods agree: counting the virtuals declared directly in `struct ISystem` puts
  `GetViewCamera` at index 113 = `+0x388`, and the located vtable's `+0x388` slot holds
  `0x180DF2BB0`. Counting naively gives the wrong answer — two virtuals belong to the nested
  `ILoadingProgressListener` and four sit inside `#if 0` blocks, and including them shifts the slot
  by two. **Rule: when deriving a vtable index from a header, count only virtuals declared directly
  in the class, and honour the preprocessor.**
- **The payoff.** `CSystem::GetViewCamera` (R-040) is `LEA RAX,[RCX+0x788]; RET`, so the global view
  camera is the *member* `CSystem::m_ViewCamera` at `+0x788` — not a pointer, and readable without
  calling anything. `CSystem::SetViewCamera` (R-041) is `ADD RCX,0x788; JMP CCamera::operator=`, and
  that operator (R-042) is a compiler-generated memberwise copy with no calls and no frustum
  recomputation. **The setter has no side effects at all.**
- **This sharpens H-008 rather than overturning it.** The hazard is unchanged in kind — R-011 still
  rebuilds the aim ray from this camera during `CArkUIHUD::OnPreRender` — but it is now located
  precisely: *all* of the risk is in who reads `m_ViewCamera` between a set and its restore, and none
  of it is in the setter. Snapshot/restore is therefore sound in principle, since `CCamera` copies are
  bit-exact, and the residual question is read ordering, which is a live-timing matter. Injecting at
  `CRenderView::SetCamera` still wins because it sidesteps the ordering question entirely.
- **`gEnv` located by a three-point fit** (R-044). Three globals seen in disassembly — an `IConsole`
  used with `GetCVar`, the R-039 `ISystem` pointer, and a pointer null-checked to mean "renderer not
  up yet" — land simultaneously on `pConsole` `+0xC0`, `pSystem` `+0xE0` and `pRenderer` `+0x120`
  from one base. Three constraints, one solution; not a guess.
- **Gate expanded 28 -> 30.** Only the two view-camera accessors were promoted, both unique at 7 and
  8 bytes. They are on the mod's critical path and the `GetViewCamera` signature pins `+0x788`
  itself. The DXGI and spec-detect functions were deliberately *not* gated: they sit on no hook path,
  and `AutoDetectSpec`'s prologue is non-unique anyway, which is the R-002 hazard recurring.
- **Verification:** every address above was re-checked directly against the installed `PreyDll.dll` by
  parsing PE sections and converting RVA to file offset, with no Ghidra involved. Three of the four
  vtable slots were predicted before being read. Fresh Release loop 11/11; doctor 7 pass, 0 warn,
  0 fail, all 30 landmarks matched. Artifact
  `C35B22D39E69D07AA7CC7DF6F5791B8EF598F81FEE293AAD8299EB6300F63AC4`.
- **One correction to my own method:** the first on-disk check reported the R-046 signature as a
  mismatch. It was not — the harness compared a 17-byte expectation against a 16-byte read. Re-run at
  equal length it matches exactly. Worth recording because a false mismatch in a verification script
  is exactly the kind of result that gets believed.

### Whole-vtable alignment, measured rather than assumed

The `CSystem` vtable was worth more than the four slots that located it, so the alignment of all 193
was tested before trusting any of them. Recorded in [`SYSTEM_VTABLE.md`](SYSTEM_VTABLE.md).

- **Shape asymmetry.** Slots whose header name begins `Get`/`Is` compile to a trivial accessor
  **74 times out of 99**; slots that do not, **2 out of 94**. A misaligned table would show the same
  rate in both groups. The two exceptions are `NeedDoWorkDuringOcclusionChecks` and `WasInDevMode` —
  predicate getters whose names happen not to start with `Get`, so there is no counterexample at all.
- **The decisive test — 24 independent name predictions.** Twenty-four accessors compile to
  `MOV RAX,[this+0x28]; MOV RAX,[RAX+d]; RET`, relaying through the `gEnv` pointer at `CSystem+0x28`.
  For each, `d` was checked against the member the header gives that accessor's name. **All 24 hit
  the right member.** The two that first looked like misses are CryEngine's own aliases —
  `GetIAnimationSystem()` returns `ICharacterManager*`, `GetIPak()` returns `ICryPak*`. One test
  therefore confirms the vtable alignment *and* the entire `SSystemGlobalEnvironment` pointer layout,
  which R-044 had derived from a three-point fit.
- **What this buys.** A verified bridge in both directions: any of 193 named `ISystem` methods to its
  Steam address, and 46 named `gEnv` subsystem pointers. Slots that were byte-verified individually
  are marked in the table; the rest are derived from an alignment confirmed at 28 independent points
  and should still have their bytes re-checked before being hooked.

The method generalises. Where a class's PDB header is available but its RVAs are not, find the one
member that can be identified in the target image by some other means — a log string, an imported
API, a distinctive constant — compute its slot from the header, and subtract. Then verify the
alignment statistically before using any other slot.

### `CCamera` closes byte-exactly, and asymmetric projection is first-class

Applying the same discipline to the camera itself paid off, because R-042 had already measured two
things the header could be tested against: the size the copy stops at, and the exact shape of its
final instructions.

- **`sizeof(CCamera) == 0x240`**, computed from `Cry_Camera.h`, matching what R-042 measured.
- **Eleven independently observed offsets land on member boundaries**, spread from the first field to
  the last with no slack. Six come from R-039's decompilation of R-011's reads. Five come from the
  *copy widths* in `CCamera::operator=`: `m_pPortal` `+0x218` and `m_pMultiCamera` `+0x228` are copied
  as qwords because they are pointers, while everything around them moves as dwords.
- **The tail is the strongest single check.** The copy ends by merging one byte with masks `& 1` and
  `& 0xE` — a 1-bit field followed by a 3-bit field — and the header's last two members are
  `m_JustActivated : 1; m_sceneMaskFilter : 3`. Compiler output and header agree down to the bit.
- **The VR payoff: `m_asymL/R/B/T` at `+0x6C`/`+0x70`/`+0x74`/`+0x78`.** Four independent frustum
  shifts mean an OpenXR per-eye asymmetric projection is directly representable — no matrix injection
  and no restructuring of the camera. This is the concrete backing for the claim ARCHITECTURE has
  carried since the R-026 probe.
- **Two caveats that belong next to that payoff, not in a footnote.** The header annotates the
  asymmetry fields *"not used for culling atm"*, so setting them should be expected to change what is
  rendered without changing what is culled — invisible at a modest IPD shift, a correctness question
  at wide asymmetry. And `m_fp`, the `m_id*` index arrays and the twelve cached corner vertices are
  derived data stored *inside* the struct, so writing `m_Matrix` or `m_fov` directly leaves them
  stale. Both are now recorded in R-048 and against H-008.

Full table in [`CCAMERA_LAYOUT.md`](CCAMERA_LAYOUT.md).

### `CRenderView::SetCamera` characterised: the per-eye seam derives the projection for us

The seam H-008 recommended was still only known by name and signature. Decompiling it turned out to
settle both of the questions that were blocking the camera lane.

`CRenderView::SetCamera` is 842 bytes, RVA `0xEE7E80`..`0xEE81CA`, and does three things:

1. **Copies the camera by value.** It opens `MOV RDI,RCX; ADD RCX,0x11A0; MOV RBX,RDX` and calls
   `CCamera::operator=` with `RDX` untouched, so `CRenderView::m_camera` at `+0x11A0` (R-049) is a
   full `CCamera` copy, not a reference. **This makes the H-008 contamination structurally impossible
   on this path** rather than merely avoided by careful ordering: a camera written here cannot alias
   `CSystem::m_ViewCamera`, whatever the call order turns out to be. It also re-confirms R-042's
   identification of `CCamera::operator=`, since that is the function being called.
2. **Builds an orthonormal basis** from the camera matrix — normalising column 0, cross-producting
   against column 1, renormalising — then calls `CRenderCamera::LookAt` (R-051) with `Eye` = camera
   position, `ViewRefPt` = position + forward, and the derived up vector. The four-argument shape
   matches `IRenderer.h:504` exactly.
3. **Derives the frustum, and it reads the asymmetry fields.** With `t = tanf(m_fov * 0.5)`:

   ```
   fWL = m_asymL - t * m_ProjectionRatio      fWR = t * m_ProjectionRatio + m_asymR
   fWB = m_asymB - t                          fWT = t + m_asymT
   ```

   then stores near from `m_edge_nlt.y` `+0x4C` and far from `m_edge_flt.y` `+0x64` into the
   `CRenderCamera` block at `CRenderView+0x1620` (R-050).

**Two things follow, and both matter.**

- **Prey's asymmetric-frustum path is live in the render pipeline, not dead code.** The offsets
  `+0x6C`..`+0x78` were header-derived an hour ago; they are now read directly by a render-path
  function, which confirms the offsets *and* their use. `fWL/fWR/fWB/fWT` are frustum **tangents** —
  the same parameterisation OpenXR's `XrFovf` uses — so an OpenXR per-eye projection is expressed by
  writing four floats on the camera handed to `SetCamera` and taking `tan` of each eye angle. No
  matrix injection, no camera restructuring.
- **This seam is now strictly better than writing the system camera**, on two independent grounds
  rather than one: it is downstream of `GetViewCamera()` *and* it copies by value.

The culling caveat from R-048 still stands and is unaffected by any of this: the engine header marks
the asymmetry *"not used for culling atm"*, so the expectation should be that setting it changes what
is rendered without changing what is culled. That remains an open live test.

Verified on disk: the function's own bytes carry `ADD RCX,0x11A0` at offset `+0x25` and disp32 stores
at `0x1620`/`0x1630`/`0x1640`/`0x1650`/`0x1660` between `+0x2DE` and `+0x309`.

### `r_overrideDXGIAdapter` reaches the real device, which solves OpenXR adapter agreement natively

The cvar first showed up as a footnote in R-046, the spec-detect helper, where it hardly mattered —
that path creates throwaway devices. Following it to its other call site is what made it useful.

`GetOverrideDXGIAdapter()` (R-052) has exactly one caller: `InitializeD3D11DeviceAndSwapChain`
(R-025), the path that creates the device the game actually renders with. The call site is explicit:

```
CALL   GetOverrideDXGIAdapter    ; EAX = cvar value, or -1
MOVSXD RDI, EAX
MOV    R13D, R14D                ; default start index 0
CMOVNS R13D, EDI                 ; override >= 0 ? use it as the index
MOV    EDX, R13D
CALL   qword ptr [R9 + 0x60]     ; IDXGIFactory1::EnumAdapters1(index, &adapter)
```

**Why this matters.** `XR_KHR_D3D11_enable` requires the application's device to be created on the
adapter LUID `xrGetD3D11GraphicsRequirementsKHR` returns. On a hybrid-graphics laptop, or any machine
where the HMD hangs off a second card, that need not be the adapter Prey's own scan would choose —
and a mismatch there is not a subtle artefact, it is a failure to present. Prey already exposes the
control, so this is reachable **natively, with no hook**.

**Two constraints belong with the finding.** It is an adapter *index*, not a LUID, so the mod has to
enumerate adapters itself and translate. And it is read once during device creation, so it must be
set before the renderer initialises rather than adjusted at runtime.

This also settles what R-046 was worth. As a hook target it was nothing — telemetry that creates and
releases throwaway devices. Its value was entirely in the cvar it revealed, which turned out to
control a completely different code path.

### The layout constants are in the build, and the tests were checked by mutation

`EngineMap.h` now carries `CameraLayout`, `SystemLayout`, `GlobalEnvironmentLayout`,
`RenderViewLayout` and `RenderCameraLayout` in the existing `XxxLayout` idiom, with the two caveats
in comments beside the fields they qualify rather than buried in a doc.

The tests assert *relationships* rather than restating the values, since a test that repeats the
constant it is checking proves nothing: members must abut with no gap, the four asymmetry shifts must
be consecutive floats in the order `SetCamera` reads them, every offset must fall inside the `0x240`
that `operator=` copies, `CRenderView`'s camera copy must not overlap its `CRenderCamera` block, and
`CRenderCamera` must begin at its first member since it has no vtable.

**Those assertions were then verified by mutation**, not merely by passing. Changing `asymBottom`
from `0x74` to `0x78` — a plausible off-by-one-field slip — produced
`FAILED: asymR and asymB are adjacent`, exit 1. Reverted, rebuilt, green again, working tree clean.
A test that has never been seen to fail is not yet evidence of anything.

The artifact stayed `C35B22D3...` across this change, because the constants are header-only and
nothing in the DLL references them yet. That is a small free re-demonstration of the `/Brepro`
reproducibility from F-005.

### `SetCamera` is virtual, and a vtable pass turned up a hazard worth a rule

Trying to answer "what calls `SetCamera`?" statically found **no direct call xrefs at all** — only two
DATA references, one in `.pdata` (the unwind entry) and one in `.rdata`. It is a virtual, dispatched
through the `CRenderView` vtable, which is why the caller question stays live.

The `.rdata` reference plus `IRenderer.h:585` (index 8, counting the two inherited
`CMultiThreadRefCount` virtuals) puts the vtable at `0x1DCAE00` (R-053). Confirmed independently:
slot 23 predicts `EnableLookingGlass` and reads `0xEE4B00`, which is R-035 — a hit at the opposite
end of the table from the anchor. Practically, this means the seam can be taken by swapping a vtable
slot rather than patching bytes; the render views are pooled, so either technique affects all
instances.

**The hazard.** Slot 3, `GetFrameId`, reads `0x903CA0` — the same address as
`ISystem::GetGlobalEnvironment`. They are unrelated functions that happen to compile to the same five
bytes, `MOV RAX,[RCX+0x28]; RET`, and MSVC's `/OPT:ICF` folded them together. So **an address does
not identify a function**, and worse, **hooking a folded address hooks every caller of every function
folded onto it** — a bug that would present as unrelated subsystems misbehaving simultaneously.

The rule is now in `BUILD_BASELINE.md`: before hooking a short function, confirm its bytes occur
exactly once in the image. The uniqueness test already used for signature promotion answers this
directly. Checked for the three functions the camera lane would touch — `CSystem::GetViewCamera`,
`CSystem::SetViewCamera` and `CRenderView::SetCamera` — all unique, so none has a folding partner.
This is a second reason to keep R-040's eight-byte signature even though it spans the whole
two-instruction function: it doubles as the anti-folding proof.

## 2026-08-22 - Native stereo: both halves of the primitive are callable, and an old claim was wrong

A peer session's playbook chapter on native stereo — driving the engine's own world render twice per
frame with the camera moved, rather than patching matrices downstream — argued the deciding property
is whether the world-render is reachable as a **vtable slot** rather than inlined in the frame loop,
and that this is answerable statically. For Prey it is, and the answer is yes.

**The world render is a vtable slot.** `C3DEngine::RenderWorld(int nRenderFlags, const
SRenderingPassInfo&, const char* szDebugName)` sits at Steam RVA `0x21F520` (R-054). It was found by
its own `"RenderWorld"` profile marker and its four-parameter shape, then confirmed structurally: it
has **zero direct call xrefs**, only DATA references — a `.pdata` unwind entry and vtable slots — so
every call is virtual dispatch. `RenderWorld` is declared on `IProcess`, the base of `I3DEngine`, at
index 3, putting it at vtable `+0x18`; the `C3DEngine` vtable is at `0x1C912A0`. Alignment confirmed
by shape rather than assumed: slot 5 `SetFlags` is `MOV [RCX+8],EDX; RET` and slot 6 `GetFlags` is
`MOV EAX,[RCX+8]; RET` — a setter and a getter on the *same* member, both predicted by name.

**The camera enters through an argument, and that constructor is callable.**
`SRenderingPassInfo::CreateGeneralPassRenderingInfo(const CCamera&, uint32, bool)` is a real function
at `0x1E5B30` (R-055), not inlined — unlike `CreateRecursivePassRenderingInfo`, which is why the
recursive route looked closed earlier. It takes an **arbitrary camera**. It has exactly **3 direct
callers**, the small validator set the playbook asked for, and one of them shows the canonical
pattern outright: fetch `gEnv->pSystem->GetViewCamera()`, hand it to this function with flags
`0x2E5DF`, pass the result to a render call.

So the primitive is `RenderWorld(flags, CreateGeneralPassRenderingInfo(eyeCamera, ...), "EyeL")`, and
both halves exist as callable code. Corroborating evidence already in hand: the `e_ArkLookingGlass`
experiment showed Prey rendering a *complete* scene from a non-default viewpoint.

**What this does not establish.** Nothing has been called or observed executing. Render-target and
`CRenderView` lifecycle behaviour under a second pass is unknown, and render views are pooled `[2][2]`
(R-026) so a second pass may contend for them. `SwitchUsageMode` sequencing is untouched and cost is
unmeasured. H-009 records this as *preconditions met*, not viability established.

### A correction: the view-camera consumer count was off by an order of magnitude

The playbook's warning — enumerate what reads the camera before borrowing it, because the engine
keeps observing while you hold it — prompted a proper sweep, and it caught an error of mine.

H-008 said "both known consumers are now traced." That was wrong in scope. R-011 and R-016 are the
two consumers relevant to the **aim ray**; they are not the consumers of the **view camera**. A
byte-level sweep for `CALL [reg+0x388]` finds **144 candidate sites in `.text`, of which 84 provably
load `gEnv->pSystem` RIP-relative within 48 bytes**. They span rendering, gameplay, UI and 3D-engine
code. 84 is a lower bound: the 48-byte window is arbitrary, and some of the other 60 will be `ISystem`
too.

Two method notes worth keeping. The sweep's *call-site* detection is sound, but its
*enclosing-function attribution* is not — it scanned back to `CC CC` padding, and MSVC does not always
pad function boundaries, so R-011's own call site at `0x15854C7` was mis-attributed to the function
before it. That is why R-011 first appeared to be missing from its own result set, which is exactly
the kind of false negative that would have been believed had the list not contained a known answer to
check against. **Put a known result in the input whenever a new enumeration method is used.**

This correction does not weaken H-008's conclusion — it strengthens it, and it makes the choice of
seam decisive rather than merely preferable: `CRenderView::SetCamera` copies the camera **by value**
into the render view's own storage, so none of those 84-plus readers can observe it.

### Checking for a native override: Prey already had one, and we had already found it

A peer's chapter on injector modes made the point that before hooking a matrix write you should look
for an override the engine already honours — their example being AnvilNext reading a nullable
`worldMatrixOverride` pointer and adopting your view matrix before building the frustum. Worth ten
minutes on CryEngine. Three results.

**Prey has the equivalent, and it is R-009/R-010 — mapped back in July, but never framed this way.**
Re-verified the disassembly at `0x148BB59`:

```
MOV  RCX, [RDI + 0x148]    ; nullable custom-view callback
TEST RCX, RCX
JZ   0x18148BB73           ; null -> default camera-mode path (reads mode at +0x1A4)
MOV  RAX, [RCX]
MOV  RDX, RSI              ; RDX = SViewParams& , the out parameter
CALL [RAX + 0x10]
JMP  0x18148C5F4           ; skips the entire default path
```

The engine tests that pointer every frame and, when non-null, hands the callable the out-parameter
and jumps past everything else. **Prey's form is better shaped than a matrix-pointer override**: we
are handed the struct to fill rather than having to win a race against the engine's own write. The
supported installer, `SetCustomViewFunction`, is already a fail-closed gate landmark. The lesson is
about framing rather than discovery — this was in the registry for three weeks described as "a
seam we mapped", when it is actually the highest-value item in the whole camera lane.

**`CCamera` itself has no such override — and that is a definitive absence, not a failed search.**
Because the layout closes byte-exactly at `0x240` with no unexplained slack (R-048), every pointer is
accounted for: `m_pPortal` and `m_pMultiCamera`, neither a view-matrix override. Being able to say a
confident "no" is the payoff of having verified the layout rather than merely read it out of a header.

**One unexpected lead.** `m_pMultiCamera` at `+0x228` is commented *"Maybe used for culling instead of
this camera"* — a nullable pointer that substitutes a **different camera for culling**. That is the
shape of a native answer to R-048's own culling caveat: supply a combined symmetric frustum covering
both eyes for culling, while each eye renders asymmetrically. Consumer untraced; recorded as a lead.

**Also found: a detached-camera cvar family** (R-056) — `g_detachCamera` and five companions, plus
`e_CameraFreeze` and `e_CameraGoto`. Registration is confirmed twice over (the `Register` call binds a
backing int at `gameCVars+0x284`, and a cleanup pass unregisters the name), but **the consumer is not
traced**, so whether the path survives in this release build is unknown — registration alone does not
prove a live feature. If it is live its value is as a **zero-hook, reversible live experiment**: it
would decouple the view from the player using only the game's own cvars, showing whether the engine
tolerates an externally driven view camera and which systems object. It is a whole-view override, so
it is not a per-eye mechanism and not a native-stereo enabler.

### Tracing the `g_detachCamera` consumer: what I proved, and what I could not

The result is an honest inconclusive, so it is worth separating what is established from what is not.

**Established.** The six cvars are contiguous at `gameCVars+0x284`..`+0x298`, mapped by pairing each
`LEA R8,[RDI+disp]` in the registration function with its name string. The registration **is
reached**: Ghidra reported no callers, but a byte search found a tail `JMP` at `0x1727AA3` from a
small wrapper at `0x1727A90` -

```
MOV RAX, [RCX + 0xF8]   ; CGame::m_pCVars
TEST RAX, RAX
JZ  skip
MOV RDX, [RCX + 0x50]
MOV RCX, RAX
JMP registration
```

So the cvars struct is **heap-allocated and held at `CGame+0xF8`**, not a global - which is why every
global-based search failed. The cvars are genuinely registered and would appear in the console.
Nothing looks any of them up by name; the only two string xrefs are the registration and the
`UnregisterVariable` cleanup sweep.

**Not established: whether anything consumes them.** I could not find a consumer, and **that negative
is not trustworthy.** Following the rule from earlier in the session I seeded the search with control
cvars from the same struct, and it failed to find consumers for those too - including
`g_difficultyLevel`, which Prey unquestionably uses. A method that cannot find a known answer cannot
be used to assert an absence. Recorded as F-007 with the rules that came out of it.

**Strong circumstantial evidence that it is vestigial Crysis GameSDK code.** The registered help
strings are unmodified GameSDK boilerplate - *"Move speed turbo boost when holding down (360) A
button"* and *"Display debug graphics for detached camera spline playback"* - describing an Xbox 360
button and a cinematic spline system. The same registration block installs `g_mpNoVTOL`,
`g_mpHatsBootsOnRadar`, `g_maxGameBrowserResults` and `g_randomSpawnPointCacheTime`: multiplayer and
vehicle cvars for systems Prey does not have. Registering a cvar is one line; the feature behind it
can be entirely absent.

**Withdrawing my earlier suggestion.** I had floated this as an available zero-hook live experiment.
It should not be treated that way - the evidence points to dead code. And settling it does not need
more static analysis: one console command in a running game, `g_detachCamera 1`, answers it outright.
That is the cheapest next step, and it needs a live host.

Either way this was never a per-eye mechanism - it is a whole-view override, so it could not have
been a native-stereo enabler. Its only value would have been as evidence about whether the engine
tolerates an externally driven view camera.

## 2026-08-23 - Data gathering built ahead of the work that needs it

Live-host time is the scarce resource on this project. Static analysis runs unattended; every
question that needs Prey actually running costs an operator round-trip, a launch and a save load. The
expensive failure mode is discovering mid-implementation that we need one more number.

So the next three hurdles were named, their data needs written down, and the capture that answers
them built now rather than when we reach them. Recorded in
[`LIVE_CAPTURE_PLAN.md`](LIVE_CAPTURE_PLAN.md).

**The hurdles.** (1) First supported-host load. (2) OpenXR session and first HMD output. (3) Per-eye
camera. Each entry lists what only a live run can supply, and where the answer comes from.

**Capture A is built and wired.** `preyvr::snapshot` resolves `gEnv` from the module base, follows
`pSystem`/`pRenderer`/`p3DEngine`, checks each object's vtable against the RVA resolved statically,
confirms `IProcess` slot 3 really is `RenderWorld`, confirms `gEnv->pRenderer` and the
`CD3D9Renderer` singleton are the same object, and decodes `CSystem::m_ViewCamera`. It runs
read-only on load, after the landmark gate and before any hook is armed, so what it records is the
engine's own undisturbed state.

**The leverage: one load validates seven registry entries at once** - R-005, R-039, R-040, R-043,
R-044, R-048 and R-054. Those are all `static-only` today. A single supported-host run promotes the
whole cluster or tells us exactly which pointer is wrong, and it costs nothing extra because the mod
was going to load anyway.

**Design notes worth keeping.**

- *Capture wide, decide later.* Reading a whole struct costs the same as reading one field, and a
  logged blob can answer a question we have not thought of yet. Only the decoding is selective.
- *Emit verdicts, not numbers.* `IsPlausible` and `RenderCameraMatchesSource` mean the log says
  pass/fail against an expectation rather than leaving us to eyeball floats - otherwise reading the
  capture is another round-trip. `RenderCameraMatchesSource` is the sharpest of these: **one boolean
  on a live frame validates our entire model of `SetCamera`**, because it recomputes the derived
  frustum tangents from the source camera the way R-030 does and compares.
- *The reader fails closed.* `VirtualQuery` checks every page is committed and readable before the
  copy, chosen over SEH so a bad address is a returned `false` rather than a swallowed fault, and so
  the module stays `/EHsc`-clean. A partial snapshot still localises the first bad pointer, and the
  mod proceeds exactly as before.
- *The pure/Win32 split holds.* All the logic lives in `src/common` behind a reader callback, so it
  is testable headlessly against a synthetic address space; the DLL only supplies the guarded reader.

**Tests, and their limits.** 12/12 now, the twelfth being the snapshot fixture: decode round-trips,
plausibility rejecting what a wrong offset would produce, the `SetCamera` frustum formula, a
synthetic supported engine, and five negatives that each break exactly one thing. Mutation-checked -
decoding the near plane from `m_edge_nlt.x` instead of `.y` produces
`FAILED: near and far are read from the edge vertices, not adjacent fields`. **But these verify the
decoding and capture logic, not the offsets.** The offsets are verified against the installed
`PreyDll.dll` by the build doctor and by the static analysis in the registry. The two kinds of
verification should not be confused when reading a green run.

**Captures B, C and D are specified but not built** - device/swapchain/render-view reads, per-frame
change detection to derive the restore list, and three scenario captures. They are written as a work
order so the next session starts from a specification rather than a blank page.

Fresh loop 12/12, doctor 7 pass 0 warn 0 fail, all 30 landmarks matched. Artifact
`A37D4C004808322F24B3C660DA42CA7C679FD34906F407CF8FB765567989D8C1`.

## 2026-08-23 - The FC1 IK lineage does not survive into Prey, but its descendant is named and findable

A cross-engine peer supplied two facts from Far Cry 1's `CryAnimation/CryModEffIKSolver.cpp`, held as a
lineage oracle: the two-bone solver takes **no pole/hint input** (its bend plane is derived as
`n = a ^ c`, current upper-arm crossed with shoulder-to-goal, with a `nlen < 1E-6` degenerate bail),
and `SetGoal`'s `goal_normal` is **not** an elbow hint but an end-effector twist correction applied
after the solve. They graded both INFERENCE for Prey and said the value was vocabulary and shape --
`SetGoal`, `ApplyToBone`, `m_additLen`, a bend plane from `a^c` -- to make a descendant findable.

**Tested the vocabulary against the binary. It does not survive.** No `IKSolver`, no `SolveIK`, no
`m_additLen`, no `ApplyToBone` anywhere in `PreyDll.dll`; the only `SetGoal` hits are unrelated
(network serialisation, `ArkTurret` orientation). The Chairloader PDB headers have **no `CryAnimation`
directory at all** and zero `IK` matches across all 1,131 files.

**But the generation that did land is named, and it is more useful than the one we were looking for.**
Prey implements CryEngine 3's `IAnimationPoseModifier` architecture, with 21 modifiers registered by
name:

- `AnimationPoseModifier_Ik2Segments` — the two-bone solver, direct descendant of the FC1 code
- `AnimationPoseModifier_LimbIk`, `_IKTorsoAim`, `_PoseAlignerChain`, `_ConstraintAim`, `_Recoil`,
  `_PoseBlenderAim`, `_PoseBlenderLook`, `_LookAtSimple`, `_TransformationPin`, and others
- **`IKLIMB_LEFTHAND` / `IKLIMB_RIGHTHAND`** — named limb identifiers, with `CreateIKLimb`, `IKLimbs`,
  and `LimbIK_Definition` / `IK_Definition` / `AimIK_Definition` / `LookIK_Definition` CHRPARAMS keys
- PoseAligner exposed at runtime through `a_poseAlignerEnable`, `a_poseAlignerForceLock` and five more
- `CPoseModifierSetup` as a serialised, data-driven modifier stack

**Why this matters more than confirming the tip would have.** A named per-hand IK facility with a
factory entry point is the same shape as R-009's custom-view callback and R-052's adapter cvar: an
override the engine already honours. That is the third instance of this pattern in Prey, and it is
becoming the first thing worth checking in any new lane rather than an occasional lucky find.

**Confidence.** Strings only. No address, no call site, nothing traced or executed. The peer's two FC1
facts remain INFERENCE for Prey and are recorded as lineage context, not as claims about this binary —
and the specific mechanism they warn about (`goal_normal` mistaken for a pole vector) cannot be
transferred, because the function it describes is not present. Recorded against H-005.

**Second item, recorded against H-009 and not adopted:** bioshock-trilogy-vr gates its second world
pass deny-by-default on the return RVA of a known gameplay caller, plus camera-silent, present-stall,
teardown and poison gates. For Prey this is a strong fit for a reason specific to what we found:
`C3DEngine::RenderWorld` is virtual with zero direct call sites, so a second pass we drive is
indistinguishable from the engine's own by signature alone. Who called us is the only discriminator
available.

## 2026-08-27 - Playbook sweep: a threshold I had not derived, and the OpenXR-to-CCamera solve

Reviewed the cross-engine playbook against our open lanes. Four things fell out, two of which changed
code.

**1. A validation threshold I had guessed.** `RenderCameraMatchesSource` shipped four days ago with
`tolerance = 0.001f`, a number chosen by eye. Appendix A3.4 is a correction the playbook made to its
*own* earlier advice, and it lands directly on that: a residual limit is a property of a metric, not of
a problem, and FarCry2-VR imported another project's limit and got a gate looser than no gate at all.
Worse, the obvious projection check is mathematically blind to a wrong FoV -- a guessed 75 degrees
scored identically to correct, because FoV lives in the scale terms and the affine test reads the
projective row.

Fixed properly: the function now exposes `RenderCameraResidual()`, and the limit is derived from a
separation table measured in the tests rather than asserted. Correct scores `0.000000000`; the tightest
wrong case, a `0.004` asymmetry error, scores `0.004000008` -- 40x above the `1e-4` limit; the others
land 600x to 7500x above. The tests assert the *gap*, not just the outcome, so widening the limit later
breaks a test.

I also corrected my own header comment during this: I had written "correct reproduces to ~1e-7" before
measuring. It reproduces to exactly zero, because the test recomputes with the same formula on the same
inputs. That distinction matters for the live capture -- the engine derives the block in its own float
ops, so a live residual will be non-zero from rounding alone, and `LIVE_CAPTURE_PLAN` now says to log
the residual as a **number rather than a verdict** so we learn where that floor actually sits.

**2. The OpenXR-to-Prey projection bridge is a direct solve, and it is now implemented.** A3.1 notes
`XrFovf` is four *signed angles* from the view axis, so their tangents are in the same units as Prey's
`fWL/fWR/fWB/fWT` (R-050). Inverting the `SetCamera` formula gives the asymmetry shifts outright:

```
asymL = tan(angleLeft)  + t*ratio        asymB = tan(angleDown) + t
asymR = tan(angleRight) - t*ratio        asymT = tan(angleUp)   - t      t = tan(fov/2)
```

`AsymmetryFromFovTangents()` implements it, with a round-trip test that pushes the result back through
the `SetCamera` formula and confirms the requested tangents come out. The guard test is that a
**symmetric FoV must yield all-zero shifts** -- a sign error survives every other check while quietly
symmetrising the eye, which the playbook names as the most common way to "fix" an asymmetric frustum
that looked wrong.

**3. A second-pass hazard class broader than render targets** (recorded against H-009). Any *per-frame
mutable state* touched by a pass run twice advances twice. SOMAVR hit this in two unrelated effects
with identical shape -- SSAO temporal phase, and a tone-mapping packet carrying exposure, white-cut,
fade and grain phase together -- so the second eye rendered at a different phase. The classifier is
whether a resource is **read before it is written** within a frame: carried state must be replayed,
intra-frame scratch must not be duplicated. Plus two traps: fixing a producer does not move its
downstream consumers, and a fix should be scoped to the narrowest predicate that reproduces.

**4. The playbook's own assessment of us matches ours**, which is worth noting because it is an
independent read: `stereo` partial/STATIC, `camera_tracking` partial/STATIC, `re_discovery` full, and
`xr_lifecycle` / `xr_input` / `ui_hud` / `hands_interaction` / `performance` / `audio` with no entry at
all. Its bottleneck line for us is "deferred CryEngine companions and render-view pools are not yet
exercised by a second eye" -- the same gate H-009 records, phrased from the render side.

Verification: 12/12, doctor 7 pass 0 warn 0 fail, all 30 landmarks matched. Artifact unchanged at
`959276B8...` -- the new functions are not referenced by the DLL, so the linker drops them, which is
the expected outcome for analysis code that only the tests and a future capture will call.

### Fleet briefing 2026-08-27: three items, and one that tested a decision from the same day

**1. The XR frame contract (XR-005), adopted before the code exists.** Wait once early, cache the
poses, render every camera from that same cached pose, submit from an end-of-frame hook after all
cameras have rendered, hand off last. A survey of eleven mods found **five got this wrong**. The value
here is entirely in the timing: we have not built the submission path, so adopting it costs nothing,
whereas five projects had to debug into it. The one legitimate deviation is re-polling inside the
render backend at draw time -- late-latching, head motion only, never the gameplay-aim pose. Recorded
as a binding constraint in `ARCHITECTURE.md` and against Hurdle 2. It also makes `RT_EndFrame` (R-003)
the natural submit site, which is already the frame observer's target.

**2. A challenge to the solve I implemented earlier the same day, checked and dismissed -- with a
limit recorded.** The briefing warns that a projection rebuilt from four tangents discards the shear
canted displays fold into the matrix, and advises treating a reachable matrix as opaque instead. That
lands directly on `AsymmetryFromFovTangents`, so it was worth testing rather than filing.

It does not apply to this seam. `CRenderCamera` (`IRenderer.h:545-549`) stores ten scalars and **no
matrix** -- three basis vectors, an origin, four tangents, near and far -- and `GetProjectionMatrix()`
derives the matrix on demand. The class even exposes `Frustum(l, r, b, t, Ndist, Fdist)`, the canonical
off-axis setter. So four tangents is not a lossy reconstruction of Prey's projection; **it is Prey's
projection.** And OpenXR's `XrFovf` is itself four signed angles, so the runtime never asks for shear
-- a canted display lives in the per-eye view pose.

The honest converse is now recorded in `CCAMERA_LAYOUT.md`: Prey's `CRenderCamera` therefore *cannot*
represent a sheared projection at all, and if that is ever needed it must be met downstream, which is
exactly where the briefing's advice would apply. Plus an open question the check surfaced -- whether
anything downstream perturbs the derived projection, TAA sub-pixel jitter being the common case. Added
to the matrix if it composes, harmful if it perturbs the frustum. No pass census exists yet.

**3. `CreateIKLimb` became META-006.** The "enumerate what the engine already honours before designing
a hook" generalisation was adopted fleet-wide, reached independently by four projects, with our
instance cited. `ARCHITECTURE.md` now records it as a cross-fleet pattern rather than a local
observation, which is the correct weight for something four projects converged on.

## 2026-08-27 - Capture B's memory-only half, built ahead of tomorrow's session

The operator tests tomorrow, so the question was what else a single session could harvest. Capture B
was specified-not-built, and its memory-only portion turned out to be entirely reachable without any
COM call -- so it is now shipped, behind an export.

**Why an export and not the load path.** Hurdle 1's job is to prove a lifecycle that has never run,
with the smallest possible payload. Adding a second capture to the automatic path would mean a failure
there could be mistaken for a lifecycle fault. `PreyVR_CaptureRenderViews` is called explicitly, and
refuses outright unless the gate has already verified the host. The automatic load path is unchanged.

**It implements R-033's own acceptance test.** That entry has been sitting at `static-only` with an
explicit instruction -- confirm all four `m_pRenderViews[2][2]` entries are non-null heap pointers and
that `[t][1]` differs from `[t][0]` before treating the pool as usable. The capture runs exactly that
and reports the verdict on its own line. Each entry's vtable is checked against R-053, so a wrong pool
offset is *reported* rather than decoded into plausible-looking garbage -- the same identity discipline
Capture A uses for `CSystem`.

For each live view it decodes `m_camera` (R-049) and the `CRenderCamera` block (R-050) and logs the
residual **as a number**. It also captures the R-026 per-frame block as an independent cross-check --
R-026 is `reproduced` from a live ReGenny probe while R-033 is `static-only`, so if they disagree the
better-evidenced one wins.

**A citation error found while doing this.** `LIVE_CAPTURE_PLAN` credited the `CRenderer+0x6F38` pool
to R-026. It is R-033. R-026 is a different thing entirely -- the per-frame block at `+0x4A08`, stride
`0x328`, with a `CRenderCamera` at `+0x240`. Both are now captured, correctly attributed.

**Deliberately still not built: the COM half.** `IDXGISwapChain::GetDesc`,
`IDXGIDevice::GetAdapter` and `ID3D11Multithread::GetMultithreadProtected` are read-only queries, but
they are calls into the game's own objects rather than reads of its memory. Only the pointer *values*
are captured. That line stays until Hurdle 1 has passed.

**Mutation-checked, with one honest detour.** Disabling the first clause of the R-033 distinctness
check did *not* fail the tests -- because the second clause independently catches the same synthetic
case. That is the check being robust rather than the test being broken, but it meant the first mutation
proved nothing. Bypassing the whole predicate produced
`FAILED: a pool whose recursive slot aliases the default fails R-033's test`, which is the result that
counts. Reverted; `git diff --stat` confirmed insertions only.

Fresh loop 12/12, doctor 7 pass 0 warn 0 fail, all 30 landmarks matched. New artifact
`A37D4C004808322F24B3C660DA42CA7C679FD34906F407CF8FB765567989D8C1`, exporting
`PreyVR_CaptureRenderViews` alongside the existing six.

## 2026-08-29 - Hurdle 1 passed, and the live frame corrected our own formula

First supported-host load of the `0.3.0` artifact. Full record in
[the capture trace](../captures/traces/2026-08-29-prey-hurdle1-live-capture.md); this entry keeps what
changes how we work.

**Hurdle 1 met every acceptance criterion.** `status=verified landmarks=30`,
`snapshot result=complete`, all identity checks `yes`, 0 landmark mismatches. Seven registry entries
moved from `static-only` to `reproduced` in a single load: R-005, R-039, R-040, R-043, R-044, R-053,
R-054, plus R-012 and the R-006/R-007 mirrors. The renderer check was the sharp one --
`gEnv->pRenderer` and the singleton at `+0x2B3E8E0` both returned `0x7FFD16674E80`, which was a
prediction rather than a reading.

**The residual paid for itself on its first live frame.** It scored **1.5588** against a `1e-4` limit.
The cause: `SetCamera` multiplies the tangent by the **near plane** before applying asymmetry, so
`fW*` are glFrustum near-plane coordinates rather than raw tangents. We had dropped that factor when
writing the formula down -- and the giveaway, `fWT / tan(fov/2) = 0.100000002`, is exactly the near
plane. With `t' = tan(fov/2) * near` the residual is `1e-9`.

Two things about this are worth keeping. First, **the error was a misreading of our own
decompilation**: `fVar8 = fVar8 * fVar1` (with `fVar1` = near) was present in the output we quoted in
the research log on 2026-08-22 and was simply not carried into the formula. Static analysis produced
the right bytes and the wrong reading. Second, **logging the residual as a number rather than a
verdict is the only reason this was diagnosable from one sample.** A bool would have said "no" and we
would have been guessing between a wrong offset, a wrong view, and a wrong formula. That decision was
made on 2026-08-27 for exactly this reason and it worked.

The fix is in `RuntimeSnapshot`, and there is now a regression test built from the **actual captured
frame** -- it asserts the corrected formula reproduces the real values to near the measured live floor,
and separately that omitting the near factor scores `> 0.5`. That test would have caught this on day
one.

**Both write seams have exactly one caller.** `CRenderView::SetCamera` runs 33/s on thread 44208 from
`PreyDll+0x2110E5`; `C3DEngine::RenderWorld` runs 33/s on the same thread from `PreyDll+0xE0BC62`,
with `szDebugName="CSystem::Render"` on every call. Presentation is a **different** thread (54600) at
144/s. That is precisely the XR-005 shape -- cache on the game thread, submit from the render thread --
and a single caller each makes the playbook's deny-by-default return-RVA gate trivial rather than
aspirational.

**R-033's acceptance test passed**, promoting the pooled `CRenderView` array to `reproduced`. The
`[t][1]` recursive slots are permanently allocated but hold untouched defaults (640x480, `fW -1,1,-1,1`,
far 10), so they are idle rather than in use.

**An assumption of mine was wrong about adapters.** I had written that mismatch was "unlikely on a
single-GPU machine". Enumeration returns **five** adapters, four of which are identical
`NVIDIA GeForce RTX 5070 Ti` entries with the same 15995 MB, distinguishable **only by LUID**. Adapter
selection here cannot be done by name or memory; the LUID-to-index translation for
`r_overrideDXGIAdapter` is necessary rather than precautionary.

**Tooling, recorded as F-008 and F-009.** x64dbg's `loadlib` hijacked and suspended Prey's main thread,
froze the game, and never loaded the DLL; Frida injected cleanly first time with no pause. And two of
our own exports raise `system error` under Frida -- the observer *enable* took effect anyway, but
**disable did not**, so the documented "disable restores the target prologue" behaviour remains
unobserved on a live host and must not be described as proven.

**Verification:** 12/12 including the new live-frame regression, doctor 7 pass 0 warn 0 fail, all 30
landmarks. Artifact `407733D4D9089C3292E918607E1479E4CFB43AB111874EC8A9085BE08AFC28BB`.

## 2026-08-29 — exhaustive live harvest, and the Ghidra cross-check of the call sites

Same live session as the entry above, continued at the instruction to gather everything needed now
or later. Prey PID `50832`, all reads through Frida, no writes to game memory. Full detail in
`captures/traces/2026-08-29-prey-hurdle1-live-capture.md`; ten new registry entries, R-057 to R-066.

**The rate discrepancy was my measurement, not the engine's behaviour.** Measured in one window
instead of several, `RT_EndFrame`, `Present`, `SetCamera` and `RenderWorld` all returned exactly 432
calls in 3.001 s — 144/s, 1:1, once per frame each. The earlier 33/s figures came from sampling
different windows while the framerate moved. Comparing rates across separate windows is invalid when
the rate is not stationary. The structural conclusions drawn from those samples were unaffected, but
the numbers in that table were wrong and are now superseded in the trace.

**Named the call sites in Ghidra, which is what the live census could not do.** The chain is now
named end to end: `CSystem::Render` (R-058) -> `RenderWorld` (R-054, via `CSystem::m_pProcess` at
`+0xAB0`) -> `C3DEngine::UpdateRenderingCamera` (R-057) -> `CRenderView::SetCamera` (R-030).
`get_function_callers` confirms `UpdateRenderingCamera` has exactly one caller, and the live census
confirms it is the only site reaching `SetCamera`.

`CSystem::Render` turned out to re-confirm four offsets we had established separately — `+0x788`
`m_ViewCamera`, `+0x28` `gEnv`, `gEnv+0x08` `p3DEngine`, `gEnv+0x120` `pRenderer` — all used together
in one twenty-line function. It also shows `m_ViewCamera` is the **single source** feeding both the
3DEngine camera update and `CreateGeneralPassRenderingInfo`, which is the structural fact H-008
needed.

**Found a camera override the engine already honours.** `UpdateRenderingCamera` branches on
`if (!e_CameraFreeze && !e_CoverageBufferDebugFreeze)`; the else-branch feeds `SetCamera` from
`gEnv->pSystem->GetViewCamera()` and skips updating the culling camera at `C3DEngine+0x610`. I
resolved the cvar-to-offset binding live rather than guessing it, by looking each name up through
`pConsole->GetCVar` and comparing the backing pointer at `ICVar+0x48` against the predicted field
address. `e_CameraGoto` and `e_Recursion` were run as negative controls and correctly matched
nothing — without them the four positives would not have meant much. This is recorded as evidence
about the engine's plumbing, **not** as a recommended seam: freezing the culling camera is a side
effect, not an option.

**A correction to something we had assumed.** R-033's `m_pRenderViews[2][2]` pool holds only 4 of the
**16** render views scheduled per frame. Fourteen are Shadow views (640x480, flags `0xAE5DE`) that
live outside the pool entirely. I found this by identifying `CRenderView+0x14` as `EViewType` and
`+0x10` as `EUsageMode` — the latter verified by tracking `SwitchUsageMode`'s argument through all
five modes. H-009 has to account for views the pool does not enumerate. The two Recursive pool
entries are allocated, correctly typed, and never scheduled or given a camera, so they are free.

**A new constraint on Hurdle 3 that static analysis had not surfaced.** `SetPreviousFrameCamera`
(R-064) fires immediately after *every* `SetCamera`, with a different camera. `r_MotionBlur = 2` and
`r_AntialiasingMode = 3` are both live, so it is load-bearing for motion vectors and temporal
reprojection: a per-eye write that handles only `SetCamera` will give the second eye the wrong
reprojection history.

**The frustum does not jitter.** Sixteen consecutive `SetCamera` calls produced one distinct frustum
and a residual of `9.519862e-9`, min equal to max. That fixes the live rounding floor four orders of
magnitude below `kRenderCameraResidualLimit`, so the limit is comfortable in both directions. Where
the temporal jitter is applied is still open — it is just not in `CRenderCamera`.

No code changed in this entry; documentation and registry only.

## 2026-08-29 — the adapter LUID probe, and an OpenXR version rejection found for free

The one Hurdle 2 question the in-process capture could not answer was the LUID
`xrGetD3D11GraphicsRequirementsKHR` returns. It needs an XR instance rather than a running Prey, so
it was always separable; `tools/xr_adapter_probe` now answers it out of process, with no injection
and no writes. It is the first code in this project that actually calls into OpenXR — everything
before it inspected the loader's *files*.

**It found a blocker before the blocker could cost a session.** The project pins OpenXR-SDK 1.1.60,
so `XR_CURRENT_API_VERSION` is 1.1.60, and `VirtualDesktopXR 1.0.10` rejects that outright with
`XR_ERROR_API_VERSION_UNSUPPORTED`. Requesting 1.0 succeeds. Recorded as F-010. What makes this worth
writing down is that **every other precondition looked green** — preflight `status=ready`, loader
present and x64 and exporting `xrGetInstanceProcAddr`, 31 extensions advertised including
`XR_KHR_D3D11_enable` — and none of those checks touch `xrCreateInstance`. A file-inspection
preflight cannot tell you the runtime will refuse your instance. This would have surfaced tomorrow as
an unexplained failure with a headset on and Prey running, which is the most expensive place to find
it.

**The adapter enumeration reproduced the in-process capture exactly**: five adapters, four identical
RTX 5070 Ti entries plus the Microsoft software adapter, same LUIDs in the same order, `0x15533` at
index 0. Two independent observations — one from inside Prey through its own factory, one from a
separate process — now agree, which retires any doubt about the order `r_overrideDXGIAdapter` indexes
into.

**Two design points the run itself taught me**, both fixed in the probe rather than noted and left.
The loader refuses `xrResultToString` without a live `XrInstance`, which is exactly when a failure
most needs naming; my first run printed `XrResult_-4` and I initially read that as
`XR_ERROR_INITIALIZATION_FAILED`. It is `XR_ERROR_API_VERSION_UNSUPPORTED`. A twenty-line fallback
table for the common negative results removed the guesswork and immediately corrected me. And a probe
that returns on its first failure wastes the run: enumerating the adapters anyway costs nothing and is
half the answer, which is why the headset-off run above is still useful.

**Still open, and now one command away.** `XR_ERROR_FORM_FACTOR_UNAVAILABLE` is the correct result
with no headset powered, so the probe is verified end to end up to precisely the point that needs
hardware. Tomorrow's session gets the LUID and the `r_overrideDXGIAdapter` index from one run with
nothing attached to Prey.

**Verification:** clean build with no warnings, 12/12 tests pass.

## 2026-08-30 — building the stereo, 6DoF and motion-controller stack

A long build session with no game running and no headset, aimed at exhausting
what could be built toward the three targets. Eleven commits. The organising
decision was to keep splitting problems until each piece could be verified
without hardware, and the split that mattered most is described under A2 below.

**Instrumentation first, because the next experiment's acceptance criterion is an
image.** "Does writing `m_ViewCamera` change the picture?" had exactly one
instrument until today: a person looking at a monitor. That is not a measurement
— it cannot be automated, compared against a prior run, or distinguished from
"something else moved". So the session started with a frame-capture path
(backbuffer readback serviced from the existing `RT_EndFrame` observer, which is
the only correct place given `ID3D11Multithread` protection is off), a console
bridge behind an allowlist, and a differ that prints numbers and no verdict.

**The console bridge earns its place on determinism, not convenience.** Temporal
AA and motion blur are both live, so a *static* scene still differs frame to
frame. `t_Scale 0` plus `r_AntialiasingMode 0` is the difference between evidence
and noise, and without it every frame comparison in the rest of this entry would
be uninterpretable. The allowlist is fail-closed and bans command separators,
because without that `t_Scale 0; quit` passes a first-token check.

**Two engine findings came out of building the camera write.**
`CCamera::UpdateFrustum` at `0x121D70` rebuilds every cached field a matrix write
invalidates — the eight corners, the six planes at `+0x10C` exactly as
`CameraLayout` documents, the sign tables, and the cached position at `+0x230`.
It takes a `CCamera*` and touches nothing else, so the edit is applied to a
private copy, the engine function is run on *that*, and only then are the bytes
blitted. And its orthonormality predicate at `0x11A310` decides whether all six
plane normals get negated — so a denormalising write does not render slightly
wrong, it inverts culling. **It also has a hole:** an all-zero matrix satisfies
all nine of its comparisons. Transcribing it verbatim rather than paraphrasing
was not fastidiousness; paraphrasing flipped a sign on the first attempt.

**The split that made stereo testable without a headset.** Validating per-eye
camera construction and validating that Prey can render twice in one frame are
independent problems, and only the second is risky. With the scene frozen, a
left-eye frame followed by a right-eye frame *is* a stereo pair — so the whole
per-eye path is provable by alternating eyes across frames, with a synthetic IPD,
in flat Prey. That is A2. The double render is A3 and stays separate, so a crash
there cannot be ambiguous between "the engine cannot do this" and "the second
camera was malformed".

**The virtual VR view.** Anaglyph is the mode that matters: disparity appears
directly as colour fringing, so a swapped pair, a zero IPD or an inverted eye
offset are all obvious — and all nearly invisible side by side. The difference
mode encodes disparity as bar width, which is how a viewmodel drawn from a single
camera will announce itself, as a black region while the world behind shows
bands.

**Prey can do asymmetric projection, and Crysis could not.** `SetCamera` folds
`m_asymL/R/B/T` into the render frustum, so we take the proper per-eye path where
fholger's Crysis mod had to use a symmetric FOV and crop at submission. The
caveat is recorded where it will be needed: the engine header marks those fields
"not used for culling", so the render frustum will be per-eye correct while the
cull frustum stays symmetric, and geometry vanishing at the outer edge of each
eye is the expected symptom rather than a new bug.

**A viewmodel constraint, from static analysis.** `r_DrawNearFoV` is latched once
per frame by `RT_BeginFrame` into `CD3D9Renderer+0x95B4`, not read per draw. So
writing the cvar between two eye renders cannot give per-eye viewmodel FOV; the
latched field is the lever. The weapon renders at 54 degrees against the world's
88, so it does not share the world's projection and will not follow a per-eye
camera on its own.

**Motion controllers have a route into native gameplay.** Prey did not replace
CryEngine's input layer — `CActionMapManager`, `CActionMap`, `CMouse` on
DirectInput, the `i_xinput*` cvars — and named actions like `attack1`, `firemode`
and `reload` are present. So a synthesised action can reach every consumer a real
button does, rather than requiring a parallel simulation. Recorded as R-070; the
`pInput` slot and the `PostInputEvent` index still want a live process.

**What the maths layer asserts.** The OpenXR-to-CryEngine basis change is
Rx(+90), derived rather than guessed and checked to be a proper rotation, since a
mirror would make `UpdateFrustum` negate the plane normals. Rotations convert by
conjugation, pinned by the invariant that converting a rotated vector equals
rotating the converted vector — the tempting alternative of rotating the
quaternion's axis agrees on simple cases and diverges once the head tilts. The
asymmetry solve is verified by pushing the shifts back through the engine's own
`SetCamera` formula and checking all four frustum edges reproduce to 1e-6.

**Also settled from the previous session's leftovers:** the 33/s vs 144/s
discrepancy was a measurement fault of mine, R-033's pool holds only 4 of the 16
views scheduled per frame, and `UpdateRenderingCamera` has a shipped camera
override gated on `e_CameraFreeze`. And the OpenXR adapter probe found F-010
before it could cost a session: the pinned SDK is 1.1.60 and VirtualDesktopXR
rejects that outright.

**Verification:** clean build with no warnings, 19/19 tests. Nothing has been run
against a live Prey today — the game was closed before the session began, so
every protocol here is written and unexercised.

## 2026-09-01 - The renderer RT-function vtable, and the RT_RenderScene hunt

**Why this matters:** `CRYENGINE_SOURCE_FINDINGS.md` establishes that Crytek's own
stereo traverses the world once and submits the render view twice. That makes
`CD3D9Renderer::RT_RenderScene` the seam for rung 1b, and it is the target this log
named on 2026-08-15 and never followed.

**Found: the vtable it lives in.** `RT_EndFrame` (`0xF7E210`) has four DATA xrefs
and no code callers, because it is virtually dispatched. One xref, `0x181DD36D8`,
is a vtable slot. Reading around it resolves cleanly:

| slot address | target | identity |
| --- | --- | --- |
| `0x181DD36D0` | `0x180F7D710` | **`RT_BeginFrame`** (R-002, already gated) |
| `0x181DD36D8` | `0x180F7E210` | **`RT_EndFrame`** (R-004, already gated) |
| `0x181DD36E0` | `0x180F7DF50` | unidentified |
| `0x181DD36E8` | `0x180F7E820` | unidentified |
| `0x181DD36F0` | `0x180F7EB20` | **eliminated** -- resolution/viewport change |
| `0x181DD3700` | `0x180F532A0` | unidentified |
| `0x181DD3710` | `0x180F7E9E0` | unidentified |
| (nearby) | `0x180F7E920` | **eliminated** -- swapchain (`+0xAE88`), bumps a per-frame counter at `frameSlot*0x328 + 0x4C94` |

Two known-good anchors in one vtable is the systematic path: walk the remaining
slots and identify by **shape**, never by declaration order -- the FEAR VR prior art
records a case where slot 17 was a one-argument alias forwarding to the real
implementation in slot 19.

**The shape to match.** 5.x declares `RT_RenderScene(CRenderView*)`, one argument.
But this log already recorded Prey's as
`RT_RenderScene(CRenderView*, int, SThreadInfo&, void(*)())`, which is the **CE3.8**
form -- consistent with Prey being a CE3/4-era fork, and another instance of the
rule that the version gap moves symbols, not concepts. So the target is a function
of roughly five parameters (including `this`) that **calls one of its own
arguments** -- the `RenderFunc` callback. That callback invocation is the most
distinctive signature available and is what to grep the decompilations for.

**Not yet found.** Two of the eight adjacent slots eliminated. Left here rather
than guessed at, and the remaining work is mechanical rather than uncertain.

## 2026-09-05 — H-011 near-pass stereo, static route located

Followed R-069's latched near-FOV member rather than the five string lookups.
Located `UpdateNearestChange` (`0xF43D70`) and the separate near view-info route.
`0xFB0B70` explicitly clears translation in a copied view matrix; `0xFB2AC0`
multiplies that copy by the near projection into view-info `+0xA0`; `0xFB57A0`
transposes it into constant-buffer payload `+0x90`. Near asymmetry scaling is
already present in both routes. Supported Steam hash verified from disk.

[Investigation and proposed near-only discriminator](RE-H011-NEAR-PASS-STEREO-2026-09-05.md)
records arguments, branches, exact clearing instructions, upload layout and
remaining runtime proof. The user reserved the running process for another
agent; no hooks or game-state edits were made, and work continued statically.
A synthetic matrix check verified the proposed eye-relative translation formula
and inverse-depth disparity; it is not a gameplay or headset result.

## 2026-09-05 — H-005 finished-pose consumer located; capture identities corrected

Located `CCharInstance::SkinningTransformationsComputation` at Steam `0x82EE10`
from its job-name string and wrapper. It consumes character+0x960's absolute
pose, performs inverse-bind DualQuat conversion, and publishes to software
skinning before returning. `FX_UpdateCharCBs` (`0xF3CC20`) waits and uploads the
same bones; `FX_DrawBatchSkinned` (`0xF0EE30`) follows render-object+0x98 to them.
The proposed takeover supplies private modified absolute joints at conversion
entry. No live process access or deployment; the other agent owns that lane.

Two material corrections: remapped meshes share master bones/jobs/CB, so distinct
matrix pointers cannot be required for body/shadow mapping; R-077's active-path
RSI is a joint byte offset, making observed 0x4EC/0x7E0 candidate joint IDs 45/72.
RDI is a relative-pose array and R13 the default skeleton, not instance identity.
The joint-name/parent/count accessors and record layout are now byte-verified.

[Investigation, exact layouts and live discriminator](RE-H005-SKINNING-CONSUMER-2026-09-05.md).
The standalone read-only verifier `tools/re/verify_h005_skinning.py` passes 16
instruction/vtable checks against the supported module. This proves static
landmarks only; independent rendered-hand control remains untested.

## 2026-09-05 — H-005B model frame, native IK, and attachment contract

The subsequent live handover confirms R-084/R-085/R-087 and supersedes the
previous entry's untested hand-control status. This new investigation stayed
static and left the running game and mod implementation to the owning agent.

Located the entire render Matrix34 at object `+0`; traced entity character
render through `0x973400 -> 0x81BCB0 -> 0x81D0D0`. Near translation is relative
to camera position while its axes remain world-oriented. The existing marker
`0x81D377` follows skinning dispatch; proposed earlier observation is `0x81D272`,
with pool ID in EAX. Per-instance matrix/origin/epoch matching is still a live proof.

Native two-bone leaf `0x871CA0` is called by the animation-driven path and
mutates both relative and absolute pose arrays. It has no pole input, rejects
degenerate geometry and permits 25% segment stretch. CreateIKLimb and the
modifier stack have compiled evaluation paths. Found a concrete main-queue
defect: `0x8779C0..0x8779D2` repeats the first entry without pointer advancement;
the nested stack `0x7F31A0` iterates correctly.

Verified attachment SetAbs at vtable `+0x48` / RVA `0x828D70`: it copies the
default QuatT and invalidates projection. Inspected animation updates retain
that value and rebuild relative/current mounts. Absolute placement requires
`B * inverse(J) * G`, with an additional Prey mount quaternion on normal
static/execute paths. Same-frame consumption and ownership are separate from
setter persistence; skinning-only hand edits do not change the attachment's J.

[H-005B report and proposed live experiments](RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md).
`tools/re/verify_h005b_model_frame.py` passes 28 static landmarks and 8 synthetic
transform fixtures. No process attachment, runtime write, deployment or headset
test was performed by this investigation.

## 2026-09-05 — H-005C near selection, origin matching and native movement input

Resolved the RenderCHR entry predicate directly from instructions: params
`+0x80 & 0x800000` OR character `+0xAC8 & 2`. It exactly drives the eventual
object near bit, including an explicit clear for pooled objects. Slot near state
is a separate upstream matrix decision and is not a universal replacement.

GetViewCamera `0xDF2BB0` returns the edited `CSystem+0x788`. Ordinary entity near
matrices subtract its position; the slot+0x68 camera-space-position branch reads
only its basis. Matched native origins cancel eye translation algebraically.
The current NearViewStereo patch additionally subtracts a near eye delta, so the
consumer contract is `inverse(M) * (P - Ceye + d)`, simplifying to cyclops only
when near/world deltas agree. Identified two relevant source limits: conditional
keep-head-rotation skips the entire camera restore; RenderFrame's valid flag
does not synchronize overlapping plain matrix copies. Recorded an owned tuple
and bounded observation protocol rather than changing the owning agent's code.

Mapped the 56-byte Prey SInputEvent using native producers, dispatchers and
refire copies. Analog left axes are gamepad device 3, Changed state 8,
`xi_thumblx/0x210` and `xi_thumbly/0x211`. Action constructors connect
`xi_movex/xi_movey` to player handlers `0x158FD20/0x158FD80`, which write movement
`+0x5C/+0x60`. The bind matcher hashes input names, while device/index blocking
occurs upstream. Dispatch uses listeners and a map-selected entity, not every
NPC. Active XML bindings, gameplay ownership and neutralization remain live
checks; the installed PAKs were not readable as ordinary ZIPs.

[H-005C report](RE-H005C-SELECTION-ORIGIN-INPUT-2026-09-05.md).
`tools/re/verify_h005c_selection_origin_input.py` passes 19 static landmarks and
9 ABI/transform fixtures. No live access, runtime edits, event posts, build,
deployment or headset experiment was performed.
