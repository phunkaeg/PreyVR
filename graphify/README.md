# PreyVR Graphify setup

This folder builds a local Graphify knowledge graph combining the current PreyVR notebook with the existing cross-engine VR-mod documentation corpus at `D:\Dev Debug\VR Modding\cross-engine-graph\corpus`.

## Why it exists

The graph is for hypothesis generation, not proof. It helps surface recurring solutions and contradictions across engines (camera/culling, stereo route, screen-space passes, controller integration), while Prey-specific claims remain in `docs/` and must be supported by evidence.

## What gets staged

`stage-corpus.ps1` mirrors the repo into the ignored `corpus/` working set:

| Group | Contents |
| --- | --- |
| markdown (recursive) | every `.md` in the repo — `docs/`, the `captures/traces/` evidence records, root `README`/`AGENTS`/`CLAUDE`, and per-directory READMEs |
| C++ source and headers | `src/`, `include/`, `tests/` — the layout headers carry the same verified offsets the documents describe, so they are the join between research notes and code |
| build and tooling | `CMakeLists.txt`, the research helper `.ps1` scripts, the ReGenny `.genny` notebook |

Files are flattened with their path encoded in the name (`docs/FOO.md` → `docs__FOO.md`) so two `README.md` files from different directories cannot collide and provenance stays visible in the graph.

**Deliberately excluded:** `build/`, `tools/Chairloader-*` (1,131 third-party headers — the PDB bridge we translate *from*, not this project's work, and it would swamp every other signal), and binary capture payloads.

## The `.graphifyignore` at the repo root is load-bearing

Graphify walks **up** from the scan root to the VCS root collecting ignore rules, and at each directory it reads `.graphifyignore` **in preference to** `.gitignore` — replacing it at that level, not merging.

The repo's `.gitignore` contains `/graphify/corpus/*`, because the staged corpus is a disposable mirror that must not be committed. Without an override, graphify honoured that rule and **excluded the entire staged corpus from its own scan** — a `detect()` run returned 1 file (`corpus/README.md`) instead of 228. Any graph built this way would have been silently near-empty.

`/.graphifyignore` fixes this: it mirrors `.gitignore`'s build/artifact exclusions but deliberately omits the `corpus/` rule. Keep the two files in sync by hand; that one omission is the only intended divergence.

## Run

```powershell
./stage-corpus.ps1
```

Then run the pipeline via the `/graphify` skill, which handles detection, AST extraction, parallel semantic extraction, clustering, and export.

**No API key is required.** Semantic extraction runs through Claude Code subagent dispatch — the host session is the LLM. Graphify reads `GEMINI_API_KEY`/`GOOGLE_API_KEY` if present to use Gemini instead, but reads no other provider key. `run-graphify.ps1` gates on `OPENAI_API_KEY`/`ANTHROPIC_API_KEY`; that gate reflects an older assumption and is not something the extraction path actually uses.

Output is written to the ignored `graphify-out/`. Intermediate state (`.graphify_ast.json`, `.graphify_detect.json`, the per-chunk JSON) persists there, and graphify keeps an extraction cache, so an interrupted run resumes without repeating completed work.

```powershell
graphify query "which cross-engine findings reduce the risk of native scene re-entry?"
```

Never place API keys in this repository or commit generated captures/graph output.
