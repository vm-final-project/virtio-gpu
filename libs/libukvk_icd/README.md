# libukvk_icd

`libukvk_icd` is the vk.icd gate Vulkan Installable Client Driver (ICD) shim
for Unikraft. It sits above vk.drm-shim (`libukvirtgpu_drm`) and provides the ICD
bootstrap layer that Vulkan benchmark ports (`app-vkmark`) require to
initialize a Venus/Vulkan context over VirtIO-GPU.

Current stage: `vk.smoke`, `gfx.vkmark`, and llama.cpp Vulkan runtime rows pass
their current substrate/runtime claims. Full vkmark scene FPS inside Unikraft is
still a future QEMU/Venus rendering gate.

Source lineage: Mesa `src/virtio/vulkan/vn_device.c` (Venus VkDevice init),
`src/virtio/vulkan/vn_renderer_virtgpu.c` (ICD renderer).

## Architecture

```
Vulkan app (app-vkmark)
  -> uk_vulkan_icd_init()       [vk.icd: this library]
    -> uk_drm_virtgpu_open()    [vk.drm-shim: libukvirtgpu_drm]
      -> uk_virtio_gpu_probe()  [libukvirtio_gpu]
        -> VirtIO-GPU controlq  [VirtIO transport]
```

## Configuring applications to use `libukvk_icd`

Enable `CONFIG_LIBUKVK_ICD` in the Unikraft config. The library
automatically selects `LIBUKVIRTGPU_DRM` (vk.drm-shim) and `LIBUKVIRTIO_GPU`:

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKVK_ICD: 'y'
```

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the
include paths used by dependent applications (e.g. `app-vkmark`).

## Public API

The public API is declared in `include/uk/vulkan_icd.h`:

```c
int  uk_vulkan_icd_init(struct uk_vulkan_icd_dev *icd, uint32_t gpu_idx);
int  uk_vulkan_icd_get_device_info(struct uk_vulkan_icd_dev *icd,
                                    struct uk_vulkan_icd_info *info_out);
void uk_vulkan_icd_close(struct uk_vulkan_icd_dev *icd);
```

`uk_vulkan_icd_init()` opens the vk.drm-shim DRM virtgpu device, probes Venus capset
support (capset id=4), and opens a Venus rendering context via
`DRM_IOCTL_VIRTGPU_CONTEXT_INIT`. On success the ICD substrate is PASS.

## Design boundaries

This library is a transport and bootstrap layer only. It does not:
- Implement the full Mesa Venus ring-buffer protocol.
- Provide a `VkInstance`/`VkDevice` handle in the Vulkan API sense.
- Expose a Vulkan loader manifest (no filesystem in Unikraft).
- Claim rendering or acceleration without a real Venus command stream.

**Allowed**: ICD initialization, Venus context creation via vk.drm-shim, capset detection (id=4), device info retrieval, host-coherent blob allocation path.

**Forbidden**: Vulkan rendering, fps scores, pipeline execution, GPU acceleration. All draw paths require ring-buffer setup and same-run frame proof tracked in `icd->info.rendering_status` and the K1 rows.

## Verification

```console
make -C tests native      # includes vk_icd_bootstrap_test
make native-tests         # full native gate
make vulkan-tests         # host Vulkan baseline
make eval-check           # regenerates gfx.vkmark row (now pass-substrate)
make verify               # full gate
```

The `gfx.vkmark` row in the evidence matrix (`results/vogue_latest_evaluation_matrix.*`)
is promoted to `pass` (substrate) when `vk_icd_bootstrap_test: all checks passed`
appears in the `make native-tests` output and `libukvk_icd/vulkan_icd.c`
exists.
