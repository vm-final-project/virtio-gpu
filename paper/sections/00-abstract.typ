= Abstract

Unikernels promise small, fast virtual machines, but graphics and accelerator
workloads usually depend on Linux DRM/KMS, Mesa, and large driver stacks. VOGUE
asks how far a Unikraft guest can go with a bounded VirtIO-GPU frontend instead
of importing those stacks. It contributes a compact VirtIO-GPU library, DRM/GBM/EGL
compatibility shims for application ports, a Venus command/ring substrate, and a
static Vulkan dispatch layer for llama.cpp/ggml's `ggml-vulkan` backend.

The artifact is evidence-gated. On a host with the Venus stack — a Venus-capable
QEMU (`virtio-gpu-gl-pci,venus=true`), a Venus-enabled virglrenderer, and an
accessible GPU render node — its generated 27-row matrix is 27/27 PASS;
without that stack the GPU/transport rows fall back to structured `blocked:*`
status rather than faking a result. Host-native tests validate protocol ABI, 2D
paths, Venus encoding/ring behavior, Vulkan ICD bootstrap, and llama/ggml
substrates. Application rows cover bounded kmscube, glmark2, vkmark, and
llama.cpp case studies. For `ggml-vulkan`, VOGUE covers the required upstream
Vulkan API surface, passes a 164-check static-dispatch regression, and — once
the historical modern-PCI transport blocker is cleared — runs the upstream
llama.cpp Vulkan bench appliance end-to-end on a real GPU over `virtio-gpu-gl`
Venus (`pp512≈245 t/s`, `tg128≈3.1 t/s` on a Tesla V100), with the Vulkan server
appliance loading all model layers onto the GPU and reaching model-loaded
readiness. We report implementation coverage and substrate correctness
separately from GPU-throughput claims, and every throughput number is backed by
a same-run guest artifact.
