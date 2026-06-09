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
make test-core          # group 1: VirtIO-GPU core helper + fake-backend path
make test-compat        # group 2: virtgpu DRM compatibility facade
make test-venus         # group 3: Venus protocol helpers
make test-dispatch      # group 4: libvulkan static dispatch
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
| Core | `test-core` | `virtio_gpu_core_test`, `virgl_encoder_core_test` | `proto.real-driver`, `xport.qemu-vgpu` |
| Compat | `test-compat` | `virtgpu_drm_compat_test` | `vk.drm-core`, `vk.drm-fdio` |
| Venus | `test-venus` | `venus_encoder_core_test`, `venus_ring_core_test` | `proto.venus-enc`, `proto.venus-ring`, `vk.readiness` |
| ggml-vulkan dispatch | `test-dispatch` | `vulkan_dispatch_core_test` | `vk.ggml-dispatch` |
| Conditional | `vulkan`, `proto-abi` | `vulkan_compute_test`, `virtio_gpu_proto_abi_test` | host Vulkan baseline, `proto.real-driver` |

## Running a single test

Each binary has a one-shot target on the component `Makefile` (build + run):

```sh
make -C tests virtio-gpu-core # virtio_gpu_core_test
make -C tests virtgpu-drm-compat   # virtgpu_drm_compat_test
make -C tests venus-encoder-core   # venus_encoder_core_test
make -C tests venus-ring-core      # venus_ring_core_test
make -C tests virgl-encoder-core   # virgl_encoder_core_test
make -C tests vulkan-dispatch-core # vulkan_dispatch_core_test
```

Or build and run one binary by path:

```sh
make -C tests build/virtio_gpu_core_test && tests/build/virtio_gpu_core_test
```

## Conditional targets

* `proto-abi` runs only when `../libs/libukvirtio_gpu/virtio_gpu_proto.h` exists.
* `vulkan` runs the host Vulkan compute baseline only when `VK_LIB` and `VK_INC`
  resolve (llvmpipe/lavapipe is fine); otherwise it prints a `BLOCKED` line and
  exits 0. Override the paths via `make -C tests vulkan VK_LIB=… VK_INC=…`.

## Expected PASS output

```text
virtio_gpu_core_test: PASS checks=37
virtgpu_drm_compat_test: PASS checks=26
venus_encoder_core_test: PASS checks=25
venus_ring_core_test: PASS checks=23
virgl_encoder_core_test: all checks passed
vulkan_dispatch_core_test: PASS checks=22
virtio_gpu_proto_abi_test passed ctrl_hdr=24 display_info=408 edid=1056
```

The full `native` run ends after the six retained binaries pass.

## How it works

* **Fake backend** — `virtio_gpu_fake.c` models the VirtIO-GPU control/DMA path
  in-process, so the same library C sources that ship in the unikernel are
  exercised deterministically on the host.
* **Two phases** — `native` compiles all binaries in parallel (one job per core,
  override with `make -C tests native JOBS=1`), then runs them **serially** for
  stable output ordering.
* **Library sources under test** — `drm_compat.c`/`gbm_compat.c` (Linux ABI shims), `drm_virtgpu.c`
  (`libukvirtgpu_drm`), `venus_driver.c` (`libukvulkan_venus`), `venus_*.c`
  (`libukvulkan_venus`), `virgl_encoder.c` (`libukvirtio_gpu`), and
  `uk_vulkan_dispatch.c` (`libvulkan`).
* **Host shims** — `shim/uk/*.h` (added to the include path via `-Ishim`) provide
  host-compilable stand-ins for the upstream Unikraft headers the guest sources
  use: `mutex.h` (no-op recursive lock), `sglist.h` (upstream `uksglist`
  scatter-gather under the host's identity mapping), and `alloc.h`
  (`uk_posix_memalign`/`uk_free` over libc). Device-backing memory in the guest
  uses these upstream APIs directly — there is no first-party DMA library.

Full evidence matrix: `results/vogue_evaluation_matrix.json`.
