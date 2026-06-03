# app-vulkan-smoke — vk.icd Vulkan smoke test substrate port

## Upstream provenance

- **Reference patterns**: <https://github.com/KhronosGroup/Vulkan-Samples> — `samples/api/hello_triangle/` (device init) and `samples/api/compute_nbody/` (dispatch shape). Source not vendored; no host checkout required.
- **License**: Apache-2.0 (reference patterns only).

## Evidence

| Row | Status | Claim |
|-----|--------|-------|
| `vk.smoke` | `pass` | Host-side Vulkan API surface proof (alloc, map, fence) and Venus capset detection; vk.drm-shim (libukvirtgpu_drm) and vk.icd (Vulkan ICD) substrate implemented |

Current stage: the smoke substrate passes as a Venus/Vulkan readiness gate.
Rendering or compute throughput still requires a dedicated same-run QEMU/Venus
payload artifact and is not implied by this smoke test.

## Porting boundary

This port validates the vk.icd gate: headless Vulkan smoke test inside Unikraft proving Venus capset detection (id=4) via VirtIO-GPU controlq and documenting the path to the full Vulkan ICD.

Vulkan rendering and compute acceleration are not claimed — they require the complete Mesa Venus ICD backed by vk.drm-shim (`libukvirtgpu_drm`) and same-run frame proof.

## Unikraft build system

- `Config.uk` — declares `CONFIG_APP_VULKAN_SMOKE` and selects `libukvk_icd` (vk.icd), `libukvirtgpu_drm` (vk.drm-shim), `libukvirtio_gpu`
- `Makefile.uk` — registers with `addlib`, uses `APPVULKAN_SMOKE_*` variables, lists `main.c`; Vulkan loader not vendored
- `exportsyms.uk` — exports only `main`
- No dedicated `kraft/Kraftfile.*` — the smoke binary is exercised through `make vulkan-check` and the host Vulkan/Venus probe scripts.

## Omitted dependencies

- Vulkan loader (`libvulkan.so`) — not vendored; would require dynamic linking or static Vulkan-Loader
- Mesa Venus guest driver (`virtio_gpu_drm_icd.so`) — needs DRM ioctl emulation via vk.drm-shim
- SPIR-V shaders — not needed for Venus detection gate
- Window system (X11/Wayland/DRM) — not needed for headless compute

## Claim boundaries

**Allowed**: `vk.smoke` — Venus capset detection, vk.icd gate substrate, host-side Vulkan API proof.

**Forbidden**: Vulkan rendering, compute acceleration, GPU performance.

## Verification

```sh
make vulkan-check
make eval-check
make verify
```
