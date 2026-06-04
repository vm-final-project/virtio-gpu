# libvulkan — application-facing Vulkan ABI/runtime (compute-first subset)

`libvulkan` is VOGUE's application-facing Vulkan ABI/runtime boundary. It owns
the exported `vk*` symbols, `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`, the
Vulkan-Hpp `DispatchLoaderDynamic` storage/init glue, and the common Vulkan
runtime state required by the supported subset. It dispatches the public Vulkan
entry points into a single statically linked Vulkan driver implementation —
currently the Unikraft-native Venus driver in `libukvulkan_venus`.

This replaces the previous design where the `vk*` ABI and dispatch state lived
inside the ggml-named `libukggml_vulkan` library. The Vulkan ABI now lives in a
correctly named loader/runtime library; the upstream ggml-vulkan stack is built
in-tree by `app-llama-upstream-vk` (the `libukggml_vk` helper was retired).

## Architecture

```text
application / llama.cpp -> upstream ggml-vulkan.cpp / Vulkan-Hpp
  -> libvulkan                  (this library: vk* ABI, dispatch, Hpp loader)
       uk_vulkan_dispatch.c     exported vk* + static dispatch table (k_procs[])
       vk_hpp_loader.cpp        VULKAN_HPP_DEFAULT_DISPATCHER storage/init
       uk_stdcxx_compat.cpp     libstdc++ ABI compat shims
  -> libukvulkan_venus          Venus Vulkan driver implementation
  -> libukvirtio_gpu            VirtIO-GPU SUBMIT_3D transport
  -> QEMU virtio-gpu-gl + virglrenderer Venus -> host Vulkan driver
```

## Configuring applications to use

Enable the Unikraft library symbol:

```text
CONFIG_LIBVULKAN=y
```

`Makefile.uk` registers `libvulkan` with Unikraft `addlib` and publishes the
`<uk/vulkan.h>` control/diagnostic include path. `CONFIG_LIBVULKAN` selects
`LIBUKVULKAN_VENUS` (the driver) for the current build; the dispatch opens the
device through that driver's native `libukvirtio_gpu` bootstrap (no DRM UAPI, no
ICD shim). Upstream Vulkan clients include the
normal Vulkan headers and call `vk*`; they only need `<uk/vulkan.h>` for
explicit boot-time `uk_vulkan_init()` / diagnostics.

## Public API

```c
#include <uk/vulkan.h>
int  uk_vulkan_init(void);
void uk_vulkan_shutdown(void);
void uk_vulkan_get_info(struct uk_vulkan_info *out);   /* driver_name="venus", ... */

/* Vulkan-Hpp glue */
PFN_uk_vkGetInstanceProcAddr uk_vulkan_get_instance_proc_addr_fn(void);
void uk_vulkan_dispatcher_init_with_proc_addr(void);

/* plus the exported Vulkan C ABI: vkCreateInstance, vkCreateDevice, ... */
```

## Design boundaries

`libvulkan` owns the app-facing `vk*` ABI and loader/runtime semantics only. It
does **not** own the Venus wire format, VirtIO-GPU probing/blobs/contexts, Linux
DRM ioctl compatibility, or any ggml/GGUF/llama runtime logic. The advertised
surface is a **compute-first Vulkan API subset** (the slice required by upstream
`ggml-vulkan.cpp`); this is **not** full Vulkan and **not** Vulkan conformance.
The library name must always be paired with "compute-first Vulkan API subset"
until broader coverage and conformance gates exist.

## Verification

```sh
make -C tests test-dispatch
make llama-vulkan-api-coverage
make llama-ggml-vk-dispatch
make lib-readme-check
make verify
```
