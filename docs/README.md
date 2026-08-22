# PreyVR research notebook

The notebook separates evidence from plans so implementation follows demonstrated ownership rather than speculation.

> Resuming work? Read [`HANDOVER-2026-08-07-HARDENING.md`](HANDOVER-2026-08-07-HARDENING.md) first — current binary identity, lifecycle changes, verified state,
> traps found the hard way, open threads, and outstanding risks.

## Start here

1. [`BUILD_BASELINE.md`](BUILD_BASELINE.md) — exact game module identity and the Ghidra target.
2. [`RESEARCH_LOG.md`](RESEARCH_LOG.md) — chronological, source-backed findings.
3. [`ADDRESS_REGISTRY.md`](ADDRESS_REGISTRY.md) — build-gated hooks/globals/signatures.
4. [`SYSTEM_VTABLE.md`](SYSTEM_VTABLE.md) — derived `ISystem` vtable and `gEnv` layout; the bridge from a PDB name to a Steam address.
5. [`HYPOTHESES.md`](HYPOTHESES.md) — testable questions that are not yet findings.
6. [`ARCHITECTURE.md`](ARCHITECTURE.md) — target lane boundaries and proof ladder.
7. [`TEST_PLAN.md`](TEST_PLAN.md) — runtime proof milestones and capture requirements.
8. [`GHIDRA_SYNC.md`](GHIDRA_SYNC.md) — imported-program and annotation state.
9. [`RUNTIME_BASELINE.md`](RUNTIME_BASELINE.md) — observed modules and live trace anchors.
10. [`FAILURE_REGISTRY.md`](FAILURE_REGISTRY.md) — failed techniques that must not be repeated blindly.
11. [`HEADLESS_TESTING.md`](HEADLESS_TESTING.md) — fast verification lanes that require neither Prey nor a headset.
12. [`SMOKE_BUILD.md`](SMOKE_BUILD.md) — exact load/fail-closed contract for the signature-gated bootstrap DLL.
13. [`LIVE_A0B_WRENCH_PROTOCOL.md`](LIVE_A0B_WRENCH_PROTOCOL.md) — completed stack-local two-swing detached-melee proof and its validation criteria.
14. [`LIVE_INTERACTION_A0_PROTOCOL.md`](LIVE_INTERACTION_A0_PROTOCOL.md) — completed no-button, stack-local changed-use-target proof for the native interaction selector.
15. [`FRAME_OBSERVER_BOOTSTRAP.md`](FRAME_OBSERVER_BOOTSTRAP.md) — historical default-off EndScene proof plus the lifecycle-hardened current protocol.
16. [`LIVE_MULTIVIEW_PROBE_PROTOCOL.md`](LIVE_MULTIVIEW_PROBE_PROTOCOL.md) — completed read-only probe that closed the multi-view half of H-001 negative.
17. [`PRIOR_ART_FC2VR.md`](PRIOR_ART_FC2VR.md) — assessment of the Far Cry 2 / Dunia VR mod: an existence proof of the mod-owned per-eye route, with the failure analysis worth inheriting.
18. [`VR_MECHANICS_RISK.md`](VR_MECHANICS_RISK.md) — binary-grounded ranking of which Prey mechanics are hardest to bring into VR, and what to decide first.

Use the shared D3D11/OpenXR playbook as methodology, but do not copy its engine-specific assumptions into Prey findings.
