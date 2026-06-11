# `tests/` conciseness pass — design

Date: 2026-06-11
Branch: dev-jerry-vk

## Goal

Make the `tests/` codebase more concise and smaller, removing shim/helper code
that is not necessary, **without** changing what the suite proves or breaking any
test. Every retained binary must keep producing the same PASS output.

## Baseline (captured, all passing)

```
core_test: PASS checks=37
drm_compat_test: PASS checks=26
venus_encoder_test: PASS checks=25
venus_ring_test: PASS checks=23
virgl_encoder_test: PASS checks=38
dispatch_test: PASS checks=22
proto_abi_test passed ctrl_hdr=24 display_info=408 edid=1056
```

## Findings

- **Shim is fully necessary.** `shim/uk/{mutex,sglist,alloc}.h` are all reachable
  from compiled sources (`alloc.h` ← `core_test.c`; `mutex.h` ← `uk_vulkan_dispatch.c`;
  `sglist.h` ← public `uk/virtio_gpu.h`). Renaming `alloc.h` away breaks the build.
  **No shim changes.**
- **`test_utils.h`** is shared by all functional tests; `proto_abi_test.c` is
  standalone by design. No duplication to remove.
- **Dead `fake/` functions** — determined authoritatively by building each binary
  with `-ffunction-sections -Wl,--gc-sections -Wl,--print-gc-sections` and
  intersecting the discarded `.text.*` sections across all six binaries:
  - `uk_virtio_gpu_gl_resource_create_3d` (fake_resource.c)
  - `uk_virtio_gpu_gl_transfer_to_host_3d` (fake_resource.c)
  - `uk_virtio_gpu_gl_transfer_from_host_3d` (fake_resource.c)
  - `uk_virtio_gpu_get_display_info` (fake_device.c)
  - `validate_xfer` (fake_device.c — private helper, only the dead 3D transfers call it)
- **Makefile** carries 6 backwards-compat alias targets and 6 repetitive per-binary
  build/run rule pairs.

## Changes

### 1. Remove truly-dead `fake/` code
Delete the 5 functions above. Then re-run the GC-sections check to catch any
cascade (helpers like `checked_mul_u64`/`rect_bytes`/`resource_size` or struct
fields that become unreferenced) and remove only what is newly dead-everywhere.
Keep all WEAK API-surface stubs and every 2D / blob / context / apir path — those
are live in at least one binary.

### 2. Aggressively prune the Makefile
- Remove the 6 backwards-compat alias targets:
  `virtio-gpu-core`, `virtgpu-drm-compat`, `venus-encoder-core`, `venus-ring-core`,
  `virgl-encoder-core`, `vulkan-dispatch-core`.
- Collapse the repetitive per-binary build rules and single-binary run targets with
  a `define`/`$(eval)` template. Preserve `FAKE_SRCS`, both `GEN_CFLAGS` include
  variants, the group targets (`native`, `test-core`, `test-compat`, `test-venus`,
  `test-dispatch`), and the `proto-abi` conditional block unchanged in behavior.

### 3. Sync docs
Update `tests/README.md` (and root `README.md` if it references removed targets)
to drop deleted alias targets / removed fake functions.

## Verification

After each change run `make -C tests native` and `make -C tests proto-abi`; the
PASS counts must match the baseline exactly. Also confirm the root-level wrappers
(`make native-tests`, `make test-core`, …) still work.

## Out of scope

- No changes to shim headers, `test_utils.h`, or the test `.c` logic.
- No changes to library sources under `libs/`.
- No removal of WEAK API stubs (user chose "remove only truly-dead code").
