# Source lanes

Create code under a lane only after its ownership is known:

- `dll/` and `lifecycle/` — module identity, logging, lifecycle, injection boundary, default-off frame observer, and Win32 OpenXR preflight.
- `graphics/` — DXGI/D3D11 observation, state guards, resource ownership, desktop preservation.
- `xr/` — OpenXR session, spaces, frame timing, swapchains, and composition.
- `engine/` — build-gated CryEngine camera/render/input integrations. No raw absolute addresses.
- `input/` — action mapping, controller pose/aim transforms, haptics, and gameplay hand-off.
- `ui/` — HUD/menu/viewmodel policy, separate from world stereo.
- `common/` — pure math, configuration, telemetry schemas, and build identity.

Do not put renderer mutations in the observation callback. The no-op EndScene observer lifecycle proof now passes; the next target is a default-off OpenXR instance/system-only smoke that creates no session or swapchain and always destroys its objects.
