# `tests/` — VOGUE host-native test suite

Deterministic C tests for the reusable VirtIO-GPU / Venus / Vulkan substrate.
Every test compiles project library sources directly and links one
self-checking binary that exercises them against a **fake VirtIO-GPU backend**
(`virtio_gpu_fake.c`). The suite needs **no QEMU, Unikraft, GPU, model, CUDA, or
EGL render node** — it is the fast inner loop and the primary CI gate.

llama.cpp runtime/throughput coverage deliberately lives elsewhere (the upstream
single-application appliances and `config/llama_env_matrix.json`); this suite
only proves the support code.

## Quick start

Run everything from the **repository root** — the root `Makefile` wraps each
group so you never need to `cd tests/`:

```sh
make native-tests       # full deterministic suite (primary CI gate)
make test-core          # group 1: VirtIO-GPU core / DMA / DRM+GBM shims
make test-venus         # group 2: virtgpu ioctl / VK ICD / Venus / virgl
make test-dispatch      # group 3: ggml-vulkan static dispatch
make proto-abi          # VirtIO-GPU wire-ABI struct/feature check
make vulkan-tests       # optional host Vulkan compute baseline (VK_LIB + VK_INC)
```

`make test-fast` (root) bundles the native suite + `proto-abi` with the
governance/doc gates for the daily developer check.

The same targets exist on this component `Makefile` if you are working inside
`tests/` directly (`make -C tests native`, `make -C tests test-core`, …).
`make -C tests` with no target runs the full `native` suite.

## Test groups

| Group | Target | Binaries | Evidence rows |
|---|---|---|---|
| Core | `test-core` | `dma_buf_test`, `virtio_gpu_full_api_test`, `virtio_gpu_2d_render_test`, `kmscube_compat_test` | `proto.real-driver`, `xport.qemu-vgpu`, `gfx.kmscube.sw`, `gfx.glmark2.sw` |
| 3D / Venus | `test-venus` | `virtgpu_drm_ioctl_test`, `vk_icd_bootstrap_test`, `venus_cs_test`, `venus_compute_test`, `virgl_encoder_test` | `vk.drm-shim`, `vk.icd`, `proto.venus-enc`, `proto.venus-ring`, `vk.readiness` |
| ggml-vulkan dispatch | `test-dispatch` | `ggml_vk_dispatch_test` | `vk.ggml-dispatch` |
| Conditional | `vulkan`, `proto-abi` | `vulkan_compute_test`, `virtio_gpu_proto_abi_test` | host Vulkan baseline, `proto.real-driver` |

## Running a single test

Each binary has a one-shot target on the component `Makefile` (build + run):

```sh
make -C tests venus-cs        # venus_cs_test
make -C tests venus-compute   # venus_compute_test
make -C tests virgl-enc       # virgl_encoder_test
make -C tests g5              # virtgpu_drm_ioctl_test  (== root: make vk-drm-shim-check)
make -C tests test-n3         # ggml_vk_dispatch_test   (legacy alias of test-dispatch)
```

Or build and run one binary by path:

```sh
make -C tests build/dma_buf_test && tests/build/dma_buf_test
```

## Conditional targets

* `proto-abi` runs only when `../libs/libukvirtio_gpu/virtio_gpu_proto.h` exists.
* `vulkan` runs the host Vulkan compute baseline only when `VK_LIB` and `VK_INC`
  resolve (llvmpipe/lavapipe is fine); otherwise it prints a `BLOCKED` line and
  exits 0. Override the paths via `make -C tests vulkan VK_LIB=… VK_INC=…`.

## Expected PASS output

```text
dma_buf_test passed alignment=4096 sg=1 len=4096
virtio_gpu_full_api_test passed capsets=5 fences=7 submits_3d=1 blobs=1 bytes_to_host=36864 bytes_from_host=4096
virtio_gpu_2d_render_test: PASS frames=3 transfers=3 flushes=3 fences=6
kmscube_compat_test passed mode=1280x800 c0=0xcaac5505 c1=0xcbd70305
virtgpu_drm_ioctl_test: all checks passed
vk_icd_bootstrap_test: all checks passed
venus_cs_test: all checks passed
venus_compute_test: all checks PASS  (Venus compute dispatch encoded correctly)
uk-venus-compute: PASS evidence_id=venus-compute-dispatch
virgl_encoder_test: all checks passed
ggml_vk_dispatch_test: all checks PASS
uk-llama-vk-n3: PASS evidence_id=llama-vk-n3-dispatch gpu=1 venus=1 substrate=static-vk-icd
virtio_gpu_proto_abi_test passed ctrl_hdr=24 display_info=408 edid=1056
```

The full `native` run ends with `Results: 164 passed, 0 failed`.

## How it works

* **Fake backend** — `virtio_gpu_fake.c` models the VirtIO-GPU control/DMA path
  in-process, so the same library C sources that ship in the unikernel are
  exercised deterministically on the host.
* **Two phases** — `native` compiles all binaries in parallel (one job per core,
  override with `make -C tests native JOBS=1`), then runs them **serially** for
  stable output ordering.
* **Library sources under test** — `drm_compat.c`/`gbm_compat.c` (Linux ABI shims), `drm_virtgpu.c`
  (`libukvirtgpu_drm`), `vulkan_icd.c` (`libukvk_icd`), `venus_*.c`
  (`libukvenus`), `virgl_encoder.c` (`libukvirtio_gpu`), and
  `uk_vulkan_dispatch.c` (`libukggml_vk`).
* **Host shims** — `shim/uk/*.h` (added to the include path via `-Ishim`) provide
  host-compilable stand-ins for the upstream Unikraft headers the guest sources
  use: `mutex.h` (no-op recursive lock), `sglist.h` (upstream `uksglist`
  scatter-gather under the host's identity mapping), and `alloc.h`
  (`uk_posix_memalign`/`uk_free` over libc). Device-backing memory in the guest
  uses these upstream APIs directly — there is no first-party DMA library.

Full evidence matrix: `results/vogue_evaluation_matrix.md`.
