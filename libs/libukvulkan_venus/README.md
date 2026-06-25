# libukvulkan_venus — Unikraft-native Venus Vulkan driver

`libukvulkan_venus` is the Venus **Vulkan driver implementation** for VOGUE. It
is the driver layer that sits *behind* `libvulkan`: `libvulkan` owns the
application-facing `vk*` ABI and loader/runtime semantics, statically links this
driver, and dispatches the supported command subset into it. This library owns
Venus protocol encode/decode, Venus instance/device/queue/command behaviour,
query round-trips, and the Venus capset/context/ring/reply transport policy over
VirtIO-GPU `SUBMIT_3D`.

It is **not** only an encoder helper: while the wire encode/decode is one of its
jobs, it implements the Vulkan driver role over VirtIO-GPU. The driver-open
surface lives in `<uk/vulkan_venus.h>`; the low-level driver primitives remain in
`<uk/venus.h>`.

Source lineage: the Venus wire format is owned upstream by Mesa's
`venus-protocol` generator (pinned in `scripts/venus/pin.json`). VOGUE keeps the
generated driver-side protocol headers under `generated/`. The
in-image `uk_venus_encode_*` entry points in `protocol/venus_cs.c` and
`protocol/venus_compute.c` are
generated-protocol bridges: each builds the real `Vk*` struct from its scalar
arguments and calls the generated `vn_encode_vk*`, so the emitted wire format
**is** the Mesa Venus format. See `GENERATOR.md`. The transport stays
`libukvirtio_gpu`; this driver never imports Mesa's `vn_renderer_virtgpu.c`.

Status: Venus command encoding, ring protocol, QEMU transport, the llama.cpp
Vulkan bench, and the llama.cpp Vulkan HTTP server (`/health` 200, `/completion`
200) all pass on the x86_64 bring-up host over the Venus GPU path
(`results/llama/llama_vk.json`, `llama_server_vk.json`). Broader Vulkan benchmark
rendering (e.g. vkmark scene FPS) remains a separate gate.

## Architecture

```text
application / llama.cpp -> upstream ggml-vulkan / Vulkan-Hpp
  -> libvulkan                  (vk* ABI, loader/runtime, dispatch)
  -> libukvulkan_venus          (this library: Venus Vulkan driver)
       compat/venus_driver.c    driver open/probe surface (uk/vulkan_venus.h)
       ring/venus_ring.c        Venus bootstrap/context/ring setup
       protocol/venus_cs.c      generated bootstrap/transport bridge
       protocol/venus_compute.c generated compute-command bridge
       ring/vn_ring_shim.c      Venus ring transport
  -> libukvirtio_gpu            SUBMIT_3D / blobs / fences
  -> QEMU virtio-gpu-gl + virglrenderer Venus -> host Vulkan driver
```

## Configuring applications to use

Enable the Unikraft library symbol:

```text
CONFIG_LIBUKVULKAN_VENUS=y
```

`Makefile.uk` registers `libukvulkan_venus` with Unikraft `addlib` and publishes
the `<uk/vulkan_venus.h>` (driver-open) and `<uk/venus.h>` (driver primitives)
include paths. In normal builds `libvulkan` (`CONFIG_LIBVULKAN`) selects this
driver; applications select `LIBVULKAN`, not this driver directly.

The optional `CONFIG_LIBUKVULKAN_VENUS_USE_DRM_COMPAT` (default `n`) makes the
*driver itself* additionally pull the Linux virtgpu DRM fd compatibility shim
(`libukvirtgpu_drm` + `LIBUKVIRTGPU_DRM_FDIO`) for code that deliberately
emulates the Linux/Mesa DRM virtgpu UAPI. The default is the native
`libukvirtio_gpu` transport. Current llama.cpp/ggml-vulkan builds use the native
path and must not depend on the DRM shim.

## Public API

```c
#include <uk/vulkan_venus.h>
int  uk_vulkan_venus_open(struct uk_vulkan_venus_dev *dev, uint32_t gpu_idx);
void uk_vulkan_venus_close(struct uk_vulkan_venus_dev *dev);
const char *uk_vulkan_venus_probe_status(struct uk_vulkan_venus_dev *dev);

#include <uk/venus.h>   /* driver primitives */
int uk_venus_encoder_init(struct uk_venus_encoder *enc, void *buf, size_t cap);
int uk_venus_submit(struct uk_virtio_gpu_dev *dev,
                    const struct uk_virtio_gpu_context *ctx,
                    const struct uk_venus_encoder *enc);
```

## Design boundaries

This library implements the Venus Vulkan *driver*; it does **not** export the
public `vk*` ABI (that is `libvulkan`'s responsibility), and it does **not**
compile ggml/GGUF/llama runtime code. It is **not** a standalone Vulkan
conformance implementation. Runtime acceleration claims require a same-run QEMU
Venus PASS marker from the consuming application. Every intentional deviation
from Mesa Venus behaviour must be documented here and covered by a
claim-boundary test.

## Verification

```sh
make -C tests venus-encoder-core
make -C tests venus-capset-core
make vulkan-check
make verify
```

## Claim boundaries

Allowed: Venus encode/decode and transport-readiness evidence. Not claimed by
this driver alone: Vulkan conformance, host GPU execution, or llama.cpp
throughput. Runtime claims require a same-run QEMU/Venus PASS marker.
