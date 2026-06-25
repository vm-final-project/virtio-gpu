# Architecture

## Runtime Stack

```text
application
  -> libvulkan
  -> libukvulkan_venus
  -> libukvirtio_gpu
  -> QEMU virtio-gpu-gl
  -> virglrenderer / host Vulkan driver
```

The guest contains only the bounded compatibility and protocol code required by
the appliances. Linux DRM, Mesa, llama.cpp, and Unikraft remain external inputs.

## Automation Boundary

The root `Makefile` owns dependency ordering and is the public interface.
Scripts are private implementation details:

- `common.py`: shared QEMU, timeout, model, and JSON helpers.
- `app-vulkan-sample.py`: QEMU VirtIO-GPU probes (runs app-vulkan-sample).
- `app-llama-cpu.py`: CPU bench and server capture.
- `app-llama-vk.py`: Vulkan bench and server capture.

There is no cross-result database or generated summary. Each target updates one
canonical JSON result, and its exit status reports execution success.

## Dependency Graph

```text
test-fast  -> native-tests + proto-abi
venus-check -> test-venus + two QEMU probes
vulkan-check -> venus-check
llama-*-run -> matching build -> one runtime capture
verify -> all of the above
```

## SMP Acceptance Boundary

The SMP acceptance claim for this branch is llama.cpp/ggml worker placement, not
general POSIX affinity. The acceptance evidence is
`llama-cpu-bench-run VOGUE_SMP=N` and `llama-cpu-server-run VOGUE_SMP=N` with
JSON metrics that record `placement_expected`, `placement_actuals_seen`, and
`placement_complete`. `pthread-affinity-run` remains a diagnostic target and is
not the definition of success for this stage.

## Mesa Alignment Boundary

| Layer | Mesa analogue | VOGUE adaptation |
| --- | --- | --- |
| VirtIO-GPU transport | `src/virtio/vulkan/vn_renderer_virtgpu.c` DRM ioctl helpers | Native `libukvirtio_gpu` calls, no DRM fd enumeration |
| Renderer info/capset | `vn_renderer_info`, `virgl_renderer_capset_venus` | `uk_venus_renderer_info` decoded from the Venus capset |
| Ring | `src/virtio/vulkan/vn_ring.c` | `uk_venus_ring` with Mesa offsets/order and Unikraft blob mapping |
| Protocol | Generated `venus-protocol` headers | Vendored/generated `libs/libukvulkan_venus/generated` wrappers |
| Vulkan ABI | Mesa Vulkan driver entrypoints | Minimal static ABI needed by ggml-vulkan, no full loader/ICD |
