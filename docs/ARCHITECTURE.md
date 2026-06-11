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
vulkan-check -> test-dispatch
llama-*-run -> matching build -> one runtime capture
verify -> all of the above
```
