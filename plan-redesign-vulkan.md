# Plan: Redesign the Vulkan Path Around `libvulkan` and a Venus Driver

## Goal

Redesign the current llama.cpp Vulkan path from a ggml-specific dispatch library:

```text
application / llama.cpp
  -> upstream ggml-vulkan.cpp
  -> libukggml_vk
  -> libukvenus
  -> libukvirtgpu_drm / libukvirtio_gpu
  -> QEMU virtio-gpu-gl + virglrenderer Venus
  -> host Vulkan driver
```

to a Vulkan-layered Unikraft design:

```text
application / llama.cpp
  -> upstream Vulkan API calls
  -> libvulkan
       - exported vk* ABI
       - Vulkan loader/runtime/dispatch semantics
       - common Vulkan object bookkeeping
       - statically links one Vulkan driver implementation
  -> libukvulkan_venus
       - Unikraft-native Venus Vulkan driver implementation
       - Venus protocol encode/decode
       - Venus instance/device/queue/command behavior
       - Venus capset/context/ring/reply transport policy
  -> libukvirtio_gpu
       - VirtIO-GPU frontend: controlq, capsets, contexts, blobs, map/unmap,
         SUBMIT_3D, fences
  -> QEMU virtio-gpu-gl + virglrenderer Venus
  -> host Vulkan driver
```

`libukvirtgpu_drm` remains optional. Use it only when the project deliberately
wants a Linux virtgpu DRM UAPI compatibility layer, for example to maximize
reuse of Mesa/Linux-oriented paths. A native Unikraft Venus stack should prefer
calling `libukvirtio_gpu` directly.

The first supported client remains upstream llama.cpp through
`ggml-vulkan.cpp`. The library boundary must not claim full Vulkan
compatibility until broader application coverage and claim gates exist.

## Evidence Checked

This plan is based on official documentation and current source code.

- Khronos Vulkan Loader Guide:
  https://docs.vulkan.org/guide/latest/loader.html
  - The loader maps applications to layers and ICDs.
  - Vulkan headers provide prototypes; applications link to a loader or build a
    dispatch table.
- Khronos Checking For Vulkan Support:
  https://docs.vulkan.org/guide/latest/checking_for_support.html
  - Vulkan requires both a loader and a driver/implementation.
  - The driver translates Vulkan API calls into a valid implementation.
- Mesa Venus documentation:
  https://docs.mesa3d.org/drivers/venus.html
  - Venus is a Virtio-GPU protocol for Vulkan command serialization.
  - Venus requires virtio-gpu support for 3D features, capset query, resource
    blobs, host-visible memory, and context init.
- Unikraft architecture:
  https://unikraft.org/docs/internals/architecture
  - Components are independently selectable micro-libraries with clear API
    boundaries.
- Unikraft build system:
  https://unikraft.org/docs/internals/build-system
  - Libraries use `Config.uk`, `Makefile.uk`, `addlib`, `LIB*` namespaces, and
    `exportsyms.uk` for explicit external APIs.

Source anchors inspected in this workspace:

| Concern | Mesa / VOGUE source | What it establishes |
|---|---|---|
| ICD entry | `../mesa/src/virtio/vulkan/vn_icd.c:15-19` | Mesa's ICD entry is thin: `vk_icdGetInstanceProcAddr()` forwards to `vn_GetInstanceProcAddr()`. This is not equivalent to VOGUE's current `libukvk_icd` bootstrap library. |
| Driver proc lookup | `../mesa/src/virtio/vulkan/vn_instance.c:465-469` | Venus driver proc lookup delegates to Mesa Vulkan runtime. |
| Vulkan runtime lookup rules | `../mesa/src/vulkan/runtime/vk_instance.c:298-345` | NULL-instance `vkGetInstanceProcAddr` behavior is runtime policy, not transport policy. |
| Venus renderer/substrate | `../mesa/src/virtio/vulkan/vn_renderer_virtgpu.c` | Mesa's virtgpu substrate sits in the Venus driver stack, not in the loader. |
| Venus ring | `../mesa/src/virtio/vulkan/vn_ring.c` | Ring submission is Venus driver transport behavior. |
| Current static dispatch | `libs/libukggml_vk/uk_vulkan_dispatch.c` | Current `vk*` entry points and dispatch state live in a ggml-named library and must move to `libvulkan`. |
| Current ICD shim | `libs/libukvk_icd/vulkan_icd.c` | Current `libukvk_icd` probes virtgpu/Venus capabilities and opens a context; it does not implement Vulkan API semantics. |
| Current Venus encoder | `libs/libukvenus/README.md` | Current Venus layer already uses generated Mesa `vn_encode_vk*` protocol code but is documented too narrowly as an encoder helper. |
| Current DRM shim | `libs/libukvirtgpu_drm/README.md` | DRM shim is a Linux virtgpu UAPI translation layer, not required by a native Unikraft Venus design. |
| Current VirtIO-GPU frontend | `libs/libukvirtio_gpu/README.md` | Existing repo transport library is named `libukvirtio_gpu` and owns capsets, blobs, contexts, SUBMIT_3D, and fake/real backends. |

## Architecture

### Mesa-Aligned Mapping

Mesa-style Linux guest:

```text
Vulkan application
  -> libvulkan loader
  -> Mesa Vulkan runtime
  -> Mesa Venus driver
       - vn_icd.c
       - vn_instance.c
       - vn_device.c
       - vn_queue.c
       - vn_physical_device.c
       - vn_ring.c
       - vn_renderer_virtgpu.c
  -> Linux virtio-gpu DRM kernel driver
  -> QEMU / virglrenderer Venus
  -> host Vulkan driver
```

VOGUE proposed guest:

```text
Vulkan application
  -> libvulkan
       - static loader-like entry point library
       - common Vulkan runtime state needed by the supported subset
       - exported vk* ABI
  -> libukvulkan_venus
       - Unikraft-native Venus Vulkan driver
       - Venus command encode/decode and object translation
       - Venus queue/ring/reply and memory policy
  -> libukvirtio_gpu
       - native VirtIO-GPU transport
  -> QEMU / virglrenderer Venus
  -> host Vulkan driver
```

Optional compatibility path:

```text
libukvulkan_venus
  -> libukvirtgpu_drm
  -> libukvirtio_gpu
```

Use this path only when emulating the Linux DRM virtgpu UAPI is more valuable
than a native Unikraft transport call path.

### Responsibility Boundaries

`libvulkan` owns:

- `vk*` exported symbols.
- `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`.
- Vulkan-Hpp `DispatchLoaderDynamic` storage/init glue.
- Common Vulkan runtime state required by the supported subset:
  instance/device/queue handles, object lifetime tracking, feature/extension
  advertisement, error mapping, allocation-callback handling where supported.
- Dispatch from public Vulkan entry points to the statically linked driver
  implementation.
- A small Unikraft control/diagnostic API in `include/uk/vulkan.h`.

`libvulkan` must not own:

- Venus wire format.
- VirtIO-GPU probing, blobs, context init, or controlq details.
- Linux DRM ioctl compatibility.
- llama.cpp, ggml, GGUF, model loading, benchmark, or server logic.

`libukvulkan_venus` owns:

- Venus as the Vulkan driver implementation over VirtIO-GPU.
- Venus protocol encode/decode, using generated Mesa `venus-protocol` code.
- Venus object translation for instance/device/queue/command-buffer relevant to
  the supported subset.
- Venus query round-trips and reply decoding.
- Venus queue, ring, batching, fence, and submit policy.
- Venus memory policy, including host-visible blob/export/import behavior.
- Backend diagnostics for `libvulkan` to report as `backend_name="venus"`.

`libukvulkan_venus` must not own:

- Public app-facing `vk*` symbol exports.
- Generic Vulkan loader/runtime semantics that are independent of the Venus
  driver.
- llama.cpp or ggml source compilation.

`libukvirtio_gpu` owns:

- VirtIO-GPU device frontend.
- Fake and real backends for tests and QEMU evidence.
- Capsets, contexts, resource/blob create/map/unmap, controlq, SUBMIT_3D, and
  fences.

`libukvirtgpu_drm` owns:

- Linux virtgpu DRM UAPI compatibility only.
- Translation from `DRM_IOCTL_VIRTGPU_*` requests to `libukvirtio_gpu`.

`libukvk_icd` is not a long-term architecture layer. It is a current bootstrap
shim whose name overstates its Khronos/Mesa role. Keep it only as a
compatibility wrapper during migration, then retire or rename it after its
tests are migrated.

## Naming Decisions

| Item | Decision |
|---|---|
| App-facing Vulkan ABI/runtime library | `libs/libvulkan/`, `CONFIG_LIBVULKAN` |
| Venus Vulkan driver implementation | `libs/libukvulkan_venus/`, `CONFIG_LIBUKVULKAN_VENUS` |
| Current Venus encoder code | Move or wrap into `libukvulkan_venus`; do not leave it documented as only an encoder helper |
| VirtIO-GPU frontend | Keep current repo name `libs/libukvirtio_gpu/` for this plan |
| Optional Linux DRM compatibility | Keep `libs/libukvirtgpu_drm/`, but make it optional |
| Current `libukvk_icd` | Compatibility wrapper only; do not absorb into `libvulkan` |
| App-specific ggml build helper | `libs/libukllama_upstream_vk/` or app-local Makefile ownership |

Why not `libukvk_backend`:

- It is too abstract for the current goal.
- There is no second Vulkan backend today.
- If a common backend selector becomes necessary later, it should be introduced
  only after there are two real driver implementations to select between.
- Today the implementation is Venus, so the library should say Venus.

Why not keep `libukvk_icd`:

- In Khronos/Mesa terminology an ICD is the driver interface exposed to a
  loader.
- Mesa `vn_icd.c` is only a thin loader entry.
- VOGUE's current `libukvk_icd` does bootstrap/substrate probing and context
  creation, not loader-facing ICD dispatch.

## Public API Design

### `libvulkan` Headers

Create:

```text
libs/libvulkan/include/vulkan/vulkan.h
libs/libvulkan/include/uk/vulkan.h
```

`include/vulkan/vulkan.h` should forward to the pinned Khronos
`Vulkan-Headers` checkout. `libvulkan` owns exported entry points, not Vulkan
type definitions.

`include/uk/vulkan.h` should expose only Unikraft control and diagnostics:

```c
int uk_vulkan_init(void);
void uk_vulkan_shutdown(void);

struct uk_vulkan_info {
    int initialized;
    const char *driver_name;       /* "venus" */
    const char *claim_boundary;    /* "ggml compute subset" initially */
    uint32_t supported_command_count;
    int batch_enabled;
    int ring_enabled;
    int hostmem_fixed;
};

void uk_vulkan_get_info(struct uk_vulkan_info *out);
```

Upstream Vulkan clients should include normal Vulkan headers and call `vk*`
entry points. They should not need `uk/vulkan.h` unless the Unikraft appliance
chooses explicit boot-time initialization.

### `libukvulkan_venus` Headers

Create:

```text
libs/libukvulkan_venus/include/uk/vulkan_venus.h
```

This header is for `libvulkan`, tests, and tightly scoped internal users. It
should expose driver-facing functions, not public `vk*` ABI:

```c
struct uk_vulkan_venus_dev;

int uk_vulkan_venus_open(struct uk_vulkan_venus_dev *dev, uint32_t gpu_idx);
void uk_vulkan_venus_close(struct uk_vulkan_venus_dev *dev);

const char *uk_vulkan_venus_probe_status(struct uk_vulkan_venus_dev *dev);
```

The exact command-call interface may start as thin wrappers around the existing
`uk_venus_encode_*` functions, but documentation must call this a Venus Vulkan
driver implementation, not only an encoder library.

## Mesa Parity Policy

Use Mesa behavior as the reference for app-visible Vulkan semantics and
Venus-driver behavior only where VOGUE exposes the corresponding subset.

`libvulkan` parity checks:

- `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` lookup rules.
- Instance/device/queue handle lifetime visible to the app.
- Feature/extension advertisement.
- Unsupported-command behavior.
- Dispatch-table organization.

`libukvulkan_venus` parity checks:

- Venus command ordering.
- Queue/ring/reply behavior.
- Physical-device and memory-property round-trips.
- Host-visible memory behavior.
- Buffer/memory requirements and bind behavior.

Every intentional deviation must be documented in the owning library README and
covered by a claim-boundary test.

## Migration Plan

### Phase 0: Rebaseline The Plan

Update documentation first so implementation workers do not follow the old
`libvulkan -> libukvk_icd -> libukvenus` design.

Required edits:

- This plan.
- `libs/libukvenus/README.md` after Phase 1, to explain the transition toward
  `libukvulkan_venus`.
- `libs/libukvk_icd/README.md` after Phase 1, to mark it as compatibility
  bootstrap.

Exit gate:

```text
rg -n -P "^(?!.*rg -n).*(Option B: absorb|CONFIG_LIBVULKAN.*LIBUKVK_ICD|libukvenus = Venus wire protocol encoder)" plan-redesign-vulkan.md
```

Expected: no matches.

### Phase 1: Introduce `libvulkan` And `libukvulkan_venus`

Create `libs/libvulkan/`:

```text
libs/libvulkan/
  Config.uk
  Makefile.uk
  README.md
  exportsyms.uk
  vk_entrypoints.c              # or moved uk_vulkan_dispatch.c initially
  vk_proc_table.c               # may remain combined at first
  vk_hpp_loader.cpp
  uk_stdcxx_compat.cpp          # only if still needed
  include/uk/vulkan.h
  include/vulkan/vulkan.h
```

Create `libs/libukvulkan_venus/`:

```text
libs/libukvulkan_venus/
  Config.uk
  Makefile.uk
  README.md
  exportsyms.uk
  venus_driver.c
  venus_instance.c              # may be folded into venus_driver.c initially
  venus_device.c                # may be folded into venus_driver.c initially
  venus_queue.c                 # may be folded into venus_driver.c initially
  venus_ring.c                  # move/wrap from libukvenus
  venus_query.c
  venus_encode_cs.c             # move/wrap from libukvenus/venus_cs.c
  venus_encode_compute.c        # move/wrap from libukvenus/venus_compute.c
  generated/
  include/uk/vulkan_venus.h
```

`CONFIG_LIBVULKAN` should select `LIBUKVULKAN_VENUS` for the current build, not
`LIBUKVK_ICD` or `LIBUKVIRTGPU_DRM` directly.

`CONFIG_LIBUKVULKAN_VENUS` should select `LIBUKVIRTIO_GPU`. It may optionally
select `LIBUKVIRTGPU_DRM` behind a compatibility option:

```text
config LIBUKVULKAN_VENUS_USE_DRM_COMPAT
    bool "Use Linux virtgpu DRM UAPI compatibility path"
    default n
    select LIBUKVIRTGPU_DRM
```

Exit gate:

```text
make -C tests native
make vulkan-api-coverage
make vulkan-dispatch-test
```

### Phase 2: Move Vulkan ABI Dispatch Out Of `libukggml_vk`

Move the app-facing Vulkan entry points and dispatch state from
`libs/libukggml_vk/uk_vulkan_dispatch.c` to `libs/libvulkan/`.

Rename public control symbols:

| Old | New |
|---|---|
| `uk_ggml_vulkan_dispatch_init` | `uk_vulkan_init` |
| `uk_ggml_vulkan_dispatch_get_info` | `uk_vulkan_get_info` |
| `uk_ggml_vulkan_get_proc_addr_fn` | `uk_vulkan_get_instance_proc_addr_fn` |
| `uk_ggml_vk_loader.cpp` | `vk_hpp_loader.cpp` or `uk_vulkan_hpp_loader.cpp` |

Keep `vk*` symbols unchanged.

Update the old dispatch code so driver-specific work calls
`libukvulkan_venus`, not `libukvk_icd` directly. `libukvk_icd` may remain a
temporary adapter called from `libukvulkan_venus` while preserving current tests.

Exit gate:

```text
make -C tests test-dispatch
make vulkan-dispatch-test
make vulkan-api-coverage
```

### Phase 3: Reframe The Venus Layer

Move or wrap current `libukvenus` sources into `libukvulkan_venus`.

Current sources:

```text
libs/libukvenus/venus_init.c
libs/libukvenus/venus_cs.c
libs/libukvenus/venus_compute.c
libs/libukvenus/vn_ring_shim.c
libs/libukvenus/generated/
```

Target meaning:

- `venus_init.c` becomes Venus driver bootstrap/context/ring setup.
- `venus_cs.c` and `venus_compute.c` remain generated-protocol bridges, but live
  under a driver library name.
- `generated/` remains generated from pinned `venus-protocol`.

Two acceptable migration styles:

1. Direct rename:
   `libs/libukvenus` -> `libs/libukvulkan_venus`.
2. Transitional wrapper:
   keep `libukvenus` as an alias/wrapper for one release while
   `libukvulkan_venus` becomes the owner.

The direct rename is cleaner long term. The wrapper is acceptable only to keep
existing gates stable during a phased migration.

Exit gate:

```text
make -C tests venus-cs
make -C tests native
make lib-readme-check
```

### Phase 4: Retire Or Rename `libukvk_icd`

Do not absorb `libukvk_icd` into `libvulkan`.

Short-term:

- Keep `libukvk_icd` only as a compatibility bootstrap shim.
- Update README language to say it is not a Khronos/Mesa ICD.
- Preserve existing tests as aliases while moving actual Venus bootstrap tests
  to `libukvulkan_venus`.

Long-term:

- Delete `libukvk_icd`, or keep a small compatibility wrapper with no public
  architecture role.
- Move capset/context bootstrap ownership into `libukvulkan_venus`.

Exit gate:

```text
rg -n "libukvk_icd" README.md docs apps libs plan-redesign-vulkan.md
```

Expected: only compatibility/deprecation references remain until the wrapper is
removed.

### Phase 5: Make DRM Compatibility Optional

Audit current uses of `libukvirtgpu_drm`.

If a code path is only emulating Linux/Mesa DRM UAPI, keep it under
`LIBUKVULKAN_VENUS_USE_DRM_COMPAT`.

If a code path is native Unikraft Venus, route directly through
`libukvirtio_gpu`:

```text
uk_virtio_gpu_gl_caps_get
uk_virtio_gpu_gl_context_create
uk_virtio_gpu_gl_context_submit
uk_virtio_gpu_gl_blob_create
uk_virtio_gpu_gl_blob_map
```

Exit gate:

```text
make -C tests native
make venus-check
make current-stage-check
```

### Phase 6: Move ggml Build Ownership Out Of `libukggml_vk`

`libvulkan` and `libukvulkan_venus` must not compile upstream llama.cpp or ggml
runtime code.

Move upstream ggml source compilation to one of:

```text
apps/app-llama-upstream-vk/Makefile.uk
libs/libukllama_upstream_vk/
```

The helper library name is acceptable because it is explicitly app/client build
glue, not a Vulkan driver layer.

Exit gate:

```text
make llama-upstream-vk-build
make governance-check
make app-port-check
make lib-readme-check
```

### Phase 7: Add The Second Vulkan Client Gate

Keep the first API slice equal to the current ggml-vulkan-required command set.
Only add command families for a second client after a concrete same-run gate
exists.

Candidate clients:

- `app-vulkan-smoke`
- `app-vkmark`

Exit gate:

```text
make vulkan-smoke-qemu-check
make vkmark-qemu-check
```

Do not promote graphics/surface/swapchain claims until these produce same-run
artifacts.

## Claim Boundaries

Allowed after `libvulkan` and `libukvulkan_venus` are introduced:

- VOGUE exposes a compute-first Vulkan API subset required by upstream
  `ggml-vulkan.cpp`.
- `libvulkan` owns the application-facing Vulkan ABI/runtime boundary.
- `libukvulkan_venus` owns the Venus Vulkan driver implementation.
- Supported commands route through generated Venus protocol code.
- llama.cpp Vulkan bench/server claims retain their existing same-run evidence
  requirements.

Forbidden:

- Full Vulkan compatibility.
- Vulkan conformance.
- General graphics pipeline support.
- vkmark FPS inside Unikraft without a same-run gate.
- Treating `libukvk_icd` as a real Khronos/Mesa ICD.
- Treating `libukvirtgpu_drm` as mandatory for native Unikraft Venus.
- Moving ggml/GGUF/llama runtime ownership into `libvulkan`.

Preferred wording:

> VOGUE exposes a compute-first Vulkan API subset through `libvulkan` and
> implements the current backend as a Unikraft-native Venus Vulkan driver in
> `libukvulkan_venus`.

Avoid:

> VOGUE implements Vulkan.

Avoid:

> `libukvenus` is only an encoder helper.

Avoid:

> `libukvk_icd` is the Vulkan driver.

## Risks

### Risk: `libvulkan` name overclaims capability

Mitigation: always pair `libvulkan` with "compute-first Vulkan API subset" until
coverage and conformance gates prove more.

### Risk: `libukvulkan_venus` rename churn breaks existing gates

Mitigation: allow `libukvenus` as a transitional wrapper or alias for one release
cycle, but make ownership clear in docs and governance.

### Risk: native path and DRM compatibility path diverge

Mitigation: document `LIBUKVULKAN_VENUS_USE_DRM_COMPAT` as a compatibility
option and keep native `libukvirtio_gpu` transport as the default long-term
path.

### Risk: Mesa parity expands scope too far

Mitigation: only compare and port behavior for the subset VOGUE advertises.
Every Mesa-derived behavior needs a local gate.

## Verification Plan

Minimum local gates:

```text
make -C tests native
make vulkan-api-coverage
make vulkan-dispatch-test
make governance-check
make lib-readme-check
make app-port-check
```

Runtime gates on the evaluation host:

```text
make llama-vulkan-check
make llm-server-vk-throughput-check
make eval-check
make current-stage-check
```

Documentation and source consistency checks:

```text
rg -n -P "^(?!.*rg -n).*(Option B: absorb|CONFIG_LIBVULKAN.*LIBUKVK_ICD|libukvenus = Venus wire protocol encoder)" plan-redesign-vulkan.md
rg -n "libukvulkan_venus|LIBUKVULKAN_VENUS|LIBUKVULKAN_VENUS_USE_DRM_COMPAT" plan-redesign-vulkan.md
```

Expected:

- First command returns no matches.
- Second command returns the new driver library and compatibility option
  references.

## References

- Khronos Vulkan loader guide:
  https://docs.vulkan.org/guide/latest/loader.html
- Khronos checking for Vulkan support:
  https://docs.vulkan.org/guide/latest/checking_for_support.html
- Mesa Venus documentation:
  https://docs.mesa3d.org/drivers/venus.html
- Mesa Vulkan runtime documentation:
  https://docs.mesa3d.org/vulkan/index.html
- Unikraft architecture:
  https://unikraft.org/docs/internals/architecture
- Unikraft build-system internals:
  https://unikraft.org/docs/internals/build-system
- Current VOGUE `libukggml_vk` contract:
  `libs/libukggml_vk/README.md`
- Current VOGUE ICD bootstrap contract:
  `libs/libukvk_icd/README.md`
- Current VOGUE Venus encoder contract:
  `libs/libukvenus/README.md`
- Current VOGUE VirtIO-GPU frontend contract:
  `libs/libukvirtio_gpu/README.md`
- Current VOGUE DRM virtgpu compatibility contract:
  `libs/libukvirtgpu_drm/README.md`
