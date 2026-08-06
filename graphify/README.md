# PreyVR Graphify setup

This folder builds a local Graphify knowledge graph combining the current PreyVR notebook with the existing cross-engine VR-mod documentation corpus at `D:\Dev Debug\VR Modding\cross-engine-graph\corpus`.

## Why it exists

The graph is for hypothesis generation, not proof. It helps surface recurring solutions and contradictions across engines (camera/culling, stereo route, screen-space passes, controller integration), while Prey-specific claims remain in `docs/` and must be supported by evidence.

## Run

```powershell
./stage-corpus.ps1
$env:OPENAI_API_KEY = '...'
./run-graphify.ps1 -Backend openai
```

`graphify` 0.8.36 is available locally. Staging copies markdown into the ignored `corpus/` working set; output is written to ignored `graphify-out/`.

Use `-Deep` only for a deliberate, higher-cost inferred-edge pass:

```powershell
./run-graphify.ps1 -Backend openai -Deep
graphify query "which cross-engine findings reduce the risk of native scene re-entry?"
```

Never place API keys in this repository or commit generated captures/graph output.
