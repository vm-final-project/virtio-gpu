# tests — VOGUE host-native test suite

The host-native suite compiles against `virtio_gpu_fake.c`. It does not require
QEMU, Unikraft, a model file, CUDA, or an EGL render node. llama.cpp runtime and
throughput checks are intentionally split into `config/llama_env_matrix.json` and
the upstream single-app Kraftfiles; the native suite only proves the reusable
VirtIO-GPU/Venus/Vulkan substrate.

## Quick start

```sh
make native         # all deterministic native tests
make test-core      # VirtIO-GPU core / DMA / shims
make test-venus     # VirtIO-GPU 3D / Venus / virgl encoder
make test-dispatch  # static Vulkan/Venus dispatch used by upstream ggml-vulkan
make proto-abi      # VirtIO-GPU wire ABI struct/feature checks
make vulkan         # optional host Vulkan compute baseline (VK_LIB + VK_INC)
make -C .. llama-env-bench
make -C .. llama-env-server
```

## Test groups

| Group | Target | Binaries | Evidence rows |
|---|---|---|---|
| Core | `make test-core` | `dma_buf_test`, `virtio_gpu_full_api_test`, `virtio_gpu_2d_render_test`, `kmscube_compat_test` | proto.real-driver, xport.qemu-vgpu, gfx.kmscube.sw, gfx.glmark2.sw |
| 3D/Venus | `make test-venus` | `virtgpu_drm_ioctl_test`, `vk_icd_bootstrap_test`, `venus_cs_test`, `venus_compute_test`, `virgl_encoder_test` | vk.drm-shim, vk.icd, proto.venus-enc, proto.venus-ring, vk.readiness |
| ggml-vulkan dispatch | `make test-dispatch` | `ggml_vk_dispatch_test` | vk.ggml-dispatch |
| Conditional | `make vulkan`, `make proto-abi` | `vulkan_compute_test`, `virtio_gpu_proto_abi_test` | Host Vulkan baseline, proto.real-driver |

Removed duplicate tests: `ukmodel_test`, `ggml_uk_test`, `ukllama_test`,
`ggml_compute_bench`, and `llama_full_test`. Upstream llama.cpp now owns model
loading, graph execution, bench, and server behavior; VOGUE tests only the
Unikraft/VirtIO-GPU support code.

## Expected pass strings

```text
dma_buf_test passed alignment=4096 sg=1 pool=2
virtio_gpu_full_api_test passed capsets=5 fences=5 submits_3d=1 blobs=1 bytes_to_host=20480 bytes_from_host=4096
virtio_gpu_2d_render_test: PASS frames=3 transfers=3 flushes=3 fences=6
kmscube_compat_test passed mode=1280x800
virtgpu_drm_ioctl_test: all checks passed
vk_icd_bootstrap_test: all checks passed
venus_cs_test: all checks passed
venus_compute_test: all checks PASS
virgl_encoder_test: all checks passed
ggml_vk_dispatch_test: all checks PASS
virtio_gpu_proto_abi_test passed ctrl_hdr=24
```

Full evidence matrix: `results/vogue_evaluation_matrix.md`.
