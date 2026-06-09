# Research-to-implementation traceability

VOGUE VirtIO-GPU Venus/Vulkan v1 — Gate implementation reference.

## Gate summary

| Gate | Description | Status |
|------|-------------|--------|
| Gate proto.api-contract | VirtIO-GPU 2D/control path | PASS |
| Gate G1 | DMA buffer support | PASS |
| Gate G2 | virgl encoder | PASS |
| Gate G3 | kmscube software render | PASS |
| Gate G4 | upstream ggml-vulkan dispatch (vk.ggml-dispatch) | PASS |
| Gate vk.drm-shim | DRM virtgpu ioctl shim | PASS |
| Gate vk.icd | Vulkan ICD bootstrap | PASS |
| Gate G7 | Venus SUBMIT_3D encoder | PASS |
| Gate G8 | vk.ggml-dispatch ggml-vulkan in Unikraft | PASS |

## performance evaluation

Reference: `results/vogue_evaluation_matrix.json`

Key metrics:
- submit latency p50/p95/p99 measured via Venus probe scripts
- vk.ggml-dispatch ggml-vulkan dispatch: 154/154 host checks pass

## Research lineage

Derived from:
- Mesa Venus protocol (VIRTIO_GPU_CAPSET_VENUS = 4)
- Linux virtio_gpu.h uapi headers
- QEMU virtio-gpu-gl implementation

## Out of scope (2026-05-22)

STK / `stk-code` / SuperTuxKart porting is **out of scope** for this project.
See `plan.md §0.5` for the full out-of-scope list. The
`the documentation validation workflow` token list for this file is scheduled
for cleanup under `plan.md §W0.2`.
