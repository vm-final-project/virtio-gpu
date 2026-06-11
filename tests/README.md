# `tests/` — VOGUE host-native test suite

Deterministic C tests for the reusable VirtIO-GPU / Venus / Vulkan substrate.
Every test compiles project library sources directly and links one
self-checking binary that exercises them against a **fake VirtIO-GPU backend**
(`virtio_gpu_fake.c`). The suite needs **no QEMU, Unikraft, GPU, model, CUDA, or
EGL render node** — it is the fast inner loop and the primary CI gate.

llama.cpp runtime coverage lives in the upstream single-application appliances;
this suite only proves the support code.

## Necessity & duplication audit

The suite was audited for unused/duplicate tests. **None were found** — every
test binary compiles a *distinct* source set and uniquely guards one module, and
each is wired into a gate (`make test-fast`, or `make vulkan-check` for the host
baseline). A test is kept only if it (a) uniquely guards a project module no
other test covers, or (b) is load-bearing for a gate. By that rule all current
tests are necessary; removing any would lose coverage or break a gate:

- `virtio_gpu_core_test`, `virgl_encoder_core_test`, `venus_encoder_core_test`,
  `venus_ring_core_test`, `vulkan_dispatch_core_test`, `virtio_gpu_proto_abi_test`
  — each is the sole guard of a distinct VOGUE substrate module.
- `virtgpu_drm_compat_test` — the **only** guard of the optional `libukvirtgpu_drm`
  Linux-DRM shim; removing it would leave that library untested (`test-compat`).
- `vulkan_compute_test` — the host Vulkan baseline; `scripts/vulkan_compute_test_runner.py`
  consumes its output for `vulkan_perf.json`, so `make vulkan-check` / `make
  verify` depend on it. It is a baseline probe rather than a substrate unit test
  (it links the host `libvulkan` and is `BLOCKED` without `VK_LIB`), but it is
  load-bearing and therefore retained.

If a future change retires `libukvirtgpu_drm` or moves the host baseline out of
the test tree, drop the matching test together with its gate and evidence rows.

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

`make test-fast` (root) bundles the native suite and `proto-abi`.

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

## Vulkan headers (`VK_INC`)

The venus encoder/ring and dispatch tests compile the generated Venus tree, which
references `VK_HEADER_VERSION 352` types. `VK_INC` therefore defaults to the
repo-pinned `.deps/src/Vulkan-Headers/include` (fetched by `make deps`, the same
headers the image build uses), via `VULKAN_HEADERS_INCLUDE` when invoked from the
root `Makefile`. Override with `make -C tests <target> VK_INC=/path`. (The old
default pointed at a sibling `../../venus-protocol` checkout; when absent, the
build fell back to a stale system `vulkan.h` and failed on newer Vulkan types.)

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
