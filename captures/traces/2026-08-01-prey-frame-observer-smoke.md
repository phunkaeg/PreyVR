# Prey frame-observer and OpenXR-preflight smoke

## Scope

Prove that the signature-gated DLL can load in the supported Prey process, observe `EndRendererScene` without mutating renderer state, remove its trampoline, and unload cleanly. OpenXR is preflight-only in this capture.

## Identity

- Host: `D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release\Prey.exe`
- Engine target: `PreyDll.dll`, SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- Artifact: `build/headless/Release/PreyVR.dll`, version `0.2.0-observer-bootstrap`
- Corrected DLL SHA-256: `8FDF164B2EC566E5E5E0AB2D22DF1A17F2B81B547C17818DFCA71A3CE5523150`
- OpenXR loader: release `1.1.60`, SHA-256 `6DF5C6ECBE0BDEB2ED91A3F92151C54328264C705981A19D768EB047192A914A`
- Debuggee PID: `783748`, x64, running under x64dbg

## Static and headless gates

- Ghidra decompilation at `PreyDll.dll+0xF7E210` confirmed the callback ABI as `void EndRendererScene(renderer*)`.
- The Release loop passed 10/10 tests after the final telemetry-ordering fix.
- Loading outside Prey remains fail-closed; runtime planning rejects a changed prologue, null base, and overflowing address.

## Supported-host result

The corrected DLL loaded at `0x7FFD775B0000`. All 21 mapped-memory landmarks matched. The observer planned target `PreyDll.dll+0xF7E210` at `0x7FFE1E99E210` and remained disabled until its explicit export was called.

OpenXR preflight reported:

```text
status=ready
runtimeSource=hklm64
runtimeManifest=C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json
loader=D:\Dev Debug\PreyVR\build\headless\Release\openxr_loader.dll
action=none
```

No OpenXR DLL was loaded by PreyVR, and no instance, system, session, swapchain, or D3D11 resource was created.

The observer was enabled for one bounded interval. Its log contained exactly one first-hit line, then the milestone and shutdown:

```text
preyvr_frame_observer_hit count=1 thread=780692 renderer=0x7FFE20544E80
preyvr_frame_observer_hit count=120 thread=780692 renderer=0x7FFE20544E80
preyvr_frame_observer_result status=disabled frames=301
```

An initial trial exposed a telemetry race: MinHook was published before the counter reset, allowing two `count=1` log lines. Initialization was moved before `MH_EnableHook`, the DLL was rebuilt, and the complete proof was repeated. The corrected run above has one first hit and a monotonic count.

## Restoration and safety

- After disable, the target bytes were restored to the exact promoted prologue:
  `40 57 48 83 EC 60 83 B9 F8 AE 00 00 00 48 8B F9`.
- x64dbg reported no software, hardware, or memory breakpoints.
- `FreeLibrary` unloaded `PreyVR.dll`; module lookup then failed as expected.
- Prey remained 64-bit, attached, running, and responsive.

## Interpretation

Lifecycle/build gate B0 and the no-op frame-boundary portion of G0 pass for this exact game build. This proves a clean render-thread observation seam; it does not prove stereo rendering, D3D11 state ownership, OpenXR frame timing, or headset output.
