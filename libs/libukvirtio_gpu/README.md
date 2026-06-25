# libukvirtio_gpu

`libukvirtio_gpu` is VOGUE's VirtIO-GPU guest frontend for Unikraft. It exposes a
small 2D display API and a Unikraft virtio-bus backend for QEMU/PCI VirtIO-GPU
evidence. The backend also contains the control-queue surface required by
virgl/Venus work:
contexts, 3D submit, resource blobs, UUID assignment, map/unmap, and metrics.

Status: the backend reaches QEMU/Venus and passes the transport, ring, and llama.cpp-Vulkan runtime rows on the evaluation host.

## Configuring applications to use `libukvirtio_gpu`

Enable `CONFIG_LIBUKVIRTIO_GPU` and the VirtIO-GPU backend:

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKVIRTIO_GPU: 'y'
    CONFIG_LIBUKVIRTIO_GPU_BACKEND_REAL: 'y'
```

For QEMU appliances, `CONFIG_LIBUKVIRTIO_GPU_BACKEND_REAL` selects existing
Unikraft virtio bus, virtqueue, scatter-gather, and allocator libraries.
Host-native tests cover protocol and encoder code only; they do not include a
fake GPU backend.

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

- Backend: uses Unikraft's virtio bus/virtqueue APIs and sends real
  VirtIO-GPU controlq commands. It must not fork or reimplement Unikraft PCI
  discovery.

## Feature bit and wire-ABI notes

- `virtio_gpu_config` is **20 bytes** (5 fields): `events_read`, `events_clear`, `num_scanouts`, `num_capsets`, and `blob_alignment` (at offset 16).
- `VIRTIO_GPU_F_BLOB_ALIGNMENT` (bit 5) is negotiated when the host offers it, subject to `VIRTIO_GPU_F_RESOURCE_BLOB` also being offered.  When negotiated, the driver reads `blob_alignment` from config space and aligns all `RESOURCE_CREATE_BLOB` sizes to that value before issuing the command.  If the value is zero or not a power-of-two the driver falls back to 4096-byte alignment.
- `BLOB_MEM_GUEST` blob staging for Venus ring buffers works without `F_BLOB_ALIGNMENT`.  Host-visible blob mapping (`MAP_BLOB`) needs the virtio-pci SHM BAR helper `virtio_pci_shm_region_get`, which is provided by `patches/unikraft/0001-virtio-pci-modern-device-support.patch` (it parses the cfg_type 8 shared-memory capability and exposes the mapped window by region id).  When the device exposes no SHM region — e.g. a legacy/transitional transport — that path degrades to `-ENOTSUP`.
  - Operational note: a guest host-visible upload through this window still depends on the host completing the host-visible blob export. On a software host Vulkan driver (lavapipe over Venus) that export does not complete and the upload deadlocks, so the llama Vulkan appliances keep all weights device-local (`--no-host` / `mparams.no_host`) and do not exercise this path during model load.

## Design boundaries

The backend passes ABI/static readiness gates. QEMU 11.0 Venus transport succeeds after the modern VirtIO-PCI patch for device ID `0x1050`. Host-visible blob mapping is still blocked upstream; Venus compute routes through `SUBMIT_3D` instead (the current implemented path).

## Verification

Run:

```console
make -C tests proto-abi
make venus-check
make verify
```

Relevant result artifacts are written under `results/venus/` and
`results/vogue_evaluation_matrix.*`.
