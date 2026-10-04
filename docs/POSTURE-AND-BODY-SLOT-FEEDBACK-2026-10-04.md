# Posture setup and body-slot feedback

VR options now have a **Setup** tab with **Calibrate Current Posture** and
**Reset VR View**. Hold left Menu for about 0.65 seconds, release, and change
tabs with grips or use the controller beam/trigger.

Sit or stand comfortably, look straight ahead, then calibrate. This calls the
existing `view.calibrate` backend: it records the current height baseline and
resets view/controller reference together. Reset VR View preserves that height
baseline. The panel reports success, a busy reference, missing current tracking,
or the need to look straight ahead. Calibration is explicit; this does not add
avatar-height matching, automatic physical crouch or a seated gameplay mode.

Requests retain their action, session and one-second deadline as one unit. A
newer click replaces any pending request; busy retries cannot inherit a newer
click's metadata. Teardown and session changes discard stale requests.

**Abilities → Body Slot Feedback** enables brief lower-view cards for the
existing medkit and body-holster features. It is cosmetic and defaults on; the
underlying gameplay options still default off. It reports medkit selection,
confirmed consumption, absent stock or refusal, and holster assignment / native
stow/draw request acceptance. These are messages, not physical prop models or a
claim that the equip animation has finished.

Cards use the existing charcoal/amber/ivory interface palette, a separate sRGB
swapchain, and framing checked against both runtime eye frusta with a conservative
margin. They expire after 1.5 seconds and hide across menus, tracking/session and
reference changes. An unused medkit selection hides on grip release. Cards appear
only when the associated gameplay feature is enabled.

Release builds and **57/57 offline checks passed**. The production options and
feedback rasters were inspected; focused tests exercise busy retry, expiry,
superseded requests, native feature isolation, GDI resource cleanup and Quest 3
safe framing. These new UI changes have not been checked in a headset or injected
into the October 4 two-hand run.

## Next native interaction work

Lightweight left-hand grabbing is still static research. The carry target, release
and eligibility paths have been mapped on Steam; see
[the carry-seam research checkpoint](PHYSICAL-GRAB-SEAMS-2026-10-04.md).
No pickup, throw or native carry-state mutation is included in this checkpoint.
