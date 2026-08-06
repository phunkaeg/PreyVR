# ReGenny workspace

`PreyVR.genny` is the live structure notebook for the exact build recorded in `docs/BUILD_BASELINE.md`.

Rules:

- Start from a runtime object pointer captured by a proven call site.
- Confirm RTTI/vtable identity before assigning a class name.
- Add fields only after repeated live reads establish their offset and interpretation.
- Keep writes out of discovery sessions. ReGenny is read-only unless a specific reversible mutation test is documented first.
- Export generated C++ layouts into a future `include/preyvr/engine/` lane only after the layout is stable.
