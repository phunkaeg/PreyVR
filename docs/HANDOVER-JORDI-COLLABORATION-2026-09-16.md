# Prey VR — proposed collaboration and integration plan

Hey Jordi,

We’ve compared both codebases in detail, and there’s a useful opportunity to combine our work. The proposal is to **keep our PreyVR codebase as the integration foundation and bring across selected features and techniques from yours**, with you involved in deciding and implementing that integration.

Our strongest areas are the original arm/weapon rig, two-handed aiming, isolated HUD/inventory rendering, controller-ray UI interaction, and the supporting tests and diagnostics. Yours adds valuable work on locomotion, snap turning, physical crouch, native weapon-wheel selection, psi targeting and weapon-specific firing behaviour—plus the feedback from your Quest 2/3 sessions.

## Repository and integration approach

I suggest we work on an integration branch rather than rebuild from scratch. We can establish clearer interfaces and replace individual subsystems while preserving the working rig, UI and engine integration, along with their tests and research history.

If we’d prefer a neutral project name and shared ownership, we could create a shared repository seeded from our existing history. That would give us a common home without discarding the implementation or attribution.

This would be feature-by-feature integration, rather than combining both DLLs or hook systems. Your Chairloader-compatible EGS target and our Steam target have different binary contracts, so engine calls, layouts and offsets need verification wherever we port them. Portable logic can be reused or adapted once we agree terms; target-specific addresses cannot simply be assumed to transfer.

We should agree source-reuse and contribution terms first, since your repository currently lists its licence as TBD.

## Proposed responsibilities

| Area | Proposed lead |
|---|---|
| Stereo rendering, OpenXR lifecycle, frame/pose correctness | Our team |
| Original weapon rig, arm IK and two-handed support | Our team |
| HUD/inventory capture, curved panels and controller pointing | Our team |
| Locomotion, snap turning, crouch and comfort policies | You |
| Weapon-specific firing, psi and native wheel behaviour | You, with our support verifying Steam integration |
| Settings UX | You on design/policy; our team on integration with the VR interface |
| Physical melee and weapon collision | Shared: our team on tracked geometry and sweeps; you on native combat behaviour |
| Tests, packaging and release integration | Our team, with scenarios and feedback from both sides |
| Headset acceptance, control layout and release decisions | Together |

These are proposed leads, not restrictions. Rendering and gameplay overlap, so changes at those boundaries should get joint review. The split is based on the strengths visible in the current implementations, and we can adjust it around what each of us wants to work on.

## First: establish a trustworthy baseline

We need to close three things in our existing foundation:

1. **Missing-FOV handling.** Our submission path can substitute the runtime’s FOV when the rendered FOV is unavailable. That needs an explicit safe fallback.
2. **Render-pose provenance.** Submitted images must carry the pose, FOV and frame identity actually used to render them. Both inspected implementations have a gap here.
3. **Settings verification.** Read back critical CVars instead of treating successful command dispatch as proof they took effect.

We can handle those while we agree the collaboration details. We should freeze a reference build and use repeatable scenarios covering gameplay, loading, inventory, recentering and focus changes.

Alternating-eye stereo stays the baseline. Full-rate stereo remains a separate research effort until a viable approach passes correctness and endurance testing.

## First feature integrations

The initial gameplay additions would be:

- **Snap turning**, including hysteresis, deliberate release/rearming and menu/input ownership.
- **Selectable head-relative locomotion**, using the live head-to-body relationship and your movement-direction and speed findings.
- **Physical crouch and height calibration**, without stacking the engine’s camera drop onto real head movement.
- **Native wheel selection**, while preserving the agreed controller bindings.

We should expose shared **pose, aim, input and settings interfaces**, so these features don’t introduce competing camera or controller state.

Floor-relative tracking can help height matching, but it should be optional where supported, with a calibrated fallback. Simply switching LOCAL to STAGE won’t solve the complete height/stance problem, and neither physical crouch nor a movement vignette needs to wait for that switch.

## Next: consistent weapon behaviour

The next joint milestone is to combine our rig and two-handed pose with your firing-consumer knowledge, then verify these together:

- Visible barrel and muzzle position.
- Shot direction and native spread.
- Beam and muzzle effects.
- Actual impact and reticle position.
- Weapon changes while the controllers are held off-axis.
- Q-beam, secondary projectile effects and left-hand psi targeting.

The shared aim contract needs explicit player/weapon ownership so changes don’t affect NPC weapons, reflected beams or secondary projectile events unintentionally.

Physical melee and weapon collision follow once the weapon geometry and gameplay consumers are dependable. Our team can lead the tracked weapon volumes and sweeps; you can lead the native attack/damage integration, with both sides reviewing the result.

## UI and performance work

We would retain our isolated HUD/PDA capture and controller-ray interaction. Your settings UX and VIEW-space HUD placement are useful additions, with separate policies for head-fixed status information, world-targeted reticles and world-locked inventory.

Deeper inventory presentation and a live world behind it remain separate tasks. Keeping the game unpaused does not by itself make the world render behind the inventory.

We should also evaluate your independent-eye swapchains and projection fitting for performance. Those comparisons need matched visual quality, resolution coverage and measured timings; fewer copies or a smaller texture alone won’t establish the overall improvement.

## Testing and first release target

We’d retain automated checks and scripted scenarios, then use headset sessions to judge alignment, readability and comfort. We can adapt useful scenarios from your mock runtime, but its action naming/binding assumptions need adjustment before it can validate our input system.

Source inspection or a mock-runtime pass won’t be treated as headset acceptance. Each integrated feature should have a small acceptance checklist, with a reproducible build and a clear way to disable it if it regresses something.

**The first combined release target should be dependable stereo, comfortable movement, usable controller-driven menus and consistent weapon aiming.** We don’t need to finish every ambitious VR feature before producing something substantially better together.

Does that division fit the parts you’d enjoy working on? If so, I suggest we first agree reuse terms and pick one bounded integration—snap turning or native wheel selection would be good starting points—while we close the rendering correctness work.
