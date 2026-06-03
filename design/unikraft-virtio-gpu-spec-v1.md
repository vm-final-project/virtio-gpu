# Unikraft VirtIO-GPU Venus/Vulkan API Specification v1

## Official Unikraft compliance checklist

This document defines the API/ABI contract for VOGUE's VirtIO-GPU stack.

### Version pins

- linux-version/linux-6.18: VirtIO-GPU host-side protocol definitions
- qemu-version/qemu-11.0: QEMU virtio-gpu-gl-pci device model

### Protocol capabilities required

- `VIRTIO_GPU_F_VIRGL` (bit 0) — virgl/3D context support; required for all `0x02xx` commands
- `VIRTIO_GPU_F_RESOURCE_BLOB` (bit 3) — blob resource support; required for `CREATE_BLOB`/`MAP_BLOB`/`UNMAP_BLOB`
- `VIRTIO_GPU_F_CONTEXT_INIT` (bit 4) — `context_init` + ring_idx support; required for Venus capset selection in `CTX_CREATE`; requires `F_VIRGL`
- `VIRTIO_GPU_F_BLOB_ALIGNMENT` (bit 5) — `blob_alignment` config field valid; requires `F_RESOURCE_BLOB`; when negotiated, blob sizes and `MAP_BLOB` offsets MUST be aligned to `virtio_gpu_config.blob_alignment`; `virtio_gpu_config` is 20 bytes (5 fields) when this feature is present
- `VIRTIO_GPU_CAPSET_VENUS == 4` — Venus capset ID (Mesa protocol)

### Library boundaries

- `libukvirtgpu_drm` — vk.drm-shim: DRM ioctl replay shim (host-kernel bypass)
- `uk_vgpu_probe` — VirtIO-GPU device probe and capset negotiation

### Out of scope (2026-05-22)

STK / `stk-code` / SuperTuxKart porting is **out of scope** for this project.
See `plan.md §0.5` for the full out-of-scope list (STK, APIR-support code,
and aliases). The `apps/app-stk/`, `kraft/Kraftfile.stk`, and `rootfs/stk/`
trees were removed on the `virt-gpu-spec` branch on 2026-05-22.

### Lineage transparency

Source lineage for this specification:
- `linux-version/linux-6.18` — VirtIO-GPU UAPI headers (`virtio_gpu.h`, `virtgpu_drm.h`)
- `qemu-version/qemu-11.0` — `virtio-gpu-gl-pci` device model with `venus=true`
- `venus-protocol` — Mesa Venus wire-format protocol (VIRTIO_GPU_CAPSET_VENUS = 4)
- `Unikraft` — target unikernel OS (`../unikraft`)

### Non-reimplementation boundaries

VOGUE does not reimplement:
- Mesa Venus guest driver (used as reference only)
- virglrenderer host-side acceleration
- Linux DRM/KMS kernel subsystem
