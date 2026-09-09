# PreyVR — Claude guide

> Read `AGENTS.md` in this folder once as well; the pair is one instruction set.
> Current user scope and the project-specific restrictions below apply throughout.

## RE guidance

Use the shared workflow, skills and target tool constraints in `AGENTS.md`.

For static investigations, use [the RE investigation procedure](docs/RE-INVESTIGATION-GUIDE.md).
Check prior reports and byte verifiers before calling a gap unresolved. H-021
shows receiver-base, concrete-vtable and producer/consumer proofs against this target.


<!-- vr-research-integration:start -->
## Research receipts and playbook contributions

For a substantial VR research finding or failed approach, read
`D:/Dev Debug/VR Modding/docs/research-receipts.md` and use the
`vr-research-receipts` skill when available. `tools/research_receipt.py` in the
playbook creates/validates a project-owned receipt and queues a candidate for review.
Keep validity, fact verdict, baseline health, evidence grade and observation environment
separate. Raw evidence stays in this project; intake never promotes claims or clears
bottlenecks. Use the numerical camera-mapping recipe and `tools/research_checks.py`
when that is the cheapest relevant proof. Existing runtime/launch restrictions apply.
<!-- vr-research-integration:end -->
