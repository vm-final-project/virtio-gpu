= Evaluation <sec:eval>

Our evaluation is structured like the two reference unikernel papers. As in Unikraft, we separate base functionality from specialization and report footprint, boot, and application throughput evidence. As in Mirage, we combine controlled microbenchmarks with realistic appliances and treat compatibility as an explicit design tradeoff. We answer four research questions:

- *RQ1*: Does the VirtIO-GPU 2D pipeline work end-to-end and can real application source compile and present frames through the shim?
- *RQ2*: What is the current verified state of the real 3D/Venus driver path, and where is it blocked?
- *RQ3*: Can upstream llama.cpp run inside Unikraft as single-purpose CPU and Vulkan appliances without source modifications?
- *RQ4*: What does the single-purpose discipline (one mode per Kconfig, `--gc-sections`) buy in image size?

== Methodology

All pass/fail rows are evidence-gated. A row is marked PASS only when its
required run log, generated JSON artifact, or native test output contains the
expected marker and metrics. Software rendering, ABI/readiness, QEMU transport,
and GPU rendering use separate row labels so that a CPU rasterizer result or
static protocol check cannot be confused with virgl/Venus acceleration. The
paper distinguishes two hardware contexts: host-native/software-substrate
measurements are taken from the local evaluation environment, while the current
Venus runtime claims are backed by same-run artifacts from the host that
exposes four Tesla V100 GPUs and a Venus-capable QEMU/virglrenderer stack. The
generated artifacts record the exact environment for each claim; rows without
the required runtime substrate remain structured blockers rather than implicit
negative results.

#include "../generated/evidence-current-table.typ"

@fig:evidence-ladder maps the dependency relationships among the current evidence rows across three parallel chains: the 2D display path (disp.2d → gfx.kmscube.sw → gfx.glmark2.sw), the Vulkan/Venus path (vk.drm-shim → vk.icd → vk.smoke → gfx.vkmark → vk.ggml-dispatch), and the LLM chain (host.baseline.vk → bld.uk.vk → llm.bench.cpu → llm.bench.vk → llm.bench.vk.real). On the evaluation host the generated matrix is 27/27 PASS; hosts without the required QEMU/Venus/GPU stack must report structured blockers instead of promoting stale or host-domain artifacts.

`plan-optimize.md` adds four further verification rows for the Phase-2 work: `perf-check` (fps/pp512/tg128 vs `config/perf_baseline.json`; current row reports `gfx.glmark2.sw` +4.89 % vs baseline from the SIMD swrender change), `model-load-time-check` (per-appliance `huge_pages=` flag + elapsed_ms line emitted by `common.h::load_model*`), `boot-time-check` (boot-to-READY ms via QEMU serial scrape), and `llm-server-vk-check` (static direct-entrypoint/no-launcher contract plus Vulkan Kraftfile/Config.uk/server.cpp flags). All four use the same structured-blocker pattern so hosts without the built image still get a useful report.

#include "../figures/evidence-ladder.typ"

*Update (real Venus bring-up and throughput repair).* On the evaluation host with the locally built QEMU 11.0.1 (`virtio-gpu-gl-pci,venus=true`), the Venus-enabled virglrenderer 1.11, and four NVIDIA Tesla V100 GPUs, the previously documented modern PCI blocker is resolved: Unikraft's `libvirtio_pci` now accepts the modern VirtIO device ID `0x1050`, so the appliances boot directly into the real `virtio-gpu-gl` Venus path instead of returning the earlier `BLOCKED` modern-PCI status. With this, the rows that previously read `blocked:probe-incomplete` / `blocked:wrong-domain-artifact` / `blocked:venus-compute-dispatch-incomplete` now PASS against same-run evidence: `xport.qemu-vgpu` and `proto.venus-ring` enumerate the real device and pass a QMP colour-band frame proof (`gfx.kmscube.frame` captures a host GL scanout read-back); `host.vk.probe` reads the real `Tesla V100-SXM2-16GB` device name back over a Venus reply round-trip; and the upstream llama.cpp Vulkan bench appliance (`llm.bench.vk`, `llm.bench.vk.real`) now runs end-to-end on the V100 over Venus at `pp512=2232.1 t/s`, `tg128=160.2 t/s` on the latest artifact after enabling batched Venus submission, with three same-host post-change runs reporting `tg128={135.7, 139.9, 160.2}` and median `139.9`. The server appliance (`llm.server.vk`) loads all 28 layers onto the GPU, reaches model-loaded readiness, and records `batch_size=2048`, `ubatch_size=512`, and `batch_enabled=1` in the READY line. The full evidence matrix is 27/27 PASS; see `docs/VENUS-BRINGUP.md` for the host stack and the guest-side fixes (thread-stack sizing for ggml-vulkan's concurrent pipeline compilation, the host virglrenderer reinstall, the egl-headless GL screendump read-back, and the later batching/dispatch tuning).

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

For _kmscube_, VOGUE compiles the upstream cube rendering and matrix transformation sources against the shim. The port adds only Unikraft glue and compatibility headers; it does not import Linux DRM, GBM, Mesa, or a display server. gfx.kmscube.sw PASS: software-rendered frames are presented through VirtIO-GPU 2D. The Gallium virgl command stream encoder (`virgl_encoder.c`) also has same-run QEMU/Venus evidence on the evaluation host: gfx.kmscube.submit and gfx.kmscube.frame PASS with a virgl CLEAR command stream and colour-band screendump proof.

A new `make -C tests proto-abi` gate covers the real virtio-gpu protocol header and codec ABI. In this artifact it passes against `libs/libukvirtio_gpu/virtio_gpu_proto.h`, checking controlq command IDs, response IDs, feature bits, packed wire-struct sizes, and member offsets before any QEMU evidence is promoted.


The QEMU/Venus transport gate follows the QEMU and Mesa Venus requirements: QEMU's GL backend must expose `virtio-gpu-gl` with blob/hostmem/Venus enabled @qemu-vgpu, and Venus is a VirtIO-GPU protocol for Vulkan command serialization @venus. On the evaluation host, xport.qemu-vgpu, KMSCube submit/frame rows, and Unikraft Vulkan runtime rows now use row-compatible same-run artifacts with matching `evidence_id`, schema fields, and no host-baseline `source`.

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

== RQ3b: Vulkan/vkmark Substrate Evaluation

VOGUE adds Vulkan substrate evaluation as point (6) of the evidence plan. Two Unikraft ports are implemented: `app-vulkan-smoke` (vk.icd gate) and `app-vkmark` (vkmark Vulkan benchmark substrate). A native host-side Vulkan test (`tests/vulkan_compute_test.c`) validates the Vulkan API surface against Khronos Vulkan Samples patterns and measures host baseline latencies. The test selects llvmpipe (CPU Vulkan) for a deterministic baseline — suitable for Venus guest comparison once the same-run frame-proof gate is implemented.

#include "../generated/vulkan-perf-table.typ"

The vk.drm-shim gate (`libukvirtgpu_drm`) is PASS: the Mesa/Linux virtgpu UAPI shim translates `DRM_IOCTL_VIRTGPU_*` calls to VirtIO-GPU protocol via `libukvirtio_gpu`, with a 44-check ioctl replay test covering GETPARAM, CONTEXT_INIT, RESOURCE_CREATE_BLOB, MAP, EXECBUFFER, and WAIT against the Venus-capable (capset id=4) fake backend.

The vk.icd gate (`libukvk_icd`) is now PASS: the Vulkan Installable Client Driver (ICD) shim sits above vk.drm-shim and bootstraps a Venus/Vulkan context. `uk_vulkan_icd_init()` opens the vk.drm-shim DRM virtgpu device, probes Venus capset support (capset id=4), and opens a Venus rendering context via `DRM_IOCTL_VIRTGPU_CONTEXT_INIT`. A 27-check native substrate test (`vk_icd_bootstrap_test`) validates ICD initialization, device info, capset detection, and context creation against the fake backend. The `app-vkmark` port uses vk.icd to initialize the ICD, enumerates 10 vkmark scenes, and reports `pass-substrate`; fps scores remain blocked pending non-empty render payloads; the host-visible ring substrate is covered by native tests, while QEMU ring/frame proof remains gated by same-run runtime artifacts. gfx.vkmark is PASS for the substrate; vk.smoke is PASS for the baseline + vk.drm-shim+vk.icd detection. Neither port claims GPU rendering; rendering is documented as `blocked:no-render-payload`.

`make vulkan-check` runs the Vulkan evaluation script and updates `results/vulkan/vulkan_perf.json`. `make vulkan-tests` builds and runs `vulkan_compute_test` on the host.

== RQ6: llama.cpp Single-Application Appliances

VOGUE hosts upstream `llama.cpp` inside Unikraft as four single-purpose appliances: CPU bench, CPU server, Vulkan bench, and Vulkan server. The Vulkan images route `ggml-vulkan.cpp` through the Venus encoder. Each image boots straight into one entrypoint — no shell, no fork/exec launcher, no auxiliary process — and the unused bench/server source files are excluded at compile time so the linker only links the path that runs. Image-size measurements are in §7.

`llm.bench.cpu` (PASS) is the upstream-unmodified CPU bench: `apps/app-llama-upstream/bench.cpp` plus `kraft/Kraftfile.llama-upstream-bench`. The model is delivered via 9pfs at `/mnt/model/model.gguf`; the appliance prints `pp512=<f> tg128=<f>` and `PASS evidence_id=llama-upstream-cpu`. The current evidence reports `pp512=9.1`, `tg128=7.7` for Qwen3-0.6B-Q4_K_M on the evaluation host.

`llm.server.cpu` (PASS) is the same source tree with the server entrypoint (`server.cpp`), loaded with `Kraftfile.llama-upstream-server`. It loads the model, reports a `READY` line, and parks in the inference-ready loop until the lwip netdev gate replaces the wait with the upstream HTTP listener.

`llm.bench.vk` (PASS) is the Vulkan bench appliance: `apps/app-llama-upstream-vk/bench.cpp` plus `kraft/Kraftfile.llama-upstream-vk`. Vulkan dispatch is wired through `libukggml_vk` (`vk.ggml-dispatch`, PASS), which translates the required Vulkan ABI entry points into Venus `SUBMIT_3D` commands without `dlopen` or a host `libvulkan.so`. On the evaluation host it runs end-to-end through real Venus and, with batched Venus submission enabled, reports `pp512=2232.1`, `tg128=160.2` on the latest same-run artifact.

`llm.server.vk` (PASS for readiness) mirrors the bench-Vulkan appliance with a direct server-mode entrypoint: `apps/app-llama-upstream-vk/server.cpp` plus `kraft/Kraftfile.llama-upstream-vk-server`. It boots directly into the server entrypoint, loads the model through real Venus, and reaches `READY`. This is not an HTTP request/response throughput claim; the lwIP/netdev serving gate remains future work.

`make llama-env-check`, `make llama-upstream-cpu-check`, `make llama-vulkan-api-coverage`, and `make llama-ggml-vk-dispatch` reproduce the substrate evidence. Cross-environment context (baremetal CPU/Vulkan/CUDA, QEMU+Linux baselines, QEMU+Unikraft) is in @tab:multienv.

=== Multi-Environment llama.cpp Comparison

@tab:multienv is retained as host and cross-environment context, not as a blanket Unikraft Vulkan runtime claim. Host Linux CPU/Vulkan/CUDA rows and QEMU-Linux rows describe the external environment against which the current Unikraft Venus run is compared. The generated evidence matrix remains authoritative for Unikraft claims: `llm.bench.vk`, `llm.server.vk`, and `llm.bench.vk.real` are PASS only when same-run Unikraft artifacts exist; host-baseline JSON alone must not satisfy Unikraft runtime rows.

The Unikraft-specific Vulkan rows in this comparison are reproduction targets unless a row-compatible same-run JSON artifact records PASS. In the current generated table, ENV10 is backed by same-run real-Venus JSON and reports PASS, while ENV9 remains blocked because no same-run llvmpipe-targeted Unikraft artifact exists yet.

ENV11 (`vk.ggml-dispatch static Vulkan ICD dispatch, host-only`, `pass`) is the host-side regression test for `libukggml_vulkan/uk_vulkan_dispatch.c`: 164/164 checks pass — proc lookup (80 Vulkan functions via `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`), dispatch init via vk.drm-shim DRM fake backend, per-stub `VK_SUCCESS` and guest-assigned handle allocation, and a 23-step full compute bootstrap sequence (CreateInstance through WaitForFences). All Vulkan struct field byte offsets are verified against the Vulkan 1.3 specification. Venus SUBMIT\_3D encoding fires for every mutating call. The static dispatch layer satisfies `ggml-vulkan.cpp`'s `VULKAN_HPP_DEFAULT_DISPATCHER.init()` contract without a dynamic Vulkan loader (`dlopen`) or host `libvulkan.so`. `make llama-ggml-vk-dispatch` reproduces this result in under 1 second. ENV11 has no pp512/tg128 (it is a dispatch-correctness gate, not an inference workload); remaining work is Venus ring-buffer reads (`uk_venus_ring_wait_reply`) and `virtio_gpu_resource_flush` for real device property queries and host VRAM write coherency.

`make multi-env-bench` regenerates @tab:multienv; `make app-multi-env-bench` regenerates the per-app environment matrix.

#include "../generated/multi-env-bench-table.typ"

=== LLAMA-VK Vulkan/Venus Cross-Environment Throughput

@tab:llama-vk-bench reports `llama-bench` throughput for the same upstream
`llama.cpp` binary set across multiple environments. It is contextual rather
than authoritative for guest claims: the authoritative Unikraft Vulkan claim is
the same-run `llm.bench.vk` / `llm.bench.vk.real` artifact. We retain this
table because it helps interpret the remaining overheads of the guest path and
because it shows that VOGUE's current contribution is not simply "Vulkan
exists," but "Vulkan exists with a bounded and measurable guest-side
dependency chain."

#include "../generated/llama-vulkan-bench-table.typ"

=== Per-App Multi-Environment Evidence Matrix

@tab:app-multi-env extends the comparison to all VOGUE applications (kmscube, glmark2, vkmark, app-vulkan-smoke, app-llama, app-llama-bench) across three Unikraft-specific environments (native substrate, QEMU+Unikraft CPU, QEMU+Unikraft VirtIO-GPU Vulkan) plus host baselines. All rows preserve explicit claim boundaries, including blocked-documented GPU acceleration rows. Each row records a `claim_allowed` and `claim_forbidden` boundary, matching VOGUE's evidence-gate discipline. `make app-multi-env-bench` generates this table from the current artifact state.

#include "../generated/app-multi-env-table.typ"

== RQ5: Current Real-Driver and Venus Stage

The Venus/Vulkan substrate evidence has advanced to runtime PASS on the evaluation host. `make venus-check` records the QEMU probe artifacts and `make stage-check` verifies the real control-queue surface and production Kraft/config/build artifacts. The former modern-PCI discovery blocker is resolved for this host. The guest-side Venus bootstrap command-serialization path (`libukvenus`) provides Mesa-compatible VkCommandTypeEXT framing (`venus_cs.c`) for the initial Vulkan bootstrap commands, capset query, context creation, and `SUBMIT_3D` transport (`venus_init.c`).

A wire-format correctness audit against Mesa's `vn_protocol_driver_instance.h` and virglrenderer's `vn_protocol_renderer_instance.h` revealed two encoding bugs that would prevent virglrenderer from parsing the serialized commands. Both are now fixed: (1) `uk_venus_encode_uint64` previously inserted 4 bytes of alignment padding before each 64-bit field; Venus is a PACKED protocol with no inter-field alignment, so the padding was removed; (2) array presence fields (`ppEnabledLayerNames`, `ppEnabledExtensionNames`, `pQueueCreateInfos`, `pQueuePriorities`, `pPhysicalDevices`) were encoded as uint64 pointer flags, but Mesa's decoder reads them as uint32 `array_size` values via `vn_encode_array_size()` — fixed in `vkCreateInstance`, `vkCreateDevice`, and `vkEnumeratePhysicalDevices`. The native test suite confirms the corrected layout: `venus_cs_test` checks packed encoding and correct field offsets for all bootstrap commands.

A registry checker validates the command constants against local Vulkan-Docs `vk.xml`, Mesa generated Venus protocol headers, and `VK_EXT_command_serialization.xml`. The proto.venus-enc row is PASS.

The Mesa-compatible Venus ring-buffer protocol is now implemented in `libukvenus` (proto.venus-ring row: PASS). `uk_venus_ring_register()` allocates a `HOST3D_GUEST` blob, zero-initializes the shared-memory control fields (`head` at offset 0, `tail` at offset 64, `status` at offset 128), computes `buf_size` as the largest power-of-2 fitting in the remaining blob space, and submits `vkCreateRingMESA` (command type 188) via `SUBMIT_3D`. `uk_venus_ring_cmd_write()` copies commands into the circular buffer with wrap-around; `uk_venus_ring_cmd_flush()` stores the guest tail via a volatile write and sends `vkNotifyRingMESA` (command type 190); `uk_venus_ring_cmd_wait()` spin-polls the host-written head field until commands are consumed. The native test suite verifies 24 ring-protocol invariants (`ring_proto`) and a throughput probe (`ring_perf`); all pass. The encoding of `vkCreateRingMESA` follows Mesa's `vn_protocol_driver_transport.h` exactly: `ring_id` (uint64), `pCreateInfo` pointer flag (uint64=1), `sType=VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA` (1000384000), `pNext=NULL`, `flags=0`, `resourceId`, `offset=0`, `size`, `idleTimeout=0`, and the five layout offsets plus `extraOffset`/`extraSize`.

gfx.kmscube.submit and gfx.kmscube.frame pass on the evaluation host with same-run virgl submission and pixel proof. On other hosts, reviewer-facing PASS claims still require a fresh `make kmscube-check` artifact.

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

The main risk in this project is overclaiming. VOGUE enforces explicit interpretation rules. The complete generated evidence matrix has *27 rows: 27 PASS and 0 blocked* on the evaluation host. Each rule below states what a PASS row allows and what it does not allow.

- gfx.kmscube.sw is not K1: software-rendered frames prove application compatibility and display output, not virgl acceleration.
- A successful capset probe or proto.real-driver pass proves protocol readiness, not rendering.
- xport.qemu-vgpu is PASS on the evaluation host; on other hosts, transport PASS still requires a complete current QEMU GL/Venus probe artifact.
- proto.venus-enc is PASS: the Venus encoder uses PACKED wire format and uint32 `array_size` per Mesa's `vn_encode_array_size()`; command IDs validated against Vulkan-Docs `vk.xml` and Mesa headers.
- proto.venus-ring is PASS for the native ring protocol: `libukvenus` implements the Mesa ring protocol and 24 `ring_proto` native checks pass. QEMU screendump/frame evidence is not promoted unless the same current appliance run also passes the relevant runtime row.
- gfx.kmscube.submit is PASS on the evaluation host; native virgl encoder tests alone are not sufficient on other hosts.
- gfx.kmscube.frame is PASS on the evaluation host; pixel-correct rendering requires the same K1 run plus pixel colour-band proof, not stale artifacts.
- The glmark2 row proves the official-source scene-clear adapter, not the full asset-heavy benchmark suite.
- vk.drm-shim proves the Mesa/Linux virtgpu DRM ioctl surface is correctly translated to VirtIO-GPU protocol; it does not claim Vulkan rendering inside Unikraft.
- gfx.vkmark is PASS for the substrate: vk.icd (`libukvk_icd`) initializes the Vulkan ICD over vk.drm-shim and enumerates 10 benchmark scenes. Scene fps scores require same-run Venus rendering evidence.
- `host.baseline.vk` is PASS as a host Linux/QEMU/Venus baseline only. `host.vk.probe`, `host.bench.vk.run`, and `host.bench.vk` require same-run Unikraft runtime JSON and cannot be satisfied by host-baseline JSON alone.
- The multi-environment comparison (@tab:multienv) is interpreted through the claim matrix, not as a blanket PASS table. ENV11 (vk.ggml-dispatch static dispatch) passes 164/164 checks and has no pp512/tg128 because it is a dispatch-correctness gate. The authoritative Unikraft Vulkan claims come from same-run runtime artifacts: ENV10 is PASS on the evaluation host, while ENV9 remains blocked pending a llvmpipe-specific same-run artifact.

- `bld.uk.vk` is PASS: `vogue-llama-upstream-vk_qemu-x86_64` links ggml-vulkan and Venus libraries under the Unikraft clang toolchain. Build-pass confirms toolchain compatibility; runtime claims still require the runtime rows.
- `llm.bench.cpu` is PASS: upstream llama.cpp CPU path runs inside Unikraft unikernel via 9pfs model delivery with sources unmodified. The current evidence reports `pp512=9.1`, `tg128=7.7`; GPU acceleration is not claimed by this CPU row.
- `llm.bench.vk` and `llm.bench.vk.real` are PASS on the evaluation host with real Venus artifacts. The original low-throughput failure mode was overly granular Venus submission during decode; enabling batched submission and explicit llama.cpp batch sizing raises the latest same-run artifact to `pp512=2232.1`, `tg128=160.2`, with three same-host post-change runs reporting `tg128` median `139.9`. Remaining work is request-level server throughput and model-load latency, not basic decode-path rescue.

These rules mirror the discipline in the reference papers: results are useful only when the baseline, workload, and allowed conclusion are explicit.

== Next-Stage Roadmap (N1 / N2 / vk.ggml-dispatch) <sec:next-stage>

The current 27-row matrix (27 PASS, 0 blocked on the evaluation host) establishes the runtime path; the next work is to keep regenerated summaries aligned, add the missing ENV9 llvmpipe artifact path, preserve the current-stage real-path hygiene gate, add real request-level server throughput evidence, and reduce the remaining model-load cost.

*N1 — Full upstream ggml CPU and Vulkan sources inside Unikraft (PASS).* `apps/app-llama-upstream/` provides the CPU port and `apps/app-llama-upstream-vk/` provides the Vulkan/Venus port, both with upstream sources unmodified and single-purpose Kraftfiles. The current optimization target is Vulkan token generation and server request throughput, not basic runtime enablement.

*N2 — Dual model-delivery transport.* `Config.uk` for `app-llama-cpu` provides a Kconfig choice between initramfs (cpio embedded in image, zero host configuration) and 9pfs (host directory served via `virtio-9p`, unlimited model size). `make llama-cpu-run-initramfs` and `make llama-cpu-run-9pfs` exercise both transports; the acceptance gate requires `pp512`/`tg128` to agree within 5% across transports. Effort: 3–5 days.

*vk.ggml-dispatch — Static Vulkan ICD dispatch layer inside Unikraft* (PASS). `libs/libukggml_vulkan/uk_vulkan_dispatch.c` provides 80+ Vulkan C ABI stubs backed by Venus SUBMIT\_3D encoder calls; `uk_ggml_vulkan_dispatch_init()` initialises over the vk.drm-shim DRM virtgpu substrate with no `dlopen` and no host `libvulkan.so`. `uk_ggml_vk_loader.cpp` wires `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` and calls `VULKAN_HPP_DEFAULT_DISPATCHER.init(uk_vkGetInstanceProcAddr)` so `ggml-vulkan.cpp` can call Vulkan entry points without a dynamic loader. Evidence: `vk.ggml-dispatch` row in @tab:evidence, ENV11 in @tab:multienv (164/164 checks, `make llama-ggml-vk-dispatch`). All stub structs are read at Vulkan 1.3 spec byte offsets, verified against the Khronos Vulkan-Docs (`vk.xml` v1.3.352) and the Mesa `vn_protocol_driver_defines.h` command-type table. Remaining work: Venus ring-buffer reads (`uk_venus_ring_wait_reply`) for real device property queries, `virtio_gpu_resource_flush` for host VRAM write coherency, and a GGUF model transport (9pfs / initramfs) to enable real inference and remove the GFLOP/s extrapolation.
