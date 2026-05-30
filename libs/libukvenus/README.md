# libukvenus — Venus Encoder for VirtIO-GPU Vulkan Compute

`libukvenus` implements the guest-side Venus wire encoder used by VOGUE. It
serializes the Vulkan compute commands required by the pinned ggml-vulkan path
and submits them through VirtIO-GPU `SUBMIT_3D`.

## Current state

Implemented and tested command families include:

- Venus bootstrap: capset query, context creation, `vkCreateInstance`, physical
  device enumeration, device creation, queue retrieval.
- Compute resources: memory, buffers, shader modules, descriptor layouts/pools,
  descriptor-set allocation/update, pipeline layouts, compute pipelines.
- Command execution: command pools/buffers, bind pipeline, bind descriptor sets,
  push constants, dispatch, copy/fill buffer, barriers, queue submit, fences.
- Ring/blob substrate helpers used by QEMU/Venus probes.

## Architecture

```text
ggml-vulkan / Vulkan-Hpp
  -> libukggml_vulkan static vk* dispatch
  -> libukvenus encode VN_CMD_vk*
  -> libukvirtio_gpu SUBMIT_3D
  -> virglrenderer Venus backend
```

## Verification

```sh
make -C tests venus-cs
make llama-vulkan-api-coverage
make llama-ggml-vk-dispatch
```

## Claim boundaries

Allowed: wire-format encoding, fake-backend/native dispatch proof, and
transport-readiness evidence.

Not claimed by this library alone: Vulkan conformance, host GPU execution, or
llama.cpp throughput. Runtime claims require a same-run QEMU/Venus PASS marker.

## Configuring applications to use

Enable the Unikraft library symbol:

```text
CONFIG_LIBUKVENUS=y
```

`Makefile.uk` registers `libukvenus` with Unikraft `addlib` and publishes the
`<uk/venus.h>` public include path for applications and libraries such as
`libukggml_vulkan`.

## Public API

```c
#include <uk/venus.h>
int uk_venus_encoder_init(struct uk_venus_encoder *enc, void *buf, size_t cap);
int uk_venus_submit(struct uk_virtio_gpu_dev *dev,
                    const struct uk_virtio_gpu_context *ctx,
                    const struct uk_venus_encoder *enc);
```

## Design boundaries

This library encodes and transports Venus commands; it is **not** a standalone
Vulkan conformance implementation. Runtime acceleration claims require a QEMU
Venus PASS marker from the consuming application. Run `make verify` before
artifact or paper claims.
