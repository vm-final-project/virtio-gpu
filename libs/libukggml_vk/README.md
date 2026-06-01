# libukggml_vulkan — ggml-vulkan Static Dispatch for Unikraft

`libukggml_vk` is the source directory for the Unikraft library symbol `CONFIG_LIBUKGGML_VULKAN`.
`libukggml_vulkan` is the Unikraft-side Vulkan dispatch layer for upstream
`ggml-vulkan.cpp`. It replaces the Linux Vulkan loader/`dlopen` path with a
static `vkGetInstanceProcAddr` table and routes the pinned ggml-vulkan compute
API surface through `libukvenus` and VirtIO-GPU `SUBMIT_3D`.

## Current state

- **API coverage:** `scripts/llama_vulkan_api_coverage.py --check` compares
  `/home/jerrytsai/llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp` with
  `uk_vulkan_dispatch.c`. Current result: all required pinned ggml-vulkan APIs
  are present; optional debug/cooperative-matrix/timing APIs remain non-claiming.
- **Native substrate:** `vk.ggml-dispatch` passes. The native test exercises
  proc lookup, Vulkan-Hpp bootstrap, descriptor updates, copy/fill commands,
  pipeline setup, queue submit, fences, and cleanup through the fake VirtIO-GPU
  backend.
- **Runtime:** `llm.bench.vk`, `llm.bench.vk.real`, and `llm.server.vk` pass on
  the evaluation host with real QEMU/Venus artifacts. Current optimization work
  targets token-generation throughput (`tg128=3.4`) and server request handling,
  not basic runtime bring-up.

## Architecture

```text
app-llama-upstream-vk
  -> upstream ggml-vulkan.cpp
  -> Vulkan-Hpp DispatchLoaderDynamic
  -> uk_ggml_vk_loader.cpp
  -> uk_vulkan_dispatch.c
  -> libukvenus Venus command encoders
  -> libukvirtgpu_drm / libukvirtio_gpu
  -> QEMU virtio-gpu-gl-pci,blob=true,venus=true
  -> virglrenderer Venus backend -> host Vulkan driver
```

## Implemented required API families

- Instance/device/queue discovery and properties.
- Memory and buffer lifecycle: allocate/free/map/unmap/create/destroy/bind.
- Shader modules, descriptor set layouts/pools/sets, descriptor updates.
- Compute pipeline and pipeline layout creation/destruction.
- Command pools/buffers, bind/dispatch/push-constants/copy/fill/barrier.
- Queue submit, queue/device idle, fences, semaphores/events/query stubs used by
  ggml-vulkan optional paths.

## Verification

```sh
make llama-vulkan-api-coverage
make llama-ggml-vk-dispatch
make llama-vulkan-n3-run     # may report blocked:no-egl-render-node locally
```

## Claim boundaries

Allowed: pinned ggml-vulkan API coverage, static loader replacement, Venus
command encoding, native dispatch substrate, and image build evidence.

Not claimed without fresh runtime evidence: real token generation inside
Unikraft, GPU throughput, or general Vulkan conformance.

## Configuring applications to use

Enable the Unikraft library symbol:

```text
CONFIG_LIBUKGGML_VULKAN=y
```

`Makefile.uk` registers this as a Unikraft library with `addlib` and exports the include path for `<uk/ggml_vulkan.h>`. Applications should call `uk_ggml_vulkan_dispatch_init()` before `ggml_backend_vk_init()`. Legacy local GGUF/ggml/llama layers were removed; upstream llama.cpp owns model parsing, graph execution, and llama runtime behavior.

## Public API

```c
#include <uk/ggml_vulkan.h>
int uk_ggml_vulkan_dispatch_init(void);
PFN_uk_vkGetInstanceProcAddr uk_ggml_vulkan_get_proc_addr_fn(void);
```

## Design boundaries

This is a pinned ggml-vulkan support layer and **not** a general Vulkan driver, model loader, ggml fork, or llama runtime.
It does not claim runtime acceleration without `llm.bench.vk.run` or `llm.bench.vk` PASS evidence.
Run `make verify` before artifact or paper claims.

## llama.cpp environment matrix

Use `config/llama_env_matrix.json` plus `scripts/llama_env_matrix.py` for environment-selected llama.cpp runs. The matrix records CPU/Vulkan/CUDA/QEMU/Unikraft thread settings and claim boundaries so library documentation does not promote structured blockers as throughput evidence.
