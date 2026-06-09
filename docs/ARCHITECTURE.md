# VOGUE architecture and artifact taxonomy

The evidence matrix (`results/vogue_evaluation_matrix.json`) is the
source of truth for PASS versus `blocked:*` status. The codebase is intentionally
small: local Unikraft libraries provide device/compatibility glue; upstream
application projects keep their own application semantics.

## Runtime stack

```text
application port (kmscube/vkmark/llama.cpp)
  -> bounded Unikraft compatibility shim
  -> libukvirtio_gpu real frontend
  -> VirtIO-GPU controlq/cursorq + resources/fences/capsets/blobs
  -> QEMU virglrenderer / Venus
  -> host GL/Vulkan driver
```

The project does not import Linux DRM/KMS or Mesa into the guest. It implements
only the bounded interfaces needed by the artifact rows.

## Dependency graph

`make depgraph` (`scripts/gen_depgraph.py`, plan in
`docs/plans/dependency-graph-plan.md`) extracts the virtio-gpu dependency multigraph
across three codebases and one protocol seam — the Linux guest driver
(`../linux/drivers/gpu/drm/virtio`), our VOGUE `libs/*`, and the QEMU device
model (`../qemu-src/hw/display`) — directly from their `Kconfig`/`Config.uk`,
`Makefile`/`meson.build`, and `#include` graphs. Edges are typed `build`
(dashed), `compile` (blue), and `runtime` (bold). Artifacts and full catalogue
live in [`results/depgraph/`](../results/depgraph/README.md):

| View | Insight | Files |
|---|---|---|
| A — The Collapse | Linux links 13 DRM/KMS/GEM objects + subsystem `select`s; VOGUE reaches the same seam through a thin `libuk*` chain, depending on **none** of that tower. | `results/depgraph/view-a-collapse.{svg,mmd}` |
| B — Three Kinds of One Edge | Where build wiring diverges from the runtime hot path on the VOGUE Vulkan chain. | `results/depgraph/view-b-edge-kinds.{svg,mmd}` |
| C — The Invariant Spine | Linux **and** VOGUE emit the same `VIRTIO_GPU_CMD_*`/Venus opcodes that QEMU consumes — interchangeable guests, one host contract. | `results/depgraph/view-c-spine.{svg,mmd}` |

View C — the shared protocol contract that both guests speak to QEMU:

```mermaid
flowchart LR
  subgraph linux["Linux DRM guest"]
    linux_virtgpu_vq["virtgpu_vq"]
    linux_virtgpu_submit["virtgpu_submit"]
  end
  subgraph vogue["VOGUE guest"]
    vogue_libvulkan["libvulkan"]
    vogue_libukvulkan_venus["libukvulkan_venus"]
    vogue_libukvirtgpu_drm["libukvirtgpu_drm"]
    vogue_libukvirtio_gpu["libukvirtio_gpu"]
  end
  subgraph seam["Protocol seam"]
    seam_ctrlq["VirtIO-GPU control/cursor queues"]
    seam_venus_ring["Venus command ring"]
  end
  subgraph qemu["QEMU host"]
    qemu_virtio_gpu["virtio-gpu"]
    qemu_virtio_gpu_virgl["virtio-gpu-virgl"]
    qemu_ext_virglrenderer["virglrenderer"]
  end
  vogue_libvulkan ==>|vk cmd encode| vogue_libukvulkan_venus
  vogue_libukvulkan_venus ==>|VENUS ring submit| vogue_libukvirtio_gpu
  vogue_libukvirtgpu_drm ==>|EXECBUFFER ioctl| vogue_libukvirtio_gpu
  vogue_libukvirtio_gpu ==>|VIRTIO_GPU_CMD_*| seam_ctrlq
  vogue_libukvulkan_venus ==>|VK command stream| seam_venus_ring
  linux_virtgpu_vq ==>|VIRTIO_GPU_CMD_*| seam_ctrlq
  linux_virtgpu_submit ==>|EXECBUFFER| seam_venus_ring
  seam_ctrlq ==>|cmd dispatch| qemu_virtio_gpu
  seam_venus_ring ==>|venus decode| qemu_virtio_gpu_virgl
  qemu_virtio_gpu_virgl ==>|host Vulkan| qemu_ext_virglrenderer
```

Regenerate with `make depgraph`; `make depgraph-check` is the drift gate.

## llama.cpp taxonomy

| Class | Surface | Purpose | Evidence status |
|---|---|---|---|
| CPU single-app | `apps/app-llama-cpu`, `kraft/Kraftfile.llama-cpu-bench`, `kraft/Kraftfile.llama-cpu-server` | Unmodified upstream llama.cpp bench-only or server-only Unikraft image. | `llm.bench.cpu` pass or structured blocker. |
| Vulkan single-app | `apps/app-llama-vk`, `kraft/Kraftfile.llama-vk` | Unmodified upstream llama.cpp Vulkan path through `libvulkan` (vk* ABI) → `libukvulkan_venus` (Venus driver). | `llm.bench.vk` / `llm.bench.vk.real` pass or structured blocker. |
| Vulkan ABI / driver | `libs/libvulkan`, `libs/libukvulkan_venus` | `libvulkan` owns the app-facing `vk*` ABI + Vulkan-Hpp dispatch (compute-first subset); `libukvulkan_venus` is the statically linked Venus driver. | `vk.ggml-dispatch` PASS. |
| ggml-vulkan build glue | `apps/app-llama-vk/Makefile.uk` | Builds upstream `ggml-vulkan.cpp` + ggml core + SPIR-V shader blobs in-tree (the former `libukggml_vk` helper was retired); resolves the `vk*` ABI against `libvulkan`. | `vk.ggml-dispatch` PASS. |
| Environment matrix | `config/llama_env_matrix.json` | Baremetal/QEMU/Unikraft CPU/Vulkan/CUDA runs with explicit threads and args. | Dry-run/execute artifacts under `results/llama-env/`. |

Synthetic local llama libraries and apps were removed: no local GGUF framework,
custom ggml substrate, or llama runtime remains under `libs/` or `apps/`.

## Evidence ladder for ggml-vulkan

1. `make llama-vulkan-api-coverage`: upstream `ggml-vulkan.cpp` required Vulkan calls are present in the static dispatch table.
2. `make llama-ggml-vk-dispatch`: host fake-backend dispatch test; no QEMU or GPU throughput claim.
3. `make llama-vk-build`: Unikraft image links upstream ggml-vulkan/Venus libraries.
4. `make llama-vk-run`: QEMU runtime; may return `blocked:no-egl-render-node` or another structured blocker.
5. Only a same-run runtime PASS may support guest GPU inference claims.

## Reviewer targets

- `make artifact-smoke`: quick no-QEMU structure/native/API/docs gate.
- `make artifact-functional`: smoke + host Vulkan/app/eval/current-stage gates.
- `make artifact-full`: functional + host-dependent QEMU/Venus/llama Vulkan gates.
