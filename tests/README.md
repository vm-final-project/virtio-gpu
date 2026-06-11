# `tests/` — VOGUE host-native test suite

Deterministic C tests for the reusable VirtIO-GPU / Venus / Vulkan substrate.
Every test compiles project library sources directly and links one
self-checking binary that exercises them against a modular **fake VirtIO-GPU backend**
under `fake/`. The suite needs **no QEMU, Unikraft, GPU, model, CUDA, or
EGL render node** — it is the fast inner loop and the primary CI gate.

llama.cpp runtime coverage lives in the upstream single-application appliances;
this suite only proves the support code.

## Package Layout & Correspondence

To make verification easy to cross-reference with the implementation, test files are organized in directories that correspond 1-to-1 with packages under `libs/`:

```text
tests/
  ├── Makefile                       # Target groups and compiler rules
  ├── README.md                      # This file
  ├── test_utils.h                   # Test assertions, device & context fixtures
  │
  ├── fake/                          # Modular VirtIO-GPU fake backend
  │   ├── virtio_gpu_fake.h          # Shared structures and internal helpers
  │   ├── fake_device.c              # Device lifecycle (probe, caps, EDID, etc.)
  │   ├── fake_resource.c            # Resource management, flushes, 2D/3D transfers
  │   ├── fake_context.c             # context creation, resource mapping, submit_3d
  │   └── fake_blob.c                # Blob creation, mapping, unmapping
  │
  ├── libukvirtio_gpu/               # Tests for libs/libukvirtio_gpu
  │   ├── core_test.c                # Core virtio-gpu frontend tests
  │   ├── proto_abi_test.c           # Wire ABI specification validation
  │   └── virgl_encoder_test.c       # Gallium virgl command encoder tests
  │
  ├── libukvulkan_venus/             # Tests for libs/libukvulkan_venus
  │   ├── venus_encoder_test.c       # Venus protocol encoder tests
  │   └── venus_ring_test.c          # Venus ring buffer transport tests
  │
  ├── libvulkan/                     # Tests for libs/libvulkan
  │   └── dispatch_test.c            # vkGetInstanceProcAddr static dispatch tests
  │
  └── libukvirtgpu_drm/              # Tests for libs/libukvirtgpu_drm
      └── drm_compat_test.c          # Linux virtgpu DRM compatibility facade tests
```

---

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
```

`make test-fast` (root) bundles the native suite and `proto-abi`.

The same targets exist on this component `Makefile` if you are working inside
`tests/` directly (`make -C tests native`, `make -C tests test-core`, …).
`make -C tests` with no target runs the full `native` suite.

## Test groups

| Group | Target | Binaries | Evidence rows |
|---|---|---|---|
| Core | `test-core` | `core_test`, `virgl_encoder_test` | `proto.real-driver`, `xport.qemu-vgpu` |
| Compat | `test-compat` | `drm_compat_test` | `vk.drm-core`, `vk.drm-fdio` |
| Venus | `test-venus` | `venus_encoder_test`, `venus_ring_test` | `proto.venus-enc`, `proto.venus-ring`, `vk.readiness` |
| ggml-vulkan dispatch | `test-dispatch` | `dispatch_test` | `vk.ggml-dispatch` |
| Conditional | `proto-abi` | `proto_abi_test` | `proto.real-driver` |

## Running a single test

Each binary has a one-shot target on the component `Makefile` (build + run):

```sh
make -C tests core-test             # core_test
make -C tests drm-compat            # drm_compat_test
make -C tests venus-encoder         # venus_encoder_test
make -C tests venus-ring            # venus_ring_test
make -C tests virgl-encoder         # virgl_encoder_test
make -C tests dispatch              # dispatch_test
```

Or build and run one binary by path:

```sh
make -C tests build/core_test && tests/build/core_test
```

## Vulkan headers (`VK_INC`)

The venus encoder/ring and dispatch tests compile the generated Venus tree, which
references `VK_HEADER_VERSION 352` types. `VK_INC` therefore defaults to the
repo-pinned `.deps/src/Vulkan-Headers/include` (fetched by `make deps`, the same
headers the image build uses), via `VULKAN_HEADERS_INCLUDE` when invoked from the
root `Makefile`. Override with `make -C tests <target> VK_INC=/path`.

## Conditional targets

* `proto-abi` runs only when `../libs/libukvirtio_gpu/virtio_gpu_proto.h` exists.

## Expected PASS output

```text
core_test: PASS checks=37
drm_compat_test: PASS checks=26
venus_encoder_test: PASS checks=25
venus_ring_test: PASS checks=23
virgl_encoder_test: all checks passed
dispatch_test: PASS checks=22
proto_abi_test passed ctrl_hdr=24 display_info=408 edid=1056
```

The full `native` run ends after the six retained binaries pass.

## How it works

* **Modular Fake backend** — Source files under `fake/` model the VirtIO-GPU control/DMA path in-process, so the same library C sources that ship in the unikernel are exercised deterministically on the host.
* **Two phases** — `native` compiles all binaries in parallel (one job per core, override with `make -C tests native JOBS=1`), then runs them **serially** for stable output ordering.
* **Library sources under test** — `drm_compat.c`/`gbm_compat.c` (Linux ABI shims), `drm_virtgpu.c` (`libukvirtgpu_drm`), `venus_driver.c` (`libukvulkan_venus`), `venus_*.c` (`libukvulkan_venus`), `virgl_encoder.c` (`libukvirtio_gpu`), and `uk_vulkan_dispatch.c` (`libvulkan`).
* **Host shims** — `shim/uk/*.h` (added to the include path via `-Ishim`) provide host-compilable stand-ins for the upstream Unikraft headers the guest sources use: `mutex.h` (no-op recursive lock), `sglist.h` (upstream `uksglist` scatter-gather under the host's identity mapping), and `alloc.h` (`uk_posix_memalign`/`uk_free` over libc). Device-backing memory in the guest uses these upstream APIs directly — there is no first-party DMA library.

Full evidence matrix: `results/vogue_evaluation_matrix.json`.
