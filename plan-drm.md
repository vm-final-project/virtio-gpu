# Plan: Refactor `libukvirtgpu_drm` Into an Unikraft fd/ioctl Compatibility Layer

Date: 2026-06-04

## Direct Recommendation

Do not merge DRM ioctl compatibility into the core `libs/libvulkan` runtime.
`libvulkan` should remain the application-facing Vulkan ABI/dispatch boundary.
First remove accidental/legacy dependencies on `libukvirtgpu_drm` from native
Vulkan/llama builds that do not need Linux DRM. Then rewrite the DRM path as an
optional compatibility provider under `libs/libukvirtgpu_drm`, with a Unikraft
`uk_file`/devfs fd facade that routes `ioctl(fd, DRM_IOCTL_VIRTGPU_*, arg)` into
the existing typed translator.

This plan is for the general DRM compatibility architecture, not for the native
VOGUE Vulkan/llama fast path. The native path is:

```text
llama.cpp / ggml-vulkan
  -> libvulkan
  -> libukvulkan_venus
  -> libukvirtio_gpu
```

That path should not require guest DRM. If the project later wants to support
all upstream Mesa Vulkan applications without rewriting them to VOGUE's native
driver/runtime boundary, then guest DRM becomes required: Mesa's virtgpu/Venus
renderer expects `/dev/dri/renderD*`, `DRM_IOCTL_VIRTGPU_*`, GEM/BO handle
lifetime, mmap offsets, syncobj, PRIME/dma-buf, and related Linux DRM behavior.

The practical target is:

```text
Mesa/Linux-style client
  -> open("/dev/dri/renderD128")
  -> ioctl(fd, DRM_IOCTL_VIRTGPU_*, arg)
  -> mmap(fd, drm_virtgpu_map.offset)
  -> Unikraft posix-fdio / ukfile / devfs dispatch
  -> libukvirtgpu_drm fd ioctl callback
  -> uk_drm_virtgpu_* typed operations
  -> libukvirtio_gpu
```

`libvulkan` may expose an opt-in Kconfig selector for this path only if a
consumer needs Linux/Mesa DRM UAPI compatibility. It should not include DRM
headers, own a `/dev/dri` device node, or dispatch DRM ioctls itself.

## Scope: Native Vulkan vs General DRM Compatibility

### Native VOGUE Vulkan/llama Path

The following components should not depend on `libukvirtgpu_drm`:

- `libs/libvulkan`: application-facing Vulkan ABI and dispatch.
- `libs/libukvulkan_venus`: native Venus driver over `libukvirtio_gpu`, unless
  `CONFIG_LIBUKVULKAN_VENUS_USE_DRM_COMPAT=y` is explicitly selected.
- `apps/app-llama-upstream-vk`: upstream llama.cpp + ggml-vulkan appliance.
- `apps/app-vkmark`: native Venus substrate proof.

For these paths, DRM is a compatibility artifact, not an architectural
requirement.

### General DRM Compatibility Path

`libs/libukvirtgpu_drm` remains necessary for:

- `vk.drm-core` and `vk.drm-fdio` tests and evidence rows.
- Mesa/Linux virtgpu UAPI experiments.
- Future fd-compatible `/dev/dri/renderD*` emulation.
- Future full Mesa Vulkan app support.

Full Mesa Vulkan app support is a larger goal than the current native VOGUE
Vulkan path. It requires the fdio/devfs and mmap-offset work in this plan, plus
broader ioctl coverage (`GET_CAPS`, `RESOURCE_INFO`, `GEM_CLOSE`, syncobj,
PRIME/dma-buf) before the claim can be made.

## Best-Practice Research

### Evidence Used

- Official Unikraft syscall shim documentation:
  https://unikraft.org/docs/internals/syscall-shim
  - Establishes that `syscall_shim` maps Linux syscall numbers to handler
    functions, and that handlers are registered with `UK_PROVIDED_SYSCALLS-y`.
  - It does not prescribe putting device-specific ioctl semantics in the
    syscall handler itself.

- Unikraft v0.16.0 release notes:
  https://unikraft.org/releases/v0.16.0
  - Records introduction of `lib/ukfile` and fixes around `posix-fdio`
    handling `ioctl(FIOCLEX|FIONCLEX)`, supporting the current fd/file split.

- Unikraft v0.17.0 release notes:
  https://unikraft.org/blog/2024-06-07-unikraft-releases-v0.17.0
  - States that `posix-tty` owns `ukfile` implementations for serial and
    pseudo-files, reinforcing that concrete file objects own concrete ioctl
    behavior.

- Unikraft GitHub organization and releases:
  https://github.com/unikraft
  https://github.com/unikraft/unikraft/releases
  - Confirms `lib-lwip` is maintained as a separate Unikraft library repo in
    the current ecosystem. This local checkout does not vendor `lib/lwip`, so
    lwIP conclusions below come from upstream lwIP docs/source rather than a
    local `../unikraft/lib/lwip` directory.

- lwIP upstream socket documentation/source:
  https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__socket.html
  https://github.com/lwip-tcpip/lwip/blob/master/src/api/sockets.c
  https://github.com/lwip-tcpip/lwip/blob/master/src/include/lwip/sockets.h
  - Establishes the same pattern at smaller scope: socket ioctl compatibility
    is handled inside the socket stack (`FIONREAD`/`FIONBIO` behavior and
    compatibility options), not by putting every protocol-specific operation in
    a global syscall handler.

- Local Unikraft source, current checkout:
  - `../unikraft/lib/posix-fdio/fd-shim.c`: `UK_LLSYSCALL_R_DEFINE(int,
    ioctl, ...)` dispatches through fd table entries, not device-specific
    switch logic.
  - `../unikraft/lib/posix-fdio/fdctl.c`: `uk_sys_ioctl()` handles generic
    fd-level controls such as `FIONBIO`, then calls `uk_file_ctl(...,
    UKFILE_CTL_IOCTL, ...)`.
  - `../unikraft/lib/posix-tty/serial.c`: `serial_ctl()` handles TTY-specific
    ioctl requests in the file object's control callback.
  - `../unikraft/lib/posix-socket/socket.c`: socket `UKFILE_CTL_IOCTL` routes
    to socket-family driver `.ioctl`.
  - `../unikraft/lib/ukfile/include/uk/file.h`: defines `UKFILE_CTL_IOCTL` as
    the Linux-compatible ioctl request family.

- Mesa Venus documentation:
  https://docs.mesa3d.org/drivers/venus.html
  - Establishes Venus dependence on virtio-gpu context initialization, blob
    resources, host-visible memory, and mmapable host-visible memory behavior.

- Local Mesa source, current checkout:
  - `../mesa/src/virtio/vulkan/vn_renderer_virtgpu.c`: Mesa's Venus virtgpu
    path calls `drmIoctl()` for `GETPARAM`, `GET_CAPS`, `CONTEXT_INIT`,
    `RESOURCE_CREATE_BLOB`, `MAP`, syncobj, PRIME, and submit operations.
    `DRM_IOCTL_VIRTGPU_MAP` returns an offset, then Mesa calls `mmap(fd,
    offset)`.
  - `../mesa/src/drm-shim/device.c` and `../mesa/src/drm-shim/drm_shim.c`:
    Mesa's own DRM shim tracks mmap offsets and routes mmap on shim fds through
    a shim-specific mapping table.

- Linux virtgpu DRM source:
  https://codebrowser.dev/linux/linux/drivers/gpu/drm/virtio/virtgpu_ioctl.c.html
  - Confirms Linux DRM virtgpu request handlers are device/file-private ioctl
    callbacks, with `MAP` returning a GEM mmap offset rather than a CPU virtual
    pointer.

### Version / Date Context

The repo was inspected on 2026-06-04. The local Unikraft and Mesa checkouts are
treated as the implementation baseline for this artifact. The online Unikraft
docs and release notes were checked on the same date for current upstream
guidance.

### Repo-Local Context

- `libs/libukvirtgpu_drm` currently has the correct core translator shape:
  `uk_drm_virtgpu_ioctl()` switches Linux `DRM_IOCTL_VIRTGPU_*` request numbers
  into typed `uk_drm_virtgpu_*` operations.
- It currently does not provide a fd namespace object, `/dev/dri/renderD128`,
  `mmap(fd, offset)`, `GEM_CLOSE`, `RESOURCE_INFO`, syncobj, PRIME import/export,
  or per-open fd lifetime semantics.
- `libs/libvulkan/README.md` and `libs/libvulkan/Config.uk` explicitly define
  `libvulkan` as the app-facing Vulkan ABI/runtime boundary and state that it
  does not own Linux DRM compatibility.
- `libs/libukvulkan_venus` already owns the native Venus driver-open path via
  `uk_vulkan_venus_open()` and direct `libukvirtio_gpu` calls. That native path
  should remain the default.
- `apps/app-llama-upstream-vk/Config.uk` selects `LIBVULKAN` and does not select
  `LIBUKVIRTGPU_DRM`; however, `kraft/Kraftfile.llama-upstream-vk` and
  `kraft/Kraftfile.llama-upstream-vk-server` currently set
  `CONFIG_LIBUKVIRTGPU_DRM: 'y'` and list `libukvirtgpu_drm` as a library. This
  is a legacy/compatibility dependency to remove after verifying the native
  path still builds and runs.
- `../unikraft/lib/lwip` is absent in this local checkout; because Unikraft
  keeps `lib-lwip` as a separate library repo, this plan uses upstream lwIP
  socket ioctl behavior as supporting evidence, not as local implementation
  evidence.

## ADR

### Decision

Keep `libukvirtgpu_drm` as a separate optional compatibility library, but first
remove dependencies from paths that do not need DRM, then rewrite it into three
layers:

1. `core`: typed virtgpu DRM UAPI translator over `libukvirtio_gpu`.
2. `fdio/devfs`: optional `/dev/dri/renderD128` file object whose ioctl callback
   calls the core translator.
3. `mmap-offset`: optional fd-compatible offset registry so `DRM_IOCTL_MAP`
   returns an offset that `mmap(fd, offset)` can resolve.

Do not integrate the implementation into `libs/libvulkan`. At most, add an
opt-in config path where `libvulkan` or a Mesa-compat application can select
the DRM fd compatibility library.

### Drivers

- Match Unikraft's current fd/ioctl dispatch model.
- Preserve `libvulkan`'s clean Vulkan ABI boundary.
- Allow upstream Mesa/Linux virtgpu code to run through the expected
  open/ioctl/mmap sequence.
- Keep the native VOGUE Venus path smaller, faster, and easier to verify.
- Make compatibility claims precise: fd-compatible DRM path is not the same
  claim as native `libvulkan`/Venus dispatch.
- Make full Mesa Vulkan app support a future compatibility claim that requires
  the complete DRM fd path, not a claim implied by native llama/ggml support.

### Alternatives Considered

- Put `DRM_IOCTL_*` handling directly in `UK_SYSCALL_DEFINE(ioctl)`.
  - Rejected: duplicates or bypasses `posix-fdio` dispatch and mixes every DRM
    request into a global syscall handler. It is contrary to the observed
    Unikraft pattern where fd lookup happens once and concrete file objects own
    concrete ioctl behavior.

- Merge all DRM compatibility into `libs/libvulkan`.
  - Rejected for the core runtime: `libvulkan` owns Vulkan ABI and dispatch,
    not Linux device-node emulation. This would make a compute-first Vulkan
    library depend on Linux DRM UAPI state and mmap rules even when native
    Venus does not need them.
  - Acceptable limited variant: `libvulkan` can expose or select an optional
    compatibility config if a build deliberately wants Mesa/Linux DRM UAPI.

- Leave `libukvirtgpu_drm` as a library-level helper only.
  - Rejected as final state: it cannot satisfy upstream Mesa's expected
    `open("/dev/dri/renderD128")`, `ioctl(fd, ...)`, `mmap(fd, offset)`, and
    per-fd lifetime behavior.

## Requirements

1. Preserve the existing host-native `uk_drm_virtgpu_*` translator tests.
2. Hide Linux DRM compatibility from the default native `libvulkan` path.
3. Remove explicit `libukvirtgpu_drm` dependencies from native Vulkan/llama
   build definitions that do not need them.
4. Add a compatibility fd path that follows Unikraft's `UKFILE_CTL_IOCTL`
   callback pattern.
5. Make `DRM_IOCTL_VIRTGPU_MAP` return an mmap offset in fd-compatible mode,
   not a CPU virtual address.
6. Keep per-open state isolated: contexts, BO handles, mmap offsets, and close
   cleanup must be bound to the render-node file object.
7. Document unsupported ioctls with explicit `-ENOSYS`, `-EINVAL`, `-ENOTTY`, or
   `-EOPNOTSUPP` policy.
8. Do not claim full Linux DRM, full Mesa, or full Vulkan conformance.
9. State explicitly that future full Mesa Vulkan app support requires the full
   DRM compatibility path.

## Implementation Plan

### Phase 0: Remove DRM Dependencies From Native Paths That Do Not Need Them

Files:

- `kraft/Kraftfile.llama-upstream-vk`
- `kraft/Kraftfile.llama-upstream-vk-server`
- `apps/app-llama-upstream-vk/Config.uk`
- `libs/libvulkan/Config.uk`
- `libs/libukvulkan_venus/Config.uk`
- `libs/libvulkan/README.md`
- `libs/libukvulkan_venus/README.md`
- `README.md`
- `docs/llama-cpp-unikraft-porting-plan.md`
- `docs/dependency-graph-plan.md`
- `scripts/eval_matrix.py`
- `scripts/app_multi_env_bench.py`
- generated paper/evidence text after regeneration, if affected

Actions:

- Remove `CONFIG_LIBUKVIRTGPU_DRM: 'y'` from the llama Vulkan Kraftfiles unless
  the same build explicitly selects a Mesa/Linux DRM compatibility mode.
- Remove the `libukvirtgpu_drm` library entry from those Kraftfiles when the
  native `libvulkan -> libukvulkan_venus -> libukvirtio_gpu` path builds without
  it.
- Keep `CONFIG_LIBUKVULKAN_VENUS_USE_DRM_COMPAT` default `n`; do not select it
  from `LIBVULKAN`, `APP_LLAMA_UPSTREAM_VK`, or `APP_VKMARK`.
- Add or keep a separate opt-in build profile only for Mesa/Linux DRM UAPI
  compatibility experiments.
- Update stale dependency graph and porting docs that still describe
  `llama.cpp/ggml -> libukvirtgpu_drm` as the native path.
- Update evaluation scripts so `llm.bench.vk` and native Vulkan/ggml rows are
  not blocked by DRM compatibility; keep `vk.drm-core` and `vk.drm-fdio` as
  independent compatibility/evidence rows.

Acceptance:

- `rg "CONFIG_LIBUKVIRTGPU_DRM: 'y'" kraft/Kraftfile.llama-upstream-vk*` returns
  no matches unless the file name or config explicitly says Mesa/DRM compat.
- `rg "libukvirtgpu_drm" libs/libvulkan apps/app-llama-upstream-vk` finds no
  implementation dependency.
- `CONFIG_LIBVULKAN=y` does not select `LIBUKVIRTGPU_DRM`.
- `CONFIG_APP_LLAMA_UPSTREAM_VK=y` does not select `LIBUKVIRTGPU_DRM`.
- Native llama Vulkan build/test evidence remains valid without the DRM shim.
- `vk.drm-core` and `vk.drm-fdio` tests still run as separate compatibility
  gates.

Test updates:

- Add a lightweight script-level guard, for example
  `scripts/check_native_vulkan_no_drm.py`, that fails if native llama/Vulkan
  Kraftfiles or app Kconfigs select `CONFIG_LIBUKVIRTGPU_DRM` outside an
  explicitly named Mesa/DRM compatibility profile.
- Wire the guard into an existing repo gate such as `make naming-check`,
  `make governance-check`, or a new narrow target
  `make native-vulkan-no-drm-check`.
- Update `tests/README.md` to document that `virtgpu_drm_ioctl_test` proves the
  compatibility row only and is not a prerequisite for native llama/ggml
  Vulkan.
- Add a negative fixture or grep assertion for `CONFIG_APP_LLAMA_UPSTREAM_VK=y`
  builds: `LIBVULKAN` and `APP_LLAMA_UPSTREAM_VK` must not select
  `LIBUKVIRTGPU_DRM`.

### Phase 1: Freeze the Current Translator as `core`

Files:

- `libs/libukvirtgpu_drm/drm_virtgpu.c`
- `libs/libukvirtgpu_drm/include/uk/drm_virtgpu.h`
- `tests/virtgpu_drm_ioctl_test.c`

Actions:

- Rename comments and README language from "ioctl surface" to "core translator"
  where appropriate.
- Keep `uk_drm_virtgpu_ioctl()` as a narrow dispatcher for direct unit tests and
  fd callback reuse.
- Add explicit coverage for Mesa-relevant requests: implemented `GET_CAPS`,
  `RESOURCE_INFO`, and `GEM_CLOSE`; unsupported syncobj and PRIME. Mark each as
  unsupported, planned, or implemented.
- Preserve the current direct-call tests so the core stays testable without
  fdio/devfs.

Acceptance:

- `make -C tests virtgpu-drm` or the existing native target still passes.
- Unsupported request behavior is deterministic and documented.

Test updates:

- Extend `tests/virtgpu_drm_ioctl_test.c` with success-path cases for
  `DRM_IOCTL_VIRTGPU_GET_CAPS`, `DRM_IOCTL_VIRTGPU_RESOURCE_INFO`, and
  `DRM_IOCTL_GEM_CLOSE`.
- Assert exact `-ENOSYS` boundaries for unsupported syncobj and PRIME request
  families.
- Keep the existing success checks for `GETPARAM`, `CONTEXT_INIT`,
  `RESOURCE_CREATE_BLOB`, `MAP`, `EXECBUFFER`, and `WAIT` unchanged.

### Phase 2: Add the fdio/devfs Facade

New or changed files:

- `libs/libukvirtgpu_drm/drm_virtgpu_fdio.c`
- `libs/libukvirtgpu_drm/include/uk/drm_virtgpu_fdio.h`
- `libs/libukvirtgpu_drm/Config.uk`
- `libs/libukvirtgpu_drm/Makefile.uk`
- `tests/virtgpu_drm_fdio_test.c`

Actions:

- Add `CONFIG_LIBUKVIRTGPU_DRM_FDIO`, default `n`.
- Depend on the Unikraft fd/file pieces needed by the target checkout
  (`LIBUKFILE`, `LIBPOSIX_FDIO`, `LIBDEVFS` or the exact available symbols in
  this Unikraft tree).
- Register a render-node file object for `/dev/dri/renderD128` when devfs is
  enabled.
- Implement a file control callback:

```c
static int virtgpu_drm_file_ctl(const struct uk_file *f, int fam, int req,
                                uintptr_t arg1, uintptr_t arg2,
                                uintptr_t arg3)
{
        struct uk_drm_virtgpu_file *vf = container_of(f, ...);

        if (fam != UKFILE_CTL_IOCTL)
                return -ENOSYS;

        return uk_drm_virtgpu_ioctl(&vf->dev, req, (void *)arg1);
}
```

- Allocate one `struct uk_drm_virtgpu_dev` per opened render node.
- Call `uk_drm_virtgpu_open()` on open and `uk_drm_virtgpu_close()` on close.
- Keep the direct `uk_drm_virtgpu_ioctl()` API available for tests and native
  code that intentionally bypasses fd compatibility.

Acceptance:

- A native test can open/create the render-node object, call `ioctl(fd,
  DRM_IOCTL_VIRTGPU_GETPARAM, ...)`, and observe the same result as direct
  `uk_drm_virtgpu_ioctl()`.
- Closing one fd releases only that fd's BO/context state.

Test updates:

- Add `tests/virtgpu_drm_fdio_test.c`.
- Add a `make -C tests virtgpu-drm-fdio` target and include it in `native` only
  after it is deterministic without QEMU/GPU/devfs runtime dependencies.
- Test at least:
  - fd/open initialization creates independent per-open `uk_drm_virtgpu_dev`
    state.
  - `UKFILE_CTL_IOCTL` dispatch reaches `uk_drm_virtgpu_ioctl()`.
  - invalid fd/control family returns the documented errno.
  - closing one fd does not destroy another fd's BO/context state.

### Phase 3: Rewrite `MAP` as fd-Compatible Offset + mmap Resolution

Files:

- `libs/libukvirtgpu_drm/drm_virtgpu.c`
- `libs/libukvirtgpu_drm/drm_virtgpu_fdio.c`
- `libs/libukvirtgpu_drm/include/uk/drm_virtgpu.h`
- `tests/virtgpu_drm_fdio_test.c`

Actions:

- Split mapping into two APIs:
  - `uk_drm_virtgpu_map_direct()` for existing direct-library tests that want
    the CPU pointer.
  - `uk_drm_virtgpu_map_offset()` for fd-compatible mode that returns an opaque
    page-aligned offset.
- Add an offset registry on the per-fd object. The registry maps offset -> BO.
- Implement file mmap handling if the local Unikraft `uk_file`/devfs stack has
  an mmap hook in this checkout. If it does not, document the missing upstream
  hook and keep this phase gated behind a test that currently fails/skips with a
  clear reason.
- Ensure `DRM_IOCTL_VIRTGPU_MAP` in fd-compatible mode returns the offset, not
  the pointer.

Acceptance:

- `DRM_IOCTL_VIRTGPU_MAP` followed by `mmap(fd, offset)` returns the same mapped
  blob pointer currently exposed by direct mode.
- Direct mode still works for host-native tests.
- The README distinguishes direct-mode "pointer as offset" from fd-compatible
  "offset resolved by mmap".

Test updates:

- Extend `tests/virtgpu_drm_fdio_test.c` with mmap-offset cases:
  - `DRM_IOCTL_VIRTGPU_MAP` returns a page-aligned opaque offset in fd mode.
  - `mmap(fd, offset)` resolves to the mapped blob for the same fd.
  - an offset from fd A cannot be used through fd B.
  - unknown offsets fail deterministically.
- Keep a direct-mode regression in `tests/virtgpu_drm_ioctl_test.c` proving
  legacy direct mapping behavior still works until that API is intentionally
  retired.
- If the local Unikraft checkout has no usable mmap hook for `uk_file`/devfs,
  add a skipped/blocked test path that emits the exact missing hook and keeps
  the direct translator tests passing.

### Phase 4: Add Mesa-Required Request Coverage Incrementally

Priority order from Mesa `vn_renderer_virtgpu.c`. This phase is only required
for Mesa/Linux virtgpu compatibility and future full Mesa Vulkan app support; it
is not required for the native VOGUE llama/ggml Vulkan path.

1. Already supported or mostly supported:
   - `DRM_IOCTL_VIRTGPU_GETPARAM`
   - `DRM_IOCTL_VIRTGPU_CONTEXT_INIT`
   - `DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB`
   - `DRM_IOCTL_VIRTGPU_MAP`
   - `DRM_IOCTL_VIRTGPU_EXECBUFFER`
   - `DRM_IOCTL_VIRTGPU_WAIT`
2. Needed for closer upstream Mesa compatibility:
   - `DRM_IOCTL_VIRTGPU_GET_CAPS`
   - `DRM_IOCTL_VIRTGPU_RESOURCE_INFO`
   - `DRM_IOCTL_GEM_CLOSE`
3. Needed only when enabling full Mesa sync/share paths:
   - `DRM_IOCTL_SYNCOBJ_*`
   - `DRM_IOCTL_PRIME_HANDLE_TO_FD`
   - `DRM_IOCTL_PRIME_FD_TO_HANDLE`

Actions:

- Add one request family at a time with tests.
- Keep unsupported families returning deterministic errors until implemented.
- Avoid pulling syncobj/PRIME into the native `libvulkan` path.

Acceptance:

- A coverage table maps every Mesa request to implemented/planned/unsupported.
- Tests prove the current advertised subset.

Test updates:

- Add table-driven request coverage in `tests/virtgpu_drm_ioctl_test.c` so each
  Mesa `vn_renderer_virtgpu.c` request family has an implemented/planned/
  unsupported assertion.
- Add success-path tests for `GET_CAPS`, `RESOURCE_INFO`, and `GEM_CLOSE` as
  they are implemented.
- Add separate tests for syncobj and PRIME/dma-buf only when those families are
  implemented; until then, assert documented negative errnos.
- Update `scripts/check_venus_vulkan_docs.py` or add a companion check so docs
  cannot claim full Mesa Vulkan app support unless the required request families
  are implemented and tested.

### Phase 5: Kconfig Boundary and Optional `libvulkan` Selection

Files:

- `libs/libvulkan/Config.uk`
- `libs/libvulkan/README.md`
- `libs/libukvulkan_venus/Config.uk`
- `libs/libukvirtgpu_drm/README.md`

Actions:

- Keep `CONFIG_LIBVULKAN` selecting native `LIBUKVULKAN_VENUS`, not DRM fdio.
- Keep native app Kconfigs (`APP_LLAMA_UPSTREAM_VK`, `APP_VKMARK`) from
  selecting `LIBUKVIRTGPU_DRM`.
- Add an explicit opt-in only if a build deliberately wants Mesa/Linux DRM UAPI:

```text
config LIBVULKAN_ENABLE_DRM_FD_COMPAT
        bool "Enable Linux virtgpu DRM fd compatibility for Mesa-style clients"
        depends on LIBVULKAN
        default n
        select LIBUKVIRTGPU_DRM_FDIO
```

- State that this option does not move DRM ownership into `libvulkan`; it only
  enables a parallel compatibility device node for clients that need it.
- State that full Mesa Vulkan app support requires this path plus broader ioctl
  coverage; native llama/ggml Vulkan support does not.
- Remove or correct stale references that say the old retired ICD bootstrap
  still pulls DRM indirectly.

Acceptance:

- `LIBVULKAN=y` without the compat option builds without DRM UAPI dependency.
- Compat-enabled builds include the render-node facade.
- Documentation does not imply `libvulkan` owns DRM compatibility.

Test updates:

- Extend the Phase 0 Kconfig guard to cover the new opt-in selector:
  - default `LIBVULKAN=y` must not select `LIBUKVIRTGPU_DRM`.
  - `LIBVULKAN_ENABLE_DRM_FD_COMPAT=y` may select `LIBUKVIRTGPU_DRM_FDIO`.
  - `APP_LLAMA_UPSTREAM_VK=y` must remain DRM-free by default.
- Add a docs check that the phrase "full Mesa Vulkan app support" appears only
  with the DRM compatibility requirement, not as a native Vulkan claim.

### Phase 6: Cleanup and Hide the Old Surface

Actions:

- Move internal-only declarations to private headers.
- Keep public API limited to:
  - direct test/core translator API, if still intentionally supported;
  - fdio registration/open helpers, if needed by devfs glue.
- Update `exportsyms.uk` so only deliberate public symbols remain.
- Rename tests to separate core translator tests from fd compatibility tests.
- Update governance checks/readmes/evidence matrix rows to separate:
  - `vk.drm-core`: request translation works;
  - `vk.drm-fdio`: fd/ioctl/mmap compatibility works;
  - `vulkan-native`: native libvulkan path works without DRM.

Acceptance:

- No app-facing code includes DRM headers unless it opts into Linux DRM UAPI.
- `rg "uk_drm_virtgpu" libs/libvulkan` returns no implementation dependency
  unless the optional selector documentation is counted.

Test updates:

- Add or update a governance check that fails if `libs/libvulkan` includes
  `<uk/drm_virtgpu.h>` or `<drm/virtgpu_drm.h>`.
- Update `tests/Makefile` grouping so DRM core/fdio tests are visibly separate
  from native Venus/Vulkan dispatch tests.
- Update generated evidence checks so `vk.drm-core`, `vk.drm-fdio`, and
  `vulkan-native` rows each have independent pass/block conditions.

## Test Update Plan

The refactor must update tests in lockstep with the commit sequence. Do not
merge broad architectural changes that only update docs.

### New Tests and Checks

- `scripts/check_native_vulkan_no_drm.py` or equivalent Makefile-backed check:
  proves native llama/Vulkan configs do not select `LIBUKVIRTGPU_DRM`.
- `tests/virtgpu_drm_fdio_test.c`: proves fdio/devfs ioctl dispatch, per-fd
  state, close cleanup, and later mmap-offset resolution.
- `make -C tests virtgpu-drm-fdio`: focused fdio compatibility target.
- Docs/evidence guard: prevents claiming full Mesa Vulkan app support unless
  the general DRM compatibility path and required Mesa request families are
  implemented.

### Tests to Update

- `tests/virtgpu_drm_ioctl_test.c`: keep direct translator success tests; add
  unsupported-request classification; add success-path `GET_CAPS`,
  `RESOURCE_INFO`, `GEM_CLOSE`; keep syncobj and PRIME as exact `-ENOSYS`
  boundaries until implemented.
- `tests/Makefile`: add explicit `virtgpu-drm` alias for the direct translator;
  add `virtgpu-drm-fdio`; keep DRM tests grouped separately from native
  Vulkan/dispatch tests.
- `tests/README.md`: document that DRM tests prove compatibility rows, not a
  native llama/ggml dependency.
- `scripts/eval_matrix.py` and related checks: split native Vulkan/llama row
  conditions from `vk.drm-core` and `vk.drm-fdio`.
- `scripts/check_venus_vulkan_docs.py`: enforce the native-vs-Mesa-compat claim
  boundary.

### Regression Gates by Intent

- Native no-DRM path:
  `make native-vulkan-no-drm-check`, `make llama-vulkan-api-coverage`,
  `make llama-ggml-vk-dispatch`.
- DRM core compatibility:
  `make -C tests virtgpu-drm`, `make -C tests g5`, `make -C tests native`.
- DRM fdio compatibility:
  `make -C tests virtgpu-drm-fdio`.
- Docs/evidence consistency:
  `make lib-readme-check`, `make eval-check`, `make current-stage-check`,
  `make test-fast`.

## Commit Plan

Use short imperative subjects with a scope prefix, matching the repository's
current style (`docs:`, `drm:`, `vulkan:`, `llama:`). Each commit should be
reviewable on its own and should mention the evidence run or intentionally not
run in the commit body.

### Commit 1: Remove Native Vulkan DRM Selection

Suggested subject:

```text
llama: drop DRM shim from native Vulkan builds
```

Scope:

- Remove `CONFIG_LIBUKVIRTGPU_DRM: 'y'` from native llama Vulkan Kraftfiles.
- Remove native Kraftfile `libukvirtgpu_drm` library entries when the build no
  longer needs them.
- Add/update the native no-DRM config guard.
- Do not touch `libs/libukvirtgpu_drm` implementation in this commit.

Primary files:

- `kraft/Kraftfile.llama-upstream-vk`
- `kraft/Kraftfile.llama-upstream-vk-server`
- `scripts/check_native_vulkan_no_drm.py` or equivalent check target
- `Makefile`

Evidence:

- `make native-vulkan-no-drm-check` if added
- `rg "CONFIG_LIBUKVIRTGPU_DRM: 'y'" kraft/Kraftfile.llama-upstream-vk*`
- Native llama Vulkan build or the closest available build gate.
- `make llama-ggml-vk-dispatch` if available in the current environment.

### Commit 2: Correct Native Vulkan Claim Boundaries

Suggested subject:

```text
docs: separate native Vulkan from DRM compatibility
```

Scope:

- Update README and porting/dependency docs so native
  `llama.cpp/ggml -> libvulkan -> libukvulkan_venus -> libukvirtio_gpu` is not
  described as requiring `libukvirtgpu_drm`.
- State explicitly that future full Mesa Vulkan app support needs the general
  DRM fd compatibility path.
- Keep `vk.drm-core` and `vk.drm-fdio` documented as independent
  compatibility/evidence rows.
- Update tests/docs checks so this boundary is enforced.

Primary files:

- `README.md`
- `docs/llama-cpp-unikraft-porting-plan.md`
- `docs/dependency-graph-plan.md`
- `libs/libvulkan/README.md`
- `libs/libukvulkan_venus/README.md`
- `tests/README.md`
- `scripts/check_venus_vulkan_docs.py`

Evidence:

- `make lib-readme-check`
- `python3 scripts/check_venus_vulkan_docs.py`
- `rg "libukvirtgpu_drm|vk.drm-core|vk.drm-fdio|llama.cpp|ggml" README.md docs libs/libvulkan libs/libukvulkan_venus`

### Commit 3: Decouple Native Evaluation From DRM Compatibility

Suggested subject:

```text
eval: decouple native Vulkan rows from DRM shim
```

Scope:

- Update evaluation scripts and generated evidence text so native Vulkan/llama
  rows are not blocked by DRM compatibility.
- Keep `vk.drm-core` and `vk.drm-fdio` as separate compatibility rows with
  their own tests.
- Update evaluation tests/checks for independent row conditions.
- Regenerate generated paper/evidence artifacts only if the scripts own them.

Primary files:

- `scripts/eval_matrix.py`
- `scripts/app_multi_env_bench.py`
- `config/row_id_map.json`
- `scripts/current_stage_report.py` if row grouping changes
- generated `paper/generated/*` artifacts if regenerated by the repo scripts

Evidence:

- `make eval-check`
- `make current-stage-check` if affected by evidence wording.
- `make test-fast` when script changes are broad.

### Commit 4: Freeze DRM Translator Scope

Suggested subject:

```text
drm: define core translator boundary
```

Scope:

- Make `libukvirtgpu_drm` README/comments describe the current direct translator
  as a core compatibility layer, not a fd-complete DRM device.
- Add deterministic success coverage for `GET_CAPS`, `RESOURCE_INFO`, and
  `GEM_CLOSE`.
- Add deterministic unsupported-request classification for syncobj and PRIME.
- Preserve existing direct-call tests and extend them with exact negative errno
  assertions.

Primary files:

- `libs/libukvirtgpu_drm/README.md`
- `libs/libukvirtgpu_drm/drm_virtgpu.c`
- `libs/libukvirtgpu_drm/include/uk/drm_virtgpu.h`
- `tests/virtgpu_drm_ioctl_test.c`

Evidence:

- `make -C tests g5`
- `make -C tests native`

### Commit 5: Add DRM fdio Compatibility Facade

Suggested subject:

```text
drm: add optional fdio render-node facade
```

Scope:

- Add `CONFIG_LIBUKVIRTGPU_DRM_FDIO`, default `n`.
- Add the Unikraft `uk_file`/devfs render-node object and ioctl callback.
- Add tests proving `ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, ...)` reaches the
  existing typed translator.
- Add tests for per-fd state isolation and close cleanup.

Primary files:

- `libs/libukvirtgpu_drm/Config.uk`
- `libs/libukvirtgpu_drm/Makefile.uk`
- `libs/libukvirtgpu_drm/drm_virtgpu_fdio.c`
- `libs/libukvirtgpu_drm/include/uk/drm_virtgpu_fdio.h`
- `tests/virtgpu_drm_fdio_test.c`
- `tests/Makefile`

Evidence:

- `make -C tests virtgpu-drm-fdio`
- `make -C tests native`

### Commit 6: Implement fd-Compatible DRM mmap Offsets

Suggested subject:

```text
drm: resolve virtgpu mmap offsets through fdio
```

Scope:

- Split direct pointer mapping from fd-compatible offset mapping.
- Add per-fd offset registry.
- Implement or explicitly gate fd mmap resolution based on local Unikraft mmap
  hook availability.
- Add tests for valid offset resolution, unknown offset failure, and cross-fd
  offset isolation.

Primary files:

- `libs/libukvirtgpu_drm/drm_virtgpu.c`
- `libs/libukvirtgpu_drm/drm_virtgpu_fdio.c`
- `libs/libukvirtgpu_drm/include/uk/drm_virtgpu.h`
- `tests/virtgpu_drm_fdio_test.c`

Evidence:

- `make -C tests virtgpu-drm-fdio`
- Test evidence showing `DRM_IOCTL_VIRTGPU_MAP` followed by `mmap(fd, offset)`
  succeeds, or a documented skip/blocker if the local Unikraft mmap hook is
  unavailable.

### Commit 7: Expand Mesa Compatibility Request Coverage

Suggested subject:

```text
drm: add Mesa virtgpu request coverage
```

Scope:

- Add `GET_CAPS`, `RESOURCE_INFO`, and `GEM_CLOSE` before syncobj/PRIME.
- Add syncobj and PRIME/dma-buf only in follow-up commits if needed for the
  next Mesa app compatibility target.
- Keep every unsupported request explicitly classified until implemented.
- Update table-driven request coverage tests for each newly implemented family.

Primary files:

- `libs/libukvirtgpu_drm/drm_virtgpu.c`
- `libs/libukvirtgpu_drm/include/drm/virtgpu_drm.h`
- `tests/virtgpu_drm_ioctl_test.c`
- `tests/virtgpu_drm_fdio_test.c`

Evidence:

- `make -C tests g5`
- `make -C tests virtgpu-drm-fdio`
- Coverage table in docs or test output maps Mesa requests to status.

### Commit 8: Add Opt-In Mesa DRM Compatibility Selector

Suggested subject:

```text
vulkan: gate Mesa DRM compatibility behind opt-in config
```

Scope:

- Add an opt-in selector only after the fdio path exists.
- Keep `CONFIG_LIBVULKAN=y` and native app Kconfigs from selecting DRM by
  default.
- Document that this is for Mesa/Linux DRM UAPI clients and future full Mesa
  Vulkan app support, not native llama/ggml.
- Extend config tests so default native Vulkan remains DRM-free while the opt-in
  selector pulls the fdio compatibility path.

Primary files:

- `libs/libvulkan/Config.uk`
- `libs/libvulkan/README.md`
- `libs/libukvulkan_venus/Config.uk`
- `libs/libukvirtgpu_drm/README.md`

Evidence:

- `make native-vulkan-no-drm-check`
- `make lib-readme-check`
- Config search proving native `LIBVULKAN` and `APP_LLAMA_UPSTREAM_VK` do not
  select `LIBUKVIRTGPU_DRM`.

### Commit 9: Rename Evidence Rows for Final Boundaries

Suggested subject:

```text
docs: split DRM and native Vulkan evidence rows
```

Scope:

- Separate `vk.drm-core`, `vk.drm-fdio`, and `vulkan-native` evidence naming.
- Update governance metadata, generated paper tables, and scripts consistently.
- Keep old row names only as documented aliases if downstream scripts still
  consume them.
- Update tests/checks so each row has its own pass/block condition.

Primary files:

- `config/row_id_map.json`
- `config/governance.json`
- `scripts/current_stage_report.py`
- `scripts/eval_matrix.py`
- `paper/generated/*`
- `README.md`

Evidence:

- `make eval-check`
- `make current-stage-check`
- `make test-fast`

## Verification Gates

Run after each implementation phase when applicable:

```sh
make -C tests native
make governance-check lib-readme-check app-port-check
make llama-vulkan-api-coverage
make llama-ggml-vk-dispatch
make test-fast
```

Additional target to add:

```sh
make -C tests virtgpu-drm-fdio
```

Expected evidence:

- Core translator direct tests still pass.
- fdio test proves `ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, ...)`.
- mmap test proves `DRM_IOCTL_VIRTGPU_MAP` offset can be resolved through fd
  mmap, or the plan remains explicitly blocked on missing local Unikraft mmap
  hook support.
- `libvulkan` native tests pass with DRM fd compatibility disabled.
- llama/ggml native Vulkan build/test evidence passes with
  `CONFIG_LIBUKVIRTGPU_DRM` disabled.
- Mesa/Linux DRM compatibility builds are separately labeled and do not become
  prerequisites for native `llm.bench.vk`.

## Risks and Mitigations

- Risk: `libvulkan` absorbs Linux DRM responsibilities and becomes a confused
  loader/device-node hybrid.
  - Mitigation: keep compat implementation under `libukvirtgpu_drm`; `libvulkan`
    only has an opt-in Kconfig selector if needed.

- Risk: `MAP` keeps returning a CPU pointer and falsely appears Mesa-compatible.
  - Mitigation: split direct pointer mapping from fd-compatible offset mapping.

- Risk: per-fd state leaks across clients.
  - Mitigation: allocate context/BO/mmap state per render-node open and test
    independent fd close behavior.

- Risk: syncobj/PRIME scope explodes.
  - Mitigation: stage those request families after `GET_CAPS`,
    `RESOURCE_INFO`, and `GEM_CLOSE`; document unsupported behavior until then.

- Risk: removing legacy DRM selection from llama Kraftfiles breaks a hidden
  dependency.
  - Mitigation: remove it under a dedicated Phase 0 gate, then run native
    Vulkan/llama build and dispatch tests before editing claim text.

- Risk: future readers assume "DRM optional" also means "full Mesa apps work
  without DRM".
  - Mitigation: keep the scope split explicit in README, plan, and evidence
    rows: native VOGUE Vulkan does not need guest DRM; full Mesa/Linux Vulkan
    app compatibility does.

- Risk: devfs/fdio APIs differ across Unikraft versions.
  - Mitigation: phase 2 starts by pinning exact local API symbols and adding a
    compile-only fdio facade test before broader behavior changes.

## Stop Condition

The refactor is complete when:

- Native `libvulkan` works without selecting DRM compatibility.
- Native llama/ggml Vulkan works without selecting `LIBUKVIRTGPU_DRM`.
- `libukvirtgpu_drm` has a tested core translator and a tested fdio/devfs
  compatibility path.
- `DRM_IOCTL_VIRTGPU_MAP` + `mmap(fd, offset)` works in compatibility mode.
- Unsupported Mesa/Linux virtgpu request families are explicitly classified.
- Docs and evidence rows clearly separate native Vulkan, DRM core translation,
  and fd-compatible DRM emulation.
- The plan and docs explicitly state that future full Mesa Vulkan app support
  requires the general DRM compatibility path.
