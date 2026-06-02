# libukvirtgpu_drm

`libukvirtgpu_drm` is the vk.drm-shim gate Mesa/Linux virtgpu UAPI shim for Unikraft.
It translates `DRM_IOCTL_VIRTGPU_*` calls — the ioctl surface expected by Mesa's
Venus guest driver (`src/virtio/vulkan/vn_renderer_virtgpu.c`) — into VirtIO-GPU
protocol commands via `libukvirtio_gpu`. It is a thin translation layer: it does
not reimplement VirtIO-GPU protocol logic.

Current stage: `vk.drm-shim` passes as the virtgpu UAPI translation gate and
supports the passing Vulkan/Venus substrate. It is not a full Linux DRM device
node implementation.

Source lineage: Linux 6.18 `include/uapi/drm/virtgpu_drm.h`, Mesa
`src/virtio/vulkan/vn_renderer_virtgpu.c`.

## Configuring applications to use `libukvirtgpu_drm`

Enable `CONFIG_LIBUKVIRTGPU_DRM` in the Unikraft config. The library
automatically selects `LIBUKVIRTIO_GPU`:

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKVIRTGPU_DRM: 'y'
```

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the
include paths used by dependent libraries or applications (e.g. a Venus Vulkan
ICD shim for vk.icd).

## Public API

The public API is declared in `include/uk/drm_virtgpu.h`:

```c
int uk_drm_virtgpu_open(struct uk_drm_virtgpu_dev *dev, uint32_t gpu_idx);
int uk_drm_virtgpu_ioctl(struct uk_drm_virtgpu_dev *dev,
                          unsigned long request, void *arg);
int uk_drm_virtgpu_getparam(struct uk_drm_virtgpu_dev *dev,
                             uint64_t param, uint64_t *value);
int uk_drm_virtgpu_context_init(struct uk_drm_virtgpu_dev *dev,
                                 uint32_t capset_id, uint32_t num_rings);
int uk_drm_virtgpu_execbuffer(struct uk_drm_virtgpu_dev *dev,
                               const void *cmd, uint32_t cmd_size);
int uk_drm_virtgpu_resource_create_blob(struct uk_drm_virtgpu_dev *dev,
                                         uint32_t blob_mem, uint32_t blob_flags,
                                         uint64_t size,
                                         uint32_t *bo_handle_out,
                                         uint32_t *res_handle_out);
int uk_drm_virtgpu_map(struct uk_drm_virtgpu_dev *dev,
                        uint32_t bo_handle, uint64_t *offset_out);
int uk_drm_virtgpu_wait(struct uk_drm_virtgpu_dev *dev, uint32_t bo_handle);
void uk_drm_virtgpu_close(struct uk_drm_virtgpu_dev *dev);
```

Supported ioctls: `GETPARAM`, `CONTEXT_INIT`, `EXECBUFFER`, `RESOURCE_CREATE_BLOB`,
`MAP`, `WAIT`. Unsupported ioctls return `-ENOSYS`.

## Design boundaries

This library is a protocol-translation shim only. It does not:
- Reimplement VirtIO-GPU protocol or virtqueue submission (delegated to `libukvirtio_gpu`).
- Provide a file descriptor, mmap, or DRM device node (Unikraft has no fd namespace).
- Claim Venus Vulkan rendering without vk.icd (Vulkan ICD shim) also passing.
- Expose virgl or `SUBMIT_3D` paths for the K1 evidence row.

The `uk_drm_virtgpu_map()` function returns the CPU virtual address of the
host-coherent blob mapping as the "offset" field. In a full unikernel context,
callers use this address directly rather than calling `mmap()`.

## Verification

```console
make -C tests g5
make vk-drm-shim-check
make verify
```

The vk.drm-shim row in the evidence matrix (`results/vogue_evaluation_matrix.*`)
is promoted to `pass` when `virtgpu_drm_ioctl_test: all checks passed` appears in
the `make native-tests` output. `make verify` re-runs all gates including vk.drm-shim
and regenerates the evaluation matrix.
