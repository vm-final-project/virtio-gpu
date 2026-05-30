# Multi-environment llama.cpp benchmark

| Env | Environment | pp512 | tg128 | Status |
|---|---|---:|---:|---|
| ENV0 | Baremetal CPU (1 thread) | — | — | skipped |
| ENV1 | Baremetal CPU (32 threads) | — | — | skipped |
| ENV2 | Baremetal Vulkan GPU | — | — | skipped |
| ENV3 | Baremetal Vulkan (llvmpipe) | — | — | skipped |
| ENV4 | Baremetal CUDA | — | — | pass |
| ENV5 | QEMU-VM Linux CPU (1 vCPU) | 20,228 | 691 | pass |
| ENV6 | QEMU-VM Linux CPU (4 vCPU) | 40,642 | 1,381 | pass |
| ENV7 | QEMU-VM Linux Vulkan (llvmpipe) | 55,298 | 1,460 | pass |
| ENV8 | QEMU-VM VirtIO-GPU Vulkan (Venus, Linux) | 120,954 | 2,039 | pass |
| ENV9 | QEMU + Unikraft VirtIO-GPU Venus (llvmpipe, CPU Vulkan) | — | — | blocked:wrong-domain-or-stale |
| ENV10 | QEMU + Unikraft VirtIO-GPU Venus (full ggml-vulkan, no patches, KVM) | — | — | blocked:n3-image-not-built |
| ENV11 | vk.ggml-dispatch Static Vulkan ICD dispatch (host) | — | — | pass |
