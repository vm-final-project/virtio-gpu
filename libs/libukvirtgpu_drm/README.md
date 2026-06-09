# libukvirtgpu_drm

`libukvirtgpu_drm` is the optional Mesa/Linux virtgpu UAPI compatibility shim
for Unikraft. Its core translator converts `DRM_IOCTL_VIRTGPU_*` calls — the
ioctl surface expected by Mesa's Venus guest driver
(`src/virtio/vulkan/vn_renderer_virtgpu.c`) — into VirtIO-GPU protocol commands
via `libukvirtio_gpu`. The optional fdio facade adds per-open render-node state
and mmap-offset resolution for Linux-style `open`/`ioctl`/`mmap` flows.

Current stage: `vk.drm-core` passes as the direct virtgpu UAPI translation gate;
`vk.drm-fdio` passes as the host-native fd-compatible render-node facade with
per-open mmap offsets. This is still not a full Linux DRM implementation:
syncobj and PRIME/dma-buf remain explicitly unsupported.

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

For Linux/Mesa-style fd compatibility, opt in to the fdio facade:

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKVIRTGPU_DRM: 'y'
    CONFIG_LIBUKVIRTGPU_DRM_FDIO: 'y'
```

Native VOGUE Vulkan/llama builds should not enable this library. Their path is
`libvulkan -> libukvulkan_venus -> libukvirtio_gpu` with no Linux DRM UAPI.

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the
include paths used by dependent libraries or applications (e.g. a future
Mesa/Linux compatibility appliance).

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
int uk_drm_virtgpu_get_caps(struct uk_drm_virtgpu_dev *dev,
                             uint32_t cap_set_id, uint32_t cap_set_ver,
                             void *buf, size_t size);
int uk_drm_virtgpu_resource_info(struct uk_drm_virtgpu_dev *dev,
                                  uint32_t bo_handle,
                                  struct drm_virtgpu_resource_info *info);
int uk_drm_virtgpu_map(struct uk_drm_virtgpu_dev *dev,
                        uint32_t bo_handle, uint64_t *offset_out);
int uk_drm_virtgpu_wait(struct uk_drm_virtgpu_dev *dev, uint32_t bo_handle);
int uk_drm_virtgpu_gem_close(struct uk_drm_virtgpu_dev *dev,
                              uint32_t bo_handle);
void uk_drm_virtgpu_close(struct uk_drm_virtgpu_dev *dev);
```

Supported core ioctls: `GETPARAM`, `GET_CAPS`, `CONTEXT_INIT`, `EXECBUFFER`,
`RESOURCE_CREATE_BLOB`, `RESOURCE_INFO`, `MAP`, `WAIT`, and `GEM_CLOSE`.
Unsupported syncobj and PRIME/dma-buf ioctls return `-ENOSYS`.

The fdio facade is declared in `include/uk/drm_virtgpu_fdio.h`:

```c
const char *uk_drm_virtgpu_render_node_path(void); /* /dev/dri/renderD128 */
int uk_drm_virtgpu_file_open(struct uk_drm_virtgpu_file *file, uint32_t gpu_idx);
int uk_drm_virtgpu_file_ioctl(struct uk_drm_virtgpu_file *file,
                              unsigned long request, void *arg);
int uk_drm_virtgpu_file_mmap(struct uk_drm_virtgpu_file *file,
                             uint64_t offset, size_t len, void **addr_out);
void uk_drm_virtgpu_file_close(struct uk_drm_virtgpu_file *file);
```

## Design boundaries

This library is a protocol-translation shim only. It does not:
- Reimplement VirtIO-GPU protocol or virtqueue submission (delegated to `libukvirtio_gpu`).
- Make native `libvulkan`/llama paths depend on Linux DRM.
- Claim Venus Vulkan rendering without the native Vulkan/Venus runtime gates
  also passing.
- Expose virgl or `SUBMIT_3D` paths for the K1 evidence row.
- Claim full Mesa/Linux DRM compatibility; syncobj and PRIME are future work.

The `uk_drm_virtgpu_map()` function returns the CPU virtual address of the
host-coherent blob mapping as the "offset" field for direct core tests. In
fd-compatible mode, `uk_drm_virtgpu_file_ioctl(..., DRM_IOCTL_VIRTGPU_MAP, ...)`
returns a page-aligned opaque offset; `uk_drm_virtgpu_file_mmap()` resolves that
offset through the same open file's mmap registry.

## Verification

```console
make native-vulkan-no-drm-check
make -C tests virtgpu-drm
make vk-drm-shim-check
make verify
```

The evidence matrix splits DRM compatibility into `vk.drm-core` for the direct
translator and `vk.drm-fdio` for the fd-compatible `ioctl`/`mmap` facade.
`make verify` re-runs the gates and regenerates the evaluation matrix.
