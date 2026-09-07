# RE investigation guidance: close the evidence chain

For Claude, Codex, or any agent working on PreyVR. Written 2026-09-07 from
[H-021 verification](RE-H021-STATIC-VERIFICATION-2026-09-07.md).
This is a working procedure, not a claim about one model's inherent ability.

H-021's useful discoveries show that the tools were usable. The avoidable
failures were retrieval, object identity, decompiler interpretation, and
premature conclusions. Tool availability can differ between sessions; check
the actual connection rather than assuming equal access.

## Before calling a static question unresolved

1. **Write the decision being made.** Example: “Can layer 16 place an ordinary
   modifier after ADIK?” State a falsifier: “An unsigned bounds check rejects
   16.” This makes the next tool call specific.
2. **Check existing evidence.** Query the project graph as AGENTS.md requires,
   check its date, then search the exact nouns/addresses in
   `docs/ADDRESS_REGISTRY.md`, relevant reports, and `tools/re/`.
   A graph miss is a retrieval miss, especially for recent work.
   H-005B already had PushPoseModifier's RVA, queues, and post-loop limitation.
3. **Preflight and pin the target.** Use Ghidra `list_instances`, then pass
   `program="/Prey/PreyDll.dll"` on each program-scoped call. Record module
   hash and distinguish VA from RVA. A registered tool is not an attached app.
4. **Find the cheapest discriminating read.** For an unknown virtual:
   constructor/accessor -> receiver base -> concrete vtable bytes -> slot
   target -> callee instructions. For an unknown field:
   producer/store -> receiver aliases -> consumer/read.
5. **Check instructions where the decompiler is uncertain.** On Windows x64,
   track RCX/RDX/R8/R9 and stack arguments at the call and at the callee;
   account for hidden return buffers and adjusted `this`.
   A displayed two-argument indirect call can still pass R8.
6. **Close with a bounded claim and a receipt.** Record the instruction address,
   object base, width/units, branch conditions, and one independent cross-check.
   Preserve a small offline byte/fixture check where it is useful. Say
   separately what still requires runtime observation.

## Three worked examples

**Layer routing.** Start with the old report's `0x839860`, verify vtable
`0x1D22D08+0x120`, inspect its comparisons. The result is ordinary layers
0..15, sentinel -1, rejection otherwise; an earlier identity-special case
exists. Do not transplant “all layers >=16 are post modifiers” from an engine
version. Read the consumer loop before recommending an append.

**Apparently conflicting signatures.** Pose constructor `0x87B670` installs
vtable `0x1D27228`. Its `+0x10/+0x30` targets are whole-QuatT setters.
Both read R8. The caller explicitly sets `R8=RAX`; the old decompile omitted
it. If a target has instructions but no function definition, inspect those
instructions and create the function at the proven boundary. “No function
found” does not mean “no code.”

**Field identity.** The loader stores the spawn name at `this+0x2E8`, but
the consumer reads weapon `+0x2F0`. The loader is on the secondary interface
at weapon `+8`, so these agree. Likewise, character `+0x610` aliases
animation `+0x4D0`. Searching only one displacement misses important writers.
Track byte offsets, typed pointer scaling, subobject offsets, and indirection
as separate operations on paper before naming the field.

## Common stopping errors and the next action

| Temptation | Better next action |
| --- | --- |
| “The header/source gives the layout.” | Use it as vocabulary; verify constructor, accessor, and concrete accesses in this build. |
| “The decompile shows one argument.” | Resolve the concrete callee and inspect register setup. |
| “That vtable address looks plausible.” | Check PE section and entry encoding. H-018's alleged wrench vtable was an unwind record in .pdata. |
| “This nonzero field must be the table count.” | Find initialization and at least one non-initializing writer, including receiver aliases. |
| “This function lacks the solver call, so the solver is skipped.” | Establish where the function sits in the frame. A post-processing branch can follow an earlier solver call. |
| “Weight 1 means exact endpoint.” | Read solver failure, reach, stretch and clamp branches; state their limits. |
| “The hook counter increased, so the feature works.” | Observe the selected rig and downstream output: final pose, attachment, or impact as appropriate. |
| “There is no scripted launch path, so I cannot test.” | Within the current authorization, inspect launcher/injector CLI, existing scripts, process-scoped xr-sim/xr-tape setup, and available tools. Name an observed blocker, not a missing convenience script. |

The last row is guidance for a separately authorized runtime task. This H-021
investigation was static only and did not take over the other agent's game.

## Keep uncertainty local

Use four distinct labels:

- **Observed static:** instruction/data, with address and receiver.
- **Source correspondence:** a name or interpretation matched to engine source.
- **Inference:** a prediction not yet established by the target evidence.
- **Observed runtime:** a named capture/fixture with scope and conditions.

A decompiler output, a Ghidra name, a report, and a source header can all repeat
the same original inference. Repetition is not independent confirmation.
Correct the registry and existing annotations when an inference is disproved,
so the next search does not recover the same error as a “known fact.”

Finish each small investigation with:

```text
Question / decision:
Producer and receiver:
Consumer and receiver:
Discriminating instructions:
Counterexample or rejected alternative:
Result and remaining runtime condition:
Replay command or evidence path:
```

Prefer this short evidence chain over another long narrative or blanket
instruction to “try harder.” These checks apply equally when reviewing your
own previous answer.

