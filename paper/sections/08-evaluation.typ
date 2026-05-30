= Evaluation <sec:eval>

Our evaluation is structured like the two reference unikernel papers. As in Unikraft, we separate base functionality from specialization and report footprint, boot, and application throughput evidence. As in Mirage, we combine controlled microbenchmarks with realistic appliances and treat compatibility as an explicit design tradeoff. We answer four research questions:

- *RQ1*: Does the VirtIO-GPU 2D pipeline work end-to-end and can real application source compile and present frames through the shim?
- *RQ2*: What is the current verified state of the real 3D/Venus driver path, and where is it blocked?
- *RQ3*: Can upstream llama.cpp run inside Unikraft as single-purpose CPU and Vulkan appliances without source modifications?
- *RQ4*: What does the single-purpose discipline (one mode per Kconfig, `--gc-sections`) buy in image size?

== Methodology

All pass/fail rows are evidence-gated. A row is marked PASS only when its required run log, generated JSON artifact, or native test output contains the expected marker and metrics. Software rendering, ABI/readiness, QEMU transport, and GPU rendering use separate row labels so that a CPU rasterizer result or static protocol check cannot be confused with virgl/Venus acceleration. Unless otherwise noted, measurements were taken on an AMD EPYC 7543P host with KVM available and an NVIDIA RTX 4000 Ada GPU. QEMU/virgl/Venus availability is recorded with the run logs; missing transport or command-stream support is reported as BLOCKED, not as a negative performance result.

#include "../generated/evidence-current-table.typ"

@fig:evidence-ladder maps the dependency relationships among the current evidence rows across three parallel chains: the 2D display path (disp.2d → gfx.kmscube.sw → gfx.glmark2.sw), the Vulkan/Venus path (vk.drm-shim → vk.icd → vk.smoke → gfx.vkmark → vk.ggml-dispatch), and the LLM chain (host.baseline.vk → bld.uk.vk → llm.bench.cpu → llm.bench.vk → llm.bench.vk.real). Blocked rows name their concrete gate: KMSCube lacks a fresh PASS marker, QEMU/Venus transport is `blocked:probe-incomplete`, host Vulkan baseline artifacts are rejected from Unikraft runtime rows as `blocked:wrong-domain-artifact`, and the upstream Vulkan appliance rows remain blocked until same-run guest evidence exists.

`plan-optimize.md` adds four further verification rows for the Phase-2 work: `perf-check` (fps/pp512/tg128 vs `config/perf_baseline.json`; current row reports `gfx.glmark2.sw` +4.89 % vs baseline from the SIMD swrender change), `model-load-time-check` (per-appliance `huge_pages=` flag + elapsed_ms line emitted by `common.h::load_model*`), `boot-time-check` (boot-to-READY ms via QEMU serial scrape), and `llm-server-vk-check` (static direct-entrypoint/no-launcher contract plus Vulkan Kraftfile/Config.uk/server.cpp flags). All four use the same structured-blocker pattern so hosts without the built image still get a useful report.

#include "../figures/evidence-ladder.typ"

== RQ1: 2D Display Pipeline Correctness

The native 2D render test exercises the complete software display pipeline: DMA allocation, scatter-gather construction, two-dimensional resource creation and backing attachment, host transfer, scanout binding, resource flush, and fence waits. The test renders three distinct BGRA frames and verifies that frame hashes are non-zero and differ across rotations.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 3pt,
    align: (left, right, left),
    table.header([Metric], [Value], [Interpretation]),
    [Frames], [3], [Multiple-frame path, not one-shot init.],
    [Transfers], [3], [One transfer per frame.],
    [Flushes], [3], [Each frame reaches scanout.],
    [Fences], [6], [Transfer and flush synchronize each frame.],
  )),
  caption: [Native 2D pipeline counters.]
) <tab:n2d>

== RQ2: Application Source Compatibility

For _kmscube_, VOGUE compiles the upstream cube rendering and matrix transformation sources against the shim. The port adds only Unikraft glue and compatibility headers; it does not import Linux DRM, GBM, Mesa, or a display server. gfx.kmscube.sw PASS: software-rendered frames are presented through VirtIO-GPU 2D. The Gallium virgl command stream encoder (`virgl_encoder.c`) has native coverage, but the current gfx.kmscube.submit/gfx.kmscube.frame appliance rows are `blocked:missing-pass-marker`; stale `frame-proof.json` evidence is removed and pixel-frame PASS now requires a same-run K1 PASS plus a screendump colour-band match.

A new `make -C tests proto-abi` gate covers the real virtio-gpu protocol header and codec ABI. In this artifact it passes against `libs/libukvirtio_gpu/virtio_gpu_proto.h`, checking controlq command IDs, response IDs, feature bits, packed wire-struct sizes, and member offsets before any QEMU evidence is promoted.


The QEMU/Venus transport gate follows the QEMU and Mesa Venus requirements: QEMU's GL backend must expose `virtio-gpu-gl` with blob/hostmem/Venus enabled @qemu-vgpu, and Venus is a VirtIO-GPU protocol for Vulkan command serialization @venus. The current artifact does not promote the stale host-baseline or stale QMP artifacts into Unikraft runtime claims: xport.qemu-vgpu is `blocked:probe-incomplete`, KMSCube submit/frame rows are `blocked:missing-pass-marker`, and `host.vk.probe` / `host.bench.vk.*` are `blocked:wrong-domain-artifact` until a row-compatible Unikraft runtime JSON with matching `evidence_id`, schema fields, and no host-baseline `source` is produced.

== RQ3: Substrate Generality

We exercised an additional application style using an official-source bounded port. The glmark2-es2 scene-clear variant is mapped from the upstream glmark2 repository and uses the EGL path to demonstrate that a benchmark-like GLES application can initialize and present. This row is a substrate proof: it shows that frame presentation is reusable, not that the full benchmark suite or GPU renderer is implemented.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 3pt,
    align: (left, left, left),
    table.header([Workload], [API surface], [Supported claim]),
    [kmscube], [EGL/GLES2/GBM/DRM], [CPU rasterizer + display output.],
    [glmark2 clear], [EGL/GLES2], [Benchmark harness initializes and presents.],
  )),
  caption: [Application coverage with bounded claims.]
) <tab:apps>


== Application Performance

The review critique asked for comprehensive performance evidence for supported applications. @tab:appperf is generated by the application performance evaluation script and measures each supported application path with the native fake VirtIO-GPU backend. This is intentionally a substrate benchmark: it measures software rendering/clearing, framebuffer copy, transfer/flush submission, and fence waits in a deterministic host process. It does not replace the still-needed QEMU/Linux/Mesa baseline or K1 virgl evaluation.

#include "../generated/app-performance-table.typ"

== RQ5b: Vulkan/vkmark Performance Evaluation

VOGUE adds Vulkan substrate evaluation as point (6) of the evidence plan. Two Unikraft ports are implemented: `app-vulkan-smoke` (vk.icd gate) and `app-vkmark` (vkmark Vulkan benchmark substrate). A native host-side Vulkan test (`tests/vulkan_compute_test.c`) validates the Vulkan API surface against Khronos Vulkan Samples patterns and measures host baseline latencies. The test selects llvmpipe (CPU Vulkan) for a deterministic baseline — suitable for Venus guest comparison once the same-run frame-proof gate is implemented.

#include "../generated/vulkan-perf-table.typ"

The vk.drm-shim gate (`libukvirtgpu_drm`) is PASS: the Mesa/Linux virtgpu UAPI shim translates `DRM_IOCTL_VIRTGPU_*` calls to VirtIO-GPU protocol via `libukvirtio_gpu`, with a 44-check ioctl replay test covering GETPARAM, CONTEXT_INIT, RESOURCE_CREATE_BLOB, MAP, EXECBUFFER, and WAIT against the Venus-capable (capset id=4) fake backend.

The vk.icd gate (`libukvk_icd`) is now PASS: the Vulkan Installable Client Driver (ICD) shim sits above vk.drm-shim and bootstraps a Venus/Vulkan context. `uk_vulkan_icd_init()` opens the vk.drm-shim DRM virtgpu device, probes Venus capset support (capset id=4), and opens a Venus rendering context via `DRM_IOCTL_VIRTGPU_CONTEXT_INIT`. A 27-check native substrate test (`vk_icd_bootstrap_test`) validates ICD initialization, device info, capset detection, and context creation against the fake backend. The `app-vkmark` port uses vk.icd to initialize the ICD, enumerates 10 vkmark scenes, and reports `pass-substrate`; fps scores remain blocked pending non-empty render payloads; the host-visible ring substrate is covered by native tests, while QEMU ring/frame proof remains gated by same-run runtime artifacts. gfx.vkmark is PASS for the substrate; vk.smoke is PASS for the baseline + vk.drm-shim+vk.icd detection. Neither port claims GPU rendering; rendering is documented as `blocked:no-render-payload`.

`make vulkan-check` runs the Vulkan evaluation script and updates `results/vulkan/vulkan_perf_latest.json`. `make vulkan-tests` builds and runs `vulkan_compute_test` on the host.

== RQ6: llama.cpp Single-Application Appliances

VOGUE hosts upstream `llama.cpp` inside Unikraft as four single-purpose appliances: CPU bench, CPU server, Vulkan bench, and Vulkan server. The Vulkan images route `ggml-vulkan.cpp` through the Venus encoder. Each image boots straight into one entrypoint — no shell, no fork/exec launcher, no auxiliary process — and the unused bench/server source files are excluded at compile time so the linker only links the path that runs. Image-size measurements are in §7.

`llm.bench.cpu` (PASS) is the upstream-unmodified CPU bench: `apps/app-llama-upstream/bench.cpp` plus `kraft/Kraftfile.llama-upstream-bench`. The model is delivered via 9pfs at `/mnt/model/model.gguf`; the appliance prints `pp512=<f> tg128=<f>` and `PASS evidence_id=llama-upstream-cpu`. Single-thread on AMD EPYC 7543P, this reaches pp512=12,610 t/s, tg128=12,597 t/s with `ggml-cpu` `GGML_USE_CPU=1` and clang's `-mavx2 -mfma -mf16c`.

`llm.server.cpu` (PASS) is the same source tree with the server entrypoint (`server.cpp`), loaded with `Kraftfile.llama-upstream-server`. It loads the model, reports a `READY` line, and parks in the inference-ready loop until the lwip netdev gate replaces the wait with the upstream HTTP listener.

`llm.bench.vk` (BLOCKED) is the Vulkan bench appliance: `apps/app-llama-upstream-vk/bench.cpp` plus `kraft/Kraftfile.llama-upstream-vk`. Vulkan dispatch is wired through `libukggml_vk` (`vk.ggml-dispatch`, PASS) which translates 80+ Vulkan ABI entry points into Venus `SUBMIT_3D` commands without `dlopen` or a host `libvulkan.so`. The image build is PASS (`bld.uk.vk`), but the runtime row is blocked on this host because QEMU `virtio-gpu-gl-pci,venus=true` requires a working EGL render node and only an NVIDIA-only path is available. The row therefore reports `blocked:no-egl-render-node`, not a code defect.

`llm.server.vk` (BLOCKED) mirrors the bench-Vulkan appliance with a direct server-mode entrypoint: `apps/app-llama-upstream-vk/server.cpp` plus `kraft/Kraftfile.llama-upstream-vk-server`. The Venus dispatch chain is identical, so the runtime row inherits the same EGL-render-node gating; the row is intentionally kept distinct so that promoting `llm.bench.vk` (single-shot throughput run) does not silently promote a long-lived server claim, and vice versa.

`make llama-env-check`, `make llama-upstream-cpu-check`, `make llama-vulkan-api-coverage`, and `make llama-ggml-vk-dispatch` reproduce the substrate evidence. Cross-environment context (baremetal CPU/Vulkan/CUDA, QEMU+Linux baselines, QEMU+Unikraft) is in @tab:multienv.

=== Multi-Environment llama.cpp Comparison

@tab:multienv is retained as host and cross-environment context, not as a blanket Unikraft Vulkan runtime claim. Host Linux CPU/Vulkan/CUDA rows and QEMU-Linux rows describe the external environment in which a future Unikraft Venus run should be compared. The current generated evidence matrix remains authoritative for Unikraft claims: `llm.bench.vk`, `llm.server.vk`, and `llm.bench.vk.real` are blocked, and host-baseline JSON must not satisfy Unikraft runtime rows.

The Unikraft-specific Vulkan rows in this comparison are reproduction targets unless a row-compatible same-run JSON artifact records PASS. ENV9/ENV10 historical/prototype data, if present in older generated files, is interpreted as substrate or blocked context only; no pp512/tg128 or GFLOP/s value is claimed for Unikraft Vulkan inference in this artifact. The fix path is the same as the matrix gate: run the Vulkan appliance under a QEMU GL/Venus environment, capture the required `evidence_id` and runtime fields, then rerun `make eval-check` before promoting any row.

ENV11 (`vk.ggml-dispatch static Vulkan ICD dispatch, host-only`, `pass`) is the host-side regression test for `libukggml_vulkan/uk_vulkan_dispatch.c`: 164/164 checks pass — proc lookup (80 Vulkan functions via `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`), dispatch init via vk.drm-shim DRM fake backend, per-stub `VK_SUCCESS` and guest-assigned handle allocation, and a 23-step full compute bootstrap sequence (CreateInstance through WaitForFences). All Vulkan struct field byte offsets are verified against the Vulkan 1.3 specification. Venus SUBMIT\_3D encoding fires for every mutating call. The static dispatch layer satisfies `ggml-vulkan.cpp`'s `VULKAN_HPP_DEFAULT_DISPATCHER.init()` contract without a dynamic Vulkan loader (`dlopen`) or host `libvulkan.so`. `make llama-ggml-vk-dispatch` reproduces this result in under 1 second. ENV11 has no pp512/tg128 (it is a dispatch-correctness gate, not an inference workload); remaining work is Venus ring-buffer reads (`uk_venus_ring_wait_reply`) and `virtio_gpu_resource_flush` for real device property queries and host VRAM write coherency.

`make multi-env-bench` regenerates @tab:multienv; `make app-multi-env-bench` regenerates the per-app environment matrix.

#include "../generated/multi-env-bench-table.typ"

=== LLAMA-VK Vulkan/Venus Cross-Environment Throughput

@tab:llama-vk-bench reports `llama-bench` throughput for the same upstream
`llama.cpp` binary set used by `apps/app-llama-vulkan` across four
environments on the same host hardware (AMD EPYC 7543P, NVIDIA RTX 4000 Ada).
The Vulkan/Venus stack runs end-to-end and emits real token-rate samples;
the Unikraft-internal hosting of the same binary is tracked separately
under `llm.bench.vk`. `make llama-vulkan-bench` regenerates this table.

#include "../generated/llama-vulkan-bench-table.typ"

=== Per-App Multi-Environment Evidence Matrix

@tab:app-multi-env extends the comparison to all VOGUE applications (kmscube, glmark2, vkmark, app-vulkan-smoke, app-llama, app-llama-bench) across three Unikraft-specific environments (native substrate, QEMU+Unikraft CPU, QEMU+Unikraft VirtIO-GPU Vulkan) plus host baselines. All rows preserve explicit claim boundaries, including blocked-documented GPU acceleration rows. Each row records a `claim_allowed` and `claim_forbidden` boundary, matching VOGUE's evidence-gate discipline. `make app-multi-env-bench` generates this table from the current artifact state.

#include "../generated/app-multi-env-table.typ"

== RQ5: Current Real-Driver and Venus Stage

The Venus/Vulkan substrate evidence has advanced significantly, but the current generated matrix keeps real QEMU/Venus transport as `blocked:probe-incomplete`. `make venus-check` records the current probe artifact and `make stage-check` verifies the real control-queue surface and production Kraft/config/build artifacts. The former modern-PCI discovery blocker has substrate fixes, but transport PASS now requires a complete current QEMU GL/Venus run. The guest-side Venus bootstrap command-serialization path (`libukvenus`) is implemented: it provides Mesa-compatible VkCommandTypeEXT framing (`venus_cs.c`) for the initial Vulkan bootstrap commands, capset query, context creation, and `SUBMIT_3D` transport (`venus_init.c`).

A wire-format correctness audit against Mesa's `vn_protocol_driver_instance.h` and virglrenderer's `vn_protocol_renderer_instance.h` revealed two encoding bugs that would prevent virglrenderer from parsing the serialized commands. Both are now fixed: (1) `uk_venus_encode_uint64` previously inserted 4 bytes of alignment padding before each 64-bit field; Venus is a PACKED protocol with no inter-field alignment, so the padding was removed; (2) array presence fields (`ppEnabledLayerNames`, `ppEnabledExtensionNames`, `pQueueCreateInfos`, `pQueuePriorities`, `pPhysicalDevices`) were encoded as uint64 pointer flags, but Mesa's decoder reads them as uint32 `array_size` values via `vn_encode_array_size()` — fixed in `vkCreateInstance`, `vkCreateDevice`, and `vkEnumeratePhysicalDevices`. The native test suite confirms the corrected layout: `venus_cs_test` checks packed encoding and correct field offsets for all bootstrap commands.

A registry checker validates the command constants against local Vulkan-Docs `vk.xml`, Mesa generated Venus protocol headers, and `VK_EXT_command_serialization.xml`. The proto.venus-enc row is PASS.

The Mesa-compatible Venus ring-buffer protocol is now implemented in `libukvenus` (proto.venus-ring row: PASS). `uk_venus_ring_register()` allocates a `HOST3D_GUEST` blob, zero-initializes the shared-memory control fields (`head` at offset 0, `tail` at offset 64, `status` at offset 128), computes `buf_size` as the largest power-of-2 fitting in the remaining blob space, and submits `vkCreateRingMESA` (command type 188) via `SUBMIT_3D`. `uk_venus_ring_cmd_write()` copies commands into the circular buffer with wrap-around; `uk_venus_ring_cmd_flush()` stores the guest tail via a volatile write and sends `vkNotifyRingMESA` (command type 190); `uk_venus_ring_cmd_wait()` spin-polls the host-written head field until commands are consumed. The native test suite verifies 24 ring-protocol invariants (`ring_proto`) and a throughput probe (`ring_perf`); all pass. The encoding of `vkCreateRingMESA` follows Mesa's `vn_protocol_driver_transport.h` exactly: `ring_id` (uint64), `pCreateInfo` pointer flag (uint64=1), `sType=VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA` (1000384000), `pNext=NULL`, `flags=0`, `resourceId`, `offset=0`, `size`, `idleTimeout=0`, and the five layout offsets plus `extraOffset`/`extraSize`.

gfx.kmscube.submit and gfx.kmscube.frame are blocked in the current generated matrix because the appliance evidence is stale or unavailable. The virgl encoder remains native-tested substrate code, but reviewer-facing PASS claims require a fresh `make kmscube-check` artifact.

#include "../generated/venus-stage-table.typ"

The reviewer-facing current-stage gate combines the design-alignment, real-path, stage-audit, benchmark-summary, evaluation-matrix, paper, and documentation checks into one generated report. @tab:current-stage is not a new performance result; it is a completeness check that the artifact, paper, and claim taxonomy agree before any accelerated rows are promoted.

#include "../generated/current-stage-table.typ"

== RQ4: Footprint and Startup

@tab:footprint compares the measured VOGUE unikernel images with a minimal Linux kernel reference. The comparison follows the Unikraft paper's methodology: report concrete image sizes and make the baseline explicit. We do not claim that image size alone proves performance; it quantifies the deployment cost of carrying the graphics substrate.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 3pt,
    align: (left, right, left),
    table.header([Configuration], [Size], [Notes]),
    [VOGUE 2D image], [292 KB], [Graphics-substrate image.],
    [VOGUE virgl probe], [336 KB], [GL-capable path.],
    [Linux kernel], [9.2 MB], [No initramfs.],
    [Linux + initramfs], [10.5 MB], [Reference VM.],
  )),
  caption: [Image-size comparison.]
) <tab:footprint>

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 3pt,
    align: (left, right, left),
    table.header([Configuration], [Boot], [Scope]),
    [VOGUE 2D], [10--11 ms], [entry to app-ready, 5 runs.],
    [VOGUE virgl probe], [251--252 ms], [host virgl init included.],
    [Linux + initramfs], [857--861 ms], [entry to init-ready.],
  )),
  caption: [Startup comparison.]
) <tab:boot>

== Claim Boundaries <sec:claim-boundaries>

The main risk in this project is overclaiming. VOGUE enforces explicit interpretation rules. The complete generated evidence matrix has *27 rows: 18 PASS and 9 blocked-documented*. Each rule below states what a PASS row allows and what it does not allow.

- gfx.kmscube.sw is not K1: software-rendered frames prove application compatibility and display output, not virgl acceleration.
- A successful capset probe or proto.real-driver pass proves protocol readiness, not rendering.
- xport.qemu-vgpu is currently `blocked:probe-incomplete`: modern VirtIO-PCI support and native protocol readiness are useful substrate work, but transport PASS requires a complete current QEMU GL/Venus probe artifact.
- proto.venus-enc is PASS: the Venus encoder uses PACKED wire format and uint32 `array_size` per Mesa's `vn_encode_array_size()`; command IDs validated against Vulkan-Docs `vk.xml` and Mesa headers.
- proto.venus-ring is PASS for the native ring protocol: `libukvenus` implements the Mesa ring protocol and 24 `ring_proto` native checks pass. QEMU screendump/frame evidence is not promoted unless the same current appliance run also passes the relevant runtime row.
- gfx.kmscube.submit is currently `blocked:missing-pass-marker`; native virgl encoder tests pass, but submit PASS requires a fresh same-run appliance PASS marker with `renderer=virgl` and nonzero `submits_3d`.
- gfx.kmscube.frame is currently `blocked:missing-pass-marker`; pixel-correct rendering requires the same K1 run plus pixel colour-band proof, not stale artifacts.
- The glmark2 row proves the official-source scene-clear adapter, not the full asset-heavy benchmark suite.
- vk.drm-shim proves the Mesa/Linux virtgpu DRM ioctl surface is correctly translated to VirtIO-GPU protocol; it does not claim Vulkan rendering inside Unikraft.
- gfx.vkmark is PASS for the substrate: vk.icd (`libukvk_icd`) initializes the Vulkan ICD over vk.drm-shim and enumerates 10 benchmark scenes. Scene fps scores require same-run Venus rendering evidence.
- `host.baseline.vk` is PASS as a host Linux/QEMU/Venus baseline only. `host.vk.probe`, `host.bench.vk.run`, and `host.bench.vk` are currently `blocked:wrong-domain-artifact` when a host-baseline JSON is offered for a Unikraft runtime row. Host token-rate samples are useful environment context, but they do not prove Unikraft runtime or throughput. See @tab:llama-vk-bench for baseline context only.
- The multi-environment comparison (@tab:multienv) is interpreted through the claim matrix, not as a blanket PASS table. ENV11 (vk.ggml-dispatch static dispatch) passes 164/164 checks and has no pp512/tg128 because it is a dispatch-correctness gate. ENV10 and upstream Vulkan guest-runtime rows remain `blocked:*` locally until rerun on a host with QEMU/Venus EGL support. Shared benchmark parameters live in `config/bench_env.yaml`, so reruns use one model path, GFLOP/token value, QEMU vCPU count, boot timeout, and vk.ggml-dispatch expected-check setting.

- `bld.uk.vk` is PASS: `vogue-llama-vk_qemu-x86_64` links ggml-vulkan and Venus libraries under the Unikraft clang toolchain. Build-pass confirms toolchain compatibility; no runtime execution or GPU throughput is claimed.
- `llm.bench.cpu` is PASS: upstream llama.cpp CPU path runs inside Unikraft unikernel via 9pfs model delivery with sources at SHA `fcae601e4` unmodified. pp512=12,610 t/s, tg128=12,597 t/s measured on AMD EPYC 7543P, single-thread. This is CPU-only path; GPU acceleration not claimed.
- `llm.bench.vk` is BLOCKED (`blocked:no-pass-line`): the upstream llama.cpp Vulkan path on Unikraft is blocked because the host EGL initialization fails — only `/dev/dri/renderD128` (NVIDIA) is available and the NVIDIA kernel module is broken on this kernel. No throughput claim. Resolved when a working EGL render node is available.
- `llm.bench.vk.real` is BLOCKED (`blocked:egl-not-initialized`): QEMU `egl-headless` cannot initialize EGL on this host (NVIDIA-only render node, broken driver). `__EGL_VENDOR_LIBRARY_FILENAMES` vendor override and `LIBGL_ALWAYS_SOFTWARE=1` do not bypass the GBM/DRM render-node selection. Resolved when a lavapipe-accessible render node is available or when QEMU supports purely software EGL without a DRM node.

These rules mirror the discipline in the reference papers: results are useful only when the baseline, workload, and allowed conclusion are explicit.

== Next-Stage Roadmap (N1 / N2 / vk.ggml-dispatch) <sec:next-stage>

The current 27-row matrix (18 PASS, 9 blocked-documented) establishes the substrate; the next work blocks promote blocked entries to run-proven measurements and unblock the real Venus GPU path.

*N1 — Full upstream ggml CPU sources inside Unikraft (PASS for CPU path).* `apps/app-llama-upstream/` provides the Unikraft port for upstream llama.cpp/ggml with `lib-pthread-embedded`, `lib-libcxx`, `lib-musl`, and 9pfs model transport. The CPU path (`llm.bench.cpu`) is PASS: pp512=12,610 t/s, tg128=12,597 t/s on AMD EPYC 7543P single-thread, upstream sources unmodified (SHA `fcae601e4`). `apps/app-llama-upstream-vk/` adds the Vulkan path (`kraft/Kraftfile.llama-upstream-vk`) — build succeeds, runtime blocked at EGL (`llm.bench.vk: blocked:no-pass-line`). Unblocked when a working EGL render node is available. Effort to unblock: replace NVIDIA render node with lavapipe-accessible device.

*N2 — Dual model-delivery transport.* `Config.uk` for `app-llama-cpu` provides a Kconfig choice between initramfs (cpio embedded in image, zero host configuration) and 9pfs (host directory served via `virtio-9p`, unlimited model size). `make llama-cpu-run-initramfs` and `make llama-cpu-run-9pfs` exercise both transports; the acceptance gate requires `pp512`/`tg128` to agree within 5% across transports. Effort: 3–5 days.

*vk.ggml-dispatch — Static Vulkan ICD dispatch layer inside Unikraft* (PASS). `libs/libukggml_vulkan/uk_vulkan_dispatch.c` provides 80+ Vulkan C ABI stubs backed by Venus SUBMIT\_3D encoder calls; `uk_ggml_vulkan_dispatch_init()` initialises over the vk.drm-shim DRM virtgpu substrate with no `dlopen` and no host `libvulkan.so`. `uk_ggml_vk_loader.cpp` wires `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` and calls `VULKAN_HPP_DEFAULT_DISPATCHER.init(uk_vkGetInstanceProcAddr)` so `ggml-vulkan.cpp` can call Vulkan entry points without a dynamic loader. Evidence: `vk.ggml-dispatch` row in @tab:evidence, ENV11 in @tab:multienv (164/164 checks, `make llama-ggml-vk-dispatch`). All stub structs are read at Vulkan 1.3 spec byte offsets, verified against the Khronos Vulkan-Docs (`vk.xml` v1.3.352) and the Mesa `vn_protocol_driver_defines.h` command-type table. Remaining work: Venus ring-buffer reads (`uk_venus_ring_wait_reply`) for real device property queries, `virtio_gpu_resource_flush` for host VRAM write coherency, and a GGUF model transport (9pfs / initramfs) to enable real inference and remove the GFLOP/s extrapolation.
