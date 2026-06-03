= Implementation <sec:impl>

== Code Organization

VOGUE is organized as reusable Unikraft micro-libraries plus application harnesses. The active production tree deliberately excludes earlier non-graphics experiments, which are archived separately.

#figure(
  table(
    columns: (auto, auto, auto),
    inset: 5pt,
    align: (left, right, left),
    table.header([Module], [LoC], [Role]),
    [libukvirtio_gpu], [~1650], [VirtIO-GPU 2D/3D frontend, blob controlq, Gallium virgl encoder (`virgl_encoder.c`), and fake backend],
    [libukdrm_compat], [~120], [DRM struct compatibility shims],
    [libukgbm_compat], [~80], [GBM compatibility shims],
    [libukswrender], [264], [CPU software rasterizer],
    [libukegl], [778], [EGL/GLES2/GBM/DRM shim],
    [libukvirtgpu_drm], [~310], [vk.drm-shim: Mesa/Linux virtgpu UAPI shim (DRM ioctl → VirtIO-GPU protocol)],
    [libukvk_icd], [~110], [vk.icd: Vulkan ICD shim (ICD bootstrap over vk.drm-shim for Venus context)],
    [apps/app-kmscube], [~250], [kmscube harness and upstream source glue],
    [apps/app-glmark2], [~190], [official-source glmark2 scene-clear adapter],
    [apps/app-vkmark], [~80], [vkmark vk.drm-shim+vk.icd substrate: ICD init + 10 scenes + host baselines],
    [apps/app-llama-upstream], [~80], [llm.bench.cpu + llm.server.cpu: upstream llama.cpp CPU appliance, sources unmodified (SHA `fcae601e4`); single-purpose bench/server modes; no fork/exec launcher; 9pfs model delivery],
    [apps/app-llama-upstream-vk], [~100], [llm.bench.vk: upstream llama.cpp Vulkan path on Unikraft; uses `libukggml_vulkan` for Venus dispatch; build PASS, runtime blocked at host EGL],
    [libukggml\_vulkan], [~1200], [vk.ggml-dispatch static Vulkan ICD dispatch: 80+ stubs wired to Venus SUBMIT\_3D encoder; no dlopen; 164/164 checks pass],
  ),
  caption: [Production module breakdown. Counts are approximate and exclude archived legacy work.]
) <tab:loc>

== Key Engineering Decisions

=== VirtIO-GPU Instead of Passthrough

GPU passthrough requires IOMMU configuration, vendor firmware, and a full guest driver. VirtIO-GPU gives the unikernel a para-virtual device model with a small command vocabulary and broad VMM support. The cost is that VOGUE must implement the guest graphics protocol and, for an accelerated claim, either a virgl command stream or a Mesa/Venus-compatible Vulkan render-payload path.

=== Software Rasterizer Before VirGL

A CPU rasterizer is not the final performance story, but it is the right first correctness oracle. It gives the project deterministic pixels, native tests, and a way to validate DMA, scanout, and fences before debugging host GL state. This mirrors the staged methodology used in strong systems papers: isolate one layer, make it measurable, then add the next source of complexity.

=== Compatibility Shim Instead of Linux DRM

The shim strategy avoids importing Linux subsystems that would dominate the codebase. Each function is classified as real, stateful shim, or safe stub. Real functions allocate surfaces and submit frames; stateful shims track GLES2 objects; safe stubs satisfy application control flow without claiming kernel DRM semantics.

== Native Fake Backend

The fake VirtIO-GPU backend is the main testability mechanism. It records resource lifecycle events, validates command order, increments transfer/flush/fence counters, and resolves fences deterministically. This permits the native test suite to catch regressions without QEMU, KVM, or host GPU access. The fake backend is intentionally not a performance model; it is a protocol and state-machine oracle.

For the real driver, we added separate ABI and static-readiness gates rather than weakening the fake-backend tests. The `proto-abi` test target includes the project `virtio_gpu_proto.h` header and checks control header, display-info, resource, backing, scanout, transfer, flush, capset, EDID, command, response, fence, blob, UUID, map/unmap, and feature-bit wire contracts. The real-driver static gate verifies that the production backend exposes context create/destroy, context attach/detach, `SUBMIT_3D`, blob creation, UUID assignment, map/unmap, and teardown commands. These gates count as proto.real-driver/vk.readiness readiness, not as QEMU/Venus rendering evidence.

The `proto-abi` gate now also checks the `VIRTIO_GPU_F_BLOB_ALIGNMENT` feature bit (bit 5, value `5u`) and the full five-field `virtio_gpu_config` wire struct including the `blob_alignment` field at offset 16 (20 bytes total). The real driver negotiates `F_BLOB_ALIGNMENT` when the host offers it (requires `F_RESOURCE_BLOB`), reads the `blob_alignment` value from device config space, and applies alignment to blob sizes issued in `RESOURCE_CREATE_BLOB` and to MAP_BLOB offset accounting. If the value is zero or not a power-of-two the driver falls back to page (4096-byte) alignment, preserving correctness on hosts that offer the feature bit but return an invalid config value. This closes the two gaps identified by the porting-surface audit (`script-llama-ggml/audit-output/llama_vulkan_unikraft_full_audit.md`): the missing feature-bit definition and the truncated device config struct.

== Native Test Suite

The active native suite contains ten production tests:

- The DMA buffer test validates DMA alignment, scatter-gather output, and buffer-pool behavior.
- The VirtIO-GPU test validates capset, context, blob, submit, and fence API behavior on the fake backend while the static gate verifies the real-backend controlq entry points.
- The kmscube compatibility test validates the DRM/GBM compatibility surface used by the kmscube port.
- The 2D render test runs the complete three-frame display path.
- The vk.drm-shim ioctl replay test (`virtgpu_drm_ioctl_test`) exercises 44 checks of the `libukvirtgpu_drm` shim: GETPARAM, CONTEXT_INIT, RESOURCE_CREATE_BLOB, MAP, EXECBUFFER, WAIT, the ioctl dispatcher, and teardown.
- The vk.icd ICD substrate test (`vk_icd_bootstrap_test`) exercises 27 checks of `libukvk_icd`: ICD initialization, device info retrieval, Venus capset detection, context creation via vk.drm-shim, and teardown.
- The Venus ring substrate test (`venus_cs_test`) exercises host-visible `HOST3D_GUEST` blob allocation, mapping, context attach/detach, staged ring writes, `SUBMIT_3D`, overflow handling, metrics counters, teardown, and a native copy-throughput probe. It validates wire-format correctness (PACKED encoding, uint32 array_size per Mesa `vn_encode_array_size()`) and the full Mesa ring-buffer protocol: `ring_proto` (24 checks) verifies `vkCreateRingMESA` encoding, head/tail/status layout at the correct offsets (0/64/128/192), power-of-2 `buf_size` computation, circular write with wrap-around, store-release tail flush, `vkNotifyRingMESA` submission, spin-poll wait, `vkDestroyRingMESA` unregister, and double-unregister safety. `ring_perf` probes circular-write throughput. All checks pass. @fig:venus-ring illustrates the ring buffer header layout and command sequence.

#include "../figures/venus-ring-protocol.typ"
- The `virgl_encoder_test` exercises 47 checks of the Gallium virgl command stream encoder (`virgl_encoder.c`): encoder init/overflow/NULL guards, SURFACE CREATE_OBJECT wire format (6-word stream), SET_FRAMEBUFFER_STATE wire format (4-word stream), CLEAR wire format (9-word stream with float/double reinterpret), chained encoding (19 words = 76 bytes), overflow guard, and a full SURFACE+FB+CLEAR pipeline submitted to the fake VirtIO-GPU backend with `submits_3d` increment verification.
- The `ggml_vk_dispatch_test` exercises 164 checks of the static Vulkan ICD dispatch layer (`libukggml_vk/uk_vulkan_dispatch.c`): proc lookup, dispatch init via vk.drm-shim, per-stub VK_SUCCESS, and a 23-step compute bootstrap from `vkCreateInstance` through `vkWaitForFences`.

These tests are the default precondition for evaluation and paper generation.


== Current-Stage Implementation Boundary

The current code should be read as five layers of evidence. First, the 2D/software-render layer is executable under native tests and application harnesses. Second, the 3D/Venus transport layer is implemented as real control-queue code and checked by ABI/static/readiness scripts. Third, QEMU/Venus transport is evidence-gated and now passes on the evaluation host with row-compatible probe artifacts. Fourth, the Mesa-compatible Venus ring-buffer protocol is implemented in `libukvenus` and has native plus QEMU evidence. Fifth, the virgl Gallium command stream encoder (`virgl_encoder.c`) is implemented in `libukvirtio_gpu` and passes native and same-run KMSCube submit/frame checks on the evaluation host.

== Upstream-Unmodified Path

A key correctness requirement is that upstream `llama.cpp` and `ggml` sources run inside Unikraft *without patches*. Two application ports implement this:

More broadly, the application layer follows the taxonomy in
`docs/ARCHITECTURE.md`: each workload keeps its upstream application semantics,
while VOGUE contributes only the bounded adapter, build glue, and the smallest
guest-side compatibility surface needed to reach the VirtIO-GPU / Venus seam.
The current application set is summarized in @tab:app-taxonomy. The LoC counts
below are the current tracked adapter/application-directory totals (entrypoints,
small glue files, `Config.uk`, `Makefile.uk`, `exportsyms.uk`), excluding
sibling upstream repositories such as `../llama.cpp`.

#figure(
  table(
    columns: (auto, auto, auto, 1.6fr),
    inset: 4pt,
    align: (left, left, right, left),
    table.header([Application], [Upstream repo], [Local LoC], [VOGUE delta / bounded difference]),
    [`app-kmscube`], [`kmscube`], [617], [Vendor a small upstream subset plus `uk_glue.c`; replace Linux DRM/GBM/EGL launch plumbing with one Unikraft entrypoint and bounded compatibility hooks. The port proves software-render and frame/submission evidence without importing Mesa or Linux DRM/KMS.],
    [`app-glmark2`], [`glmark2`], [185], [Keep only the scene-clear workload shape from the upstream benchmark; omit the larger C++ scene suite, asset loaders, and host window-system backends. The result is a deterministic substrate benchmark, not a full upstream benchmark rebuild.],
    [`app-vkmark`], [`vkmark`], [86], [Use a tiny substrate harness instead of reproducing the upstream meson build and full scene runtime. The port proves ICD/bootstrap and Venus-capset readiness and documents the 10 target scenes plus host baselines, but does not yet claim in-guest scene FPS.],
    [`app-llama-upstream`], [`llama.cpp`], [473], [Compile upstream `ggml`/`llama.cpp` CPU sources unmodified into bench-only or server-only images; add only single-purpose entrypoints, 9pfs/ramfs model-loading glue, and one-line include shims for architecture-specific files. No shell and no `fork()`/`exec()`.],
    [`app-llama-upstream-vk`], [`llama.cpp`], [668], [Extend the CPU port with `GGML_USE_VULKAN=1`, static Vulkan dispatch through `libukggml_vk`, and the Venus chain `libukvk_icd` → `libukvenus` → `libukvirtgpu_drm` → `libukvirtio_gpu`. Server mode calls upstream `llama_server()` directly over lwIP rather than launching a second process.],
  ),
  caption: [Current application adapters. LoC counts are exact for the tracked application directories in this revision and exclude sibling upstream repositories.]
) <tab:app-taxonomy>

This table makes the implementation boundary concrete. VOGUE is not a fresh set
of applications with graphics-inspired names; it is a collection of small
adapters around existing upstream projects. The engineering work lies in the
guest-side compatibility and transport libraries (`libukegl`,
`libukvirtgpu_drm`, `libukvk_icd`, `libukvenus`, `libukggml_vk`) rather than in
forking application logic into project-local substitutes.

`apps/app-llama-upstream/` provides the CPU path. It compiles the upstream ggml
CPU backend, backend registry, and llama.cpp `src/*.cpp` sources directly into
the Unikraft image — no prebuilt archives, no project-local fork of the
application sources. The CPU appliance uses ramfs plus 9pfs for model delivery,
and the current evidence records `llm.bench.cpu` as PASS with `pp512=9.1` and
`tg128=7.7` for the staged Qwen3-0.6B-Q4_K_M model on the evaluation host.

`apps/app-llama-upstream-vk/` extends the CPU port with the Vulkan backend. It
uses `libukggml_vulkan` for the ggml core and Vulkan SPIR-V shader blobs, then
compiles the ggml CPU backend and llama.cpp sources in-tree with
`GGML_USE_VULKAN=1`. The static API surface is checked by
`llama-vulkan-api-coverage`; runtime is validated separately by same-run QEMU
artifacts. On the evaluation host, the Vulkan appliance no longer represents a
blocked bring-up path: it reaches the real Venus runtime and, after batching
tuning, records `pp512=2232.1` and `tg128=160.2` in the latest artifact.

The x86-specific CPU feature files (`x86-quants.c`, `x86-repack.cpp`, `x86-cpu-feats.cpp`) and shim files (`ggml-cpu-cpp.cpp`, `dummy-amx.cpp`, `models-llama.cpp`) are shared between the two apps via relative paths from `apps/app-llama-upstream/`. Each shim is a one-line `#include` resolved through the project-relative `-I$(LLAMA_ROOT)/...` paths supplied by `Makefile.uk`, so the tracked sources contain no personal-checkout paths. `LLAMA_ROOT` and `VULKAN_HEADERS_INCLUDE` default to the values recorded in `config/external_paths.json` and may be overridden via environment variables. The cmake toolchain file (`cmake/unikraft-clang.cmake`) consumes the same variables, so the upstream cmake build and the Unikraft in-tree build see the same header set.

== Single-Purpose Image Minimization

Unikraft's defining property is that the unikernel image carries exactly the
code its single workload runs. VOGUE makes that property structural for the
llama.cpp ports rather than relying on the linker alone.

`apps/app-llama-upstream/` ships two entrypoint sources — `bench.cpp` and
`server.cpp` — sharing a small `common.h` for the 9pfs mount and model load.
The Makefile selects exactly one entrypoint at configure time:

```
APPLLAMA_UPSTREAM_SRCS-$(CONFIG_APP_LLAMA_UPSTREAM_MODE_BENCH)  += $(APPLLAMA_UPSTREAM_BASE)/bench.cpp
APPLLAMA_UPSTREAM_SRCS-$(CONFIG_APP_LLAMA_UPSTREAM_MODE_SERVER) += $(APPLLAMA_UPSTREAM_BASE)/server.cpp
```

The unused mode is never compiled. The application is built with
`-Os -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables` and
linked with `-Wl,--gc-sections`, so every function or data symbol reachable
neither from `main()` nor from required constructors is discarded from the
final image. The same flags apply to all upstream `llama.cpp` and `ggml*`
translation units, so the bench image carries bench code only.

`make image-size-check` writes `results/image-size/report.{json,md}`. When an
image has not been built locally the row is a structured
`blocked:image-missing`, never a fatal error, so the reviewer-only fast path
stays usable.

== Implementation Difference from Linux

The code organization mirrors the dependency-collapse argument from
@sec:system-overview. Linux guest graphics distributes responsibility across a
kernel driver, ioctl-heavy userspace libraries, and Mesa drivers. VOGUE moves
only the minimum necessary responsibilities into guest-local Unikraft
libraries: transport and resource management in `libukvirtio_gpu`, bounded
application compatibility in `libukegl`, and Vulkan/Venus protocol logic in
`libukvirtgpu_drm`, `libukvk_icd`, `libukvenus`, and `libukggml_vulkan`. This
split is the reason the project can support real workloads while keeping the
guest dependency graph small enough to inspect and explain.

== Performance Optimisation

The companion file `plan-optimize.md` records the project's optimisation
plan as a four-layer table (Unikraft core, ggml/llama.cpp inference, Venus
dispatch, QEMU/host) where every lever cites a public reference — Unikraft
EuroSys 2021 @unikraft-eurosys, llama.cpp upstream issues and the Vulkan
backend documentation, Mesa Venus docs, QEMU virtio-gpu docs, and the lwIP
tuning wiki. Each layer entry pairs the source citation with the exact tree
artifact that implements it and the verification gate that proves it.

The implementation already exercises five plan entries: a hot/cold compile
flag split (`<FILE>_FLAGS-y` in both `app-llama-upstream*` Makefile.uk),
SIMD-friendly inner loops in `libukswrender`, a coalesced
`TRANSFER_TO_HOST_2D + RESOURCE_FLUSH` API in `libukvirtio_gpu` (one fence
per frame rather than two), an env-gated SUBMIT_3D batcher in
`libukggml_vk/uk_vulkan_dispatch.c` (collapses recorded command-buffer ops
into one Venus submission), and Venus encoder scalar fast paths in
`libukvenus/venus_cs.c`. The Phase-2 entries surface continuous batching and
prefix-cache knobs in the `llm.server.cpu` and `llm.server.vk` Config.uk
files (`*_PARALLEL`, `*_PROMPT_CACHE`), pin the QEMU
`virtio-gpu-gl,hostmem=8G,blob=true,venus=true` flags in the Vulkan-server
Kraftfile, and add a model-load-latency line that the new
`model-load-time-check` gate consumes.

Three new evaluation gates verify the plan: `make perf-check` compares fps,
pp512 and tg128 against `config/perf_baseline.json` with a 3 % regression
threshold; `make boot-time-check` records boot-to-READY ms per appliance;
`make llm-server-vk-check` asserts the static Phase-2 contract (Kraftfile
device flags, run.sh egl-headless, Config.uk slots/prompt-cache, server.cpp
dispatch-info call). All three follow the same structured-blocker pattern
as `image-size-check`: missing artifacts produce a `blocked:*` row rather
than a fatal error, so a reviewer running on a host without QEMU or a built
image still gets a useful report.

== libukvenus Generation

`libukvenus` is the guest-side Venus encoder. Mesa's `venus-protocol`
generator already walks `vk.xml` plus `VK_MESA_venus_protocol.xml` with Mako
templates; vendoring its 3,000+ lines and 30+ templates would duplicate
upstream maintenance every time the Venus wire format moves. Instead
`scripts/gen_libukvenus.py` delegates to the sibling `../venus-protocol`
checkout and writes the result into `libs/libukvenus/generated/`, which
`make clean` removes. Only the extensions used by upstream `ggml-vulkan.cpp`
are sliced, keeping the encoder small. Detailed workflow:
`libs/libukvenus/GENERATOR.md`.

== Kraftfile Integration

The production appliance is declared in the root build configuration shown in @fig:kraftfile. All VOGUE libraries are local-path dependencies. The build target is intentionally simple: a reviewer can inspect the selected libraries and confirm that the appliance does not depend on Linux DRM, Mesa, or archived legacy components.

The additional official-source port is built as a standalone appliance because a Unikraft image selects one application `main()`. `kraft/Kraftfile.glmark2` selects `CONFIG_APP_GLMARK2` with the EGL/software-render stack. This follows Unikraft's porting model: each app directory carries `Config.uk`, `Makefile.uk`, and `exportsyms.uk`; each `Makefile.uk` registers the app with `addlib`, uses namespaced `APP_*` source variables, and records the upstream repository reference used by the adapter.

#figure(
  raw(lang: "yaml", block: true,
"spec: v0.6
name: vogue
unikraft:
  source: ../unikraft
  kconfig:
    CONFIG_APP_KMSCUBE: 'y'
    CONFIG_LIBUKSGLIST: 'y'
    CONFIG_LIBUKVIRTIO_GPU: 'y'
    CONFIG_LIBUKSWRENDER: 'y'
    CONFIG_LIBUKEGL: 'y'
    CONFIG_LIBUKDRM_COMPAT: 'y'
    CONFIG_LIBUKGBM_COMPAT: 'y'"),
  caption: [VOGUE Kraftfile configuration excerpt. Each CONFIG entry selects one VOGUE micro-library; no Linux DRM or Mesa libraries are included.]
) <fig:kraftfile>
