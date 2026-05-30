= Abstract

Unikernels promise small, fast virtual machines, but graphics and accelerator
workloads usually depend on Linux DRM/KMS, Mesa, and large driver stacks. VOGUE
asks how far a Unikraft guest can go with a bounded VirtIO-GPU frontend instead
of importing those stacks. It contributes a compact VirtIO-GPU library, DRM/GBM/EGL
compatibility shims for application ports, a Venus command/ring substrate, and a
static Vulkan dispatch layer for llama.cpp/ggml's `ggml-vulkan` backend.

The artifact is evidence-gated. Its current generated matrix has 27 rows: 18 PASS and 9
structured `blocked:*` rows. Host-native tests validate protocol ABI, 2D paths,
Venus encoding/ring behavior, Vulkan ICD bootstrap, and llama/ggml substrates.
Application rows cover bounded kmscube, glmark2, vkmark, and llama.cpp
case studies. For `ggml-vulkan`, VOGUE covers the required upstream Vulkan API
surface and passes a 164-check static-dispatch regression, but end-to-end guest
GPU inference remains blocked in this environment by QEMU/Venus EGL render-node
availability (`blocked:no-egl-render-node`). We therefore report implementation
coverage and substrate correctness separately from GPU-throughput claims.
