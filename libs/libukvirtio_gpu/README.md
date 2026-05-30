# libukvirtio_gpu

`libukvirtio_gpu` is VOGUE's VirtIO-GPU guest frontend for Unikraft. It exposes a
small 2D display API, a deterministic fake backend for native tests, and a real
Unikraft virtio-bus backend for QEMU/PCI VirtIO-GPU evidence. The real backend
also contains the control-queue surface required by future virgl/Venus work:
contexts, 3D submit, resource blobs, UUID assignment, map/unmap, and metrics.

## Configuring applications to use `libukvirtio_gpu`

Enable `CONFIG_LIBUKVIRTIO_GPU` and select exactly one backend:

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKVIRTIO_GPU: 'y'
    CONFIG_LIBUKVIRTIO_GPU_BACKEND_REAL: 'y'
```

For native unit tests, use `CONFIG_LIBUKVIRTIO_GPU_BACKEND_FAKE`. For QEMU
appliances, use `CONFIG_LIBUKVIRTIO_GPU_BACKEND_REAL`, which selects existing
Unikraft virtio bus, virtqueue, scatter-gather, and allocator libraries.

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the include paths used by dependent libraries or applications.

## Public API

The public API is declared in `include/uk/virtio_gpu.h` and includes:

- 2D resource lifecycle: probe, display info, create 2D, attach backing,
  transfer, flush, and fence wait.
- Capability discovery: feature flags, capset information, EDID, and capset
  names.
- 3D and accelerated-transport readiness: context create/destroy, resource
  attach/detach, `SUBMIT_3D`, blob create/map/unmap/destroy, UUID assignment,
  and metrics.
- removed: project standardises on Vulkan/Venus.

## Backend model

- Fake backend: deterministic in-process state machine for native tests. It is
  not a performance model and must not be used as QEMU evidence.
- Real backend: uses Unikraft's virtio bus/virtqueue APIs and sends real
  VirtIO-GPU controlq commands. It must not fork or reimplement Unikraft PCI
  discovery.

## Feature bit and wire-ABI notes

- `virtio_gpu_config` is **20 bytes** (5 fields): `events_read`, `events_clear`, `num_scanouts`, `num_capsets`, and `blob_alignment` (at offset 16).
- `VIRTIO_GPU_F_BLOB_ALIGNMENT` (bit 5) is negotiated when the host offers it, subject to `VIRTIO_GPU_F_RESOURCE_BLOB` also being offered.  When negotiated, the driver reads `blob_alignment` from config space and aligns all `RESOURCE_CREATE_BLOB` sizes to that value before issuing the command.  If the value is zero or not a power-of-two the driver falls back to 4096-byte alignment.
- `BLOB_MEM_GUEST` blob staging for Venus ring buffers works without `F_BLOB_ALIGNMENT`.  Host-visible blob mapping (`MAP_BLOB`) requires the upstream Unikraft virtio-pci SHM BAR helper, which is not yet available; that path returns `-ENOTSUP` until the upstream adds `virtio_pci_shm_region_get`.

## Design boundaries

The real backend passes ABI/static readiness gates. QEMU 11.0 Venus transport succeeds after the modern VirtIO-PCI patch for device ID `0x1050`. Host-visible blob mapping is still blocked upstream; Venus compute routes through `SUBMIT_3D` instead (the current implemented path).

## Verification

Run:

```console
make -C tests proto-abi
make venus-check
make stage-check
make verify
```

Relevant result artifacts are written under `results/venus/`, `results/stage/`,
and `results/vogue_latest_evaluation_matrix.*`.
