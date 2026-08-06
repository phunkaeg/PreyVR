# Captures and runtime evidence

This directory stores large or generated evidence and is intentionally excluded from version control. Preserve small textual capture manifests when they support a documented finding.

| Folder | Contents |
| --- | --- |
| `renderdoc/` | `.rdc` captures plus a sidecar markdown/json manifest. |
| `eyes/` | final-eye, private-target, and depth visualization dumps. |
| `screenshots/` | annotated flat/VR comparison screenshots. |
| `traces/` | bounded API/input/timing traces. |
| `logs/` | per-run mod logs and crash evidence. |

Use a timestamped test identifier in every filename, for example `2026-07-30T2215Z-flat-bridge-frame-0042-left.png`. Record the game build hash, mod commit, OpenXR runtime, headset, test scenario, and result in the paired manifest or [`docs/TEST_PLAN.md`](../docs/TEST_PLAN.md).
