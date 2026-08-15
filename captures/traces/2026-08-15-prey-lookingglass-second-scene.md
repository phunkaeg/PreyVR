# Prey Looking Glass second-scene probe — H-007

## Scope

Decide whether Prey's `e_ArkLookingGlass` second-scene path is a real scene render. Live session,
read-only except for two bounded cvar writes that were restored and verified.

## Identity

- Host: `Prey.exe` PID `82656`, x64, launched normally from Steam with no injection or wrapper
- `PreyDll.dll` mapped at `0x7FFFB8920000`; renderer singleton at `PreyDll+0x2B24E80`
- Save: opening apartment, player facing the window. Camera `vOrigin` `(863.30, 1584.70, 9.37)`

## Method: cvar writes without a console

Prey ships no developer console, which blocked this experiment for several sessions. It is not
actually a blocker: cvars are plain `int`s in a heap object, so they can be written directly.

The registration site was read from disassembly in `FUN_180235400`:

```text
; e_ArkLookingGlass
18023b24b: LEA R8,[RBX + 0x5c4]      <- target variable
18023b257: MOV R9D,0x1               <- default 1
18023b262: LEA RDX,[0x181c9c330]     <- name string

; e_ArkLookingGlassDebug
18023b293: LEA R8,[RBX + 0x5c8]      <- target variable
18023b29f: XOR R9D,R9D               <- default 0
18023b2a7: LEA RDX,[0x181c9c3c8]     <- name string
```

The cvar object is the pointer at `PreyDll+0x243A688`; this session it was heap `0x14B74E6ABC0`.
`+0x5C8` independently matches the decompiled consumer `*(int *)(DAT_18243a688 + 0x5c8)`, and both
fields read their registered defaults live (`1` and `0`) before any write. Two independent
derivations agree.

## Result: the second scene is real

With `e_ArkLookingGlass = 3` ("show only the second scene") the observer reported the display
switch to **the exterior view alone, with none of the apartment interior rendering**. Passing
through mode `0` ("disabled, renders normally") first showed what the observer described as a
**film studio** — the actual level geometry behind the Looking Glass illusion.

That is direct visual evidence that:

1. Two distinct scenes exist — the apartment interior and an exterior environment;
2. the second scene is a complete 3D render, not a texture, video, or skybox;
3. the engine can render that second scene **alone and full-screen**, with the main scene absent;
4. mode `1` composites both, so both are produced within a single frame.

Both cvars were then written back to their registered defaults and read back to confirm.

## What did not work, and why it is worth recording

**Scene-recursion depth was the wrong instrument.** R-028 (`m_SceneRecurseCount` at
renderer `+0xAEF8`) never exceeded `1` in any mode, at ~4,400 samples per frame. The hypothesis was
that a second scene would nest `BeginRendererScene`/`EndRendererScene`. It does not. The second
scene is produced without nesting that pair, so this counter cannot detect it. The measurement was
sound; the inference behind it was wrong.

**The timing share was noise.** Time spent at depth 1 was offered as a secondary signal. At mode 1
alone it measured `54.0%`, `41.3%` and `82.2%` across three samples at the same location. A mode-3
reading of `22.8%` was initially reported as meaningful; against that spread it is not. No
conclusion in this capture rests on the timing figures.

The decisive evidence came from setting the cvar and looking at the screen. Both instruments built
for this question failed; the cheap observation answered it.

## Interpretation and limits

H-007's core question resolves **positive**: a native path exists that renders a complete scene from
a non-default viewpoint to the full framebuffer, in the same frame as the main scene. That is the
capability the committed mod-owned per-eye route needs, and it is present in the shipping build.

Three limits, none of which this session addressed:

- **Two environments is not two cameras.** The second scene here is a *different* environment, not
  the main world from a second viewpoint. Stereo requires the latter. Whether the mechanism
  generalises to an arbitrary camera into the main world is unproven and is the next question.
- **Camera control is unproven.** Nothing here shows the second scene's camera can be set by a mod.
- **Cost is unmeasured.** Because the timing data was noise, this capture says nothing about
  whether a second scene render is affordable at VR frame rates.

## Safety

Two `int` writes to display-mode cvars, both bounded to documented ranges, both restored and
verified by read-back. No hooks, no injection, no wrapper, nothing written to the game directory,
and the game was launched normally from Steam. Prey remained running throughout.
