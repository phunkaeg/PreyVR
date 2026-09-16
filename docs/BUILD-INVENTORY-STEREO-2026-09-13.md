# Optimized stereo inventory development build

Package: `build/packages/PreyVR-inventory-stereo-prototype-20260913.zip`.
Development DLL: `build/inventory-stereo/Release/PreyVR.dll`.
SHA-256: `e49098b03cedf252ad1ee24d6f211318524e205c73783b720563f67613521880`.
Size: 921600 bytes. Source base: `1ac2d61` plus the uncommitted prototype changes.
No feature-branch merge, existing-package overwrite or live injection occurred.

This final build uses the working project's Release compiler/linker flags
(`/O2 /Ob2 /DNDEBUG`, x64, C++ exception unwinding). A first failed toolchain
detection had left empty flag cache entries; the earlier 2521600-byte build
passed its tests but lacked those optimizations. It is retained separately as
`build/inventory-stereo/PreyVR-e44703a2-before-release-flags.dll` for provenance.
The earlier arithmetic receipt and evidence files remain unchanged.

Final Release tests: **39/39 passed**. Logs are in
`docs/evidence/inventory-stereo-2026-09-13/release-validation/`.
Native arithmetic emulation also passed, as detailed in the
[prototype report](INVENTORY-STEREO-PROTOTYPE-2026-09-13.md).

**Not game-tested or headset-accepted.** Stereo is off by default. The pending
test first enables `ui.inventory 1`, `ui.depth 0`, `ui.stereo 1` and checks
same-frame raw images with `capture.inventory 1000`; only then should it try
nonzero depth. Two Omikron processes were still running at closeout, so the
user's fleet runtime exclusion prevented launching Prey.

The code graph was refreshed from the changed staged sources using the
documented `extract --force --code-only` command: 4805 -> 4876 nodes; existing
document-source coverage was retained. No semantic pass or fleet rebuild was
performed. The extractor still reports its pre-existing syntax limitation for
`src/dll/Bootstrap.h`; this is not evidence that those declarations are absent.
