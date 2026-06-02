= Discussion and Limitations <sec:discussion>

== Main Insight

The central lesson is that graphics support for a unikernel is not an all-or-nothing Linux-porting problem. A useful substrate can be decomposed into four separable planes: (1) a device plane that speaks VirtIO-GPU correctly, (2) an API plane that preserves source compatibility for selected applications, (3) a rendering plane that can begin with software pixels and later switch to virgl or Venus, and (4) a transport-readiness plane that tests real 3D/blob commands without pretending that they are application acceleration. This decomposition is what lets VOGUE produce meaningful evidence before a full GPU renderer exists.

== What the Current Paper Proves

VOGUE proves that a small Unikraft library stack can present frames through VirtIO-GPU, support real graphics application source at the compatibility boundary, and implement the real VirtIO-GPU 3D/blob control-queue surface without importing Linux DRM or Mesa. It also proves that the project can distinguish among working display output, protocol readiness, transport blockers, and hardware acceleration. This distinction is not merely defensive writing; it is an engineering mechanism. By forcing every row to name its allowed and forbidden claim, the evaluator guides implementation priorities.

== What This Does Not Yet Support

*K1 virgl `SUBMIT_3D` delivery and pixel-correct frame evidence now have current PASS claims on the evaluation host.* A K1 submit claim still requires the current K1 row itself to pass with `renderer=virgl` and `submits_3d>0`; a frame claim additionally requires same-run pixel proof with `colour_band_source="pixel"` and a matched colour index. Log-only SUBMIT_3D diagnostics and stale frame proofs remain blockers on hosts without the same-run artifact.

*Full accelerated benchmark coverage* is missing. The current glmark2 row is a scene-clear substrate proof, not a full GL benchmark. A SOSP-strength version should report at least one real visual workload with frame hashes or screenshots, one synthetic microbenchmark for transfer and fence overhead, and one Linux/QEMU baseline under the same host conditions.

*Security and isolation analysis* is incomplete. The current work reduces guest code size, but it does not yet quantify attack surface reduction, fuzz the command parser, or evaluate malicious-device behavior.

== Why This Is Still Valuable

The staged evidence ladder — disp.2d → gfx.kmscube.sw → gfx.glmark2.sw → xport.gl-probe → K1 — is a reusable method for unikernel device work. It avoids the common systems-paper failure mode of presenting an ambitious final architecture without enough intermediate evidence to tell which component actually works. The method also clarifies what should be done next: keep the same-run KMSCube proof reproducible, broaden to accelerated visual workloads, and optimize the now-running Venus/Vulkan compute path.

== Performance Implications

The software-render path performs two expensive operations per frame: CPU rasterization and a framebuffer copy into DMA memory. At 1280×800 BGRA, the copy touches roughly 4 MB per frame; at 60 FPS this is about 240 MB/s before virtqueue overhead. This is plausible for simple appliances but not a substitute for GPU rendering. Virgl/Venus should remove the full-frame CPU copy for accelerated workloads by moving geometry, command buffers, or Vulkan resources to the host renderer, but they will introduce command-encoding, serialization, host-visible memory, and renderer-latency overheads. The right future evaluation is therefore not "VOGUE faster than Linux" in the abstract, but a breakdown of boot, memory, frame latency, and CPU utilization across: VOGUE software render, VOGUE virgl, Linux DRM/Mesa, and a minimal Linux appliance.

== Threats to Validity

*External validity.* The kmscube, scene-clear, and null-renderer workloads are intentionally small. They show feasibility, not coverage of complex GUI or game workloads.

*Measurement validity.* Current footprint and boot comparisons are useful, but a submission-quality evaluation should pin CPU frequency, repeat runs, report variance, and publish raw logs.

*Implementation validity.* The fake backend is excellent for command-order tests and the static gate is useful for ABI coverage, but neither substitutes for QEMU/virgl/Venus behavior. All claims that depend on host-device behavior must be backed by same-run QEMU logs and frame evidence.


== Remaining Work for SOSP-Level Competitiveness

@tab:sosp-gaps summarizes the reviewer-driven work that cannot honestly be claimed by the current artifact. VOGUE now fixes the paper-structure, figure, and native application-performance weaknesses, but the table keeps the larger implementation gaps visible.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 3pt,
    align: (left, left, left),
    table.header([Pri.], [Missing work], [Evidence required / status]),
    [P0], [Broader accelerated app benchmark], [K1 same-run submit + pixel-frame proof passes on the eval host; broaden to vkmark/full scenes.],
    [P0], [Linux baseline], [matched kmscube/glmark2 runs; not collected.],
    [P0], [QEMU accelerated app bench], [boot, first-frame, steady frame, CPU/GPU; transport passes on eval host, broader app metrics still missing.],
    [P1], [API coverage], [real/stateful/safe-stub table; partial.],
    [P1], [visual artifacts], [screenshots or frame hashes; native CRC only.],
    [P2], [robustness tests], [malformed resource/fence/capset cases; future.],
  )),
  caption: [Reviewer-driven remaining work before full SOSP-level graphics acceleration.]
) <tab:sosp-gaps>

== Root Cause Analysis of Blocked Rows

=== K1: current PASS and remaining scope

K1 is a current PASS claim on the evaluation host. The root causes that previously blocked it, and the guardrails that remain, are:

1. *Native encoder readiness is separate from QEMU runtime proof.* `virgl_encoder.c` in `libukvirtio_gpu` encodes CREATE_OBJECT/SURFACE, SET_FRAMEBUFFER_STATE, and CLEAR command streams, and the native `virgl_encoder_test` passes. This proves command construction under the fake backend, not host virglrenderer acceptance.
2. *Current run log must carry the required PASS marker.* `kmscube_vgpu_gl_eval.py` removes stale `frame-proof.json` data when it is not present in the current `run.log`. Submit PASS requires the current K1 row to carry `renderer=virgl` and `submits_3d>0`.
3. *Pixel proof is stricter than submit proof.* `frame_pixel_proof.json` may record SUBMIT_3D diagnostics, but `gfx.kmscube.frame` only passes when the screendump itself matches an encoded CLEAR colour band and the proof hashes the same `run.log`.

The reproduction plan is to rebuild the KMSCube appliance with KraftKit, run it under QEMU's `virtio-gpu-gl`/Venus-capable backend as documented by QEMU @qemu-vgpu, capture a same-run serial PASS marker, capture the QMP screendump, and rerun `make kmscube-check && make eval-check`. Without that run on a new host, native virgl encoder results remain substrate evidence only.

=== llama.cpp full inference: current PASS and remaining scope

Full upstream llama.cpp CPU and Vulkan inference now pass inside single-purpose Unikraft appliances on the evaluation host. The earlier blockers were:

1. *C++ runtime missing.* The real `llama.cpp` and `ggml` source files are C++17. Unikraft's default build uses `nolibc`; adding a C++ runtime requires pulling `libc-musl` (for a full POSIX libc) plus `libcxx`/`libcxxabi` (LLVM libc++) from the Unikraft external library catalog. Without these, `std::vector`, `std::string`, and exception-handling infrastructure are absent.
2. *POSIX threads missing.* `ggml`'s CPU backend uses a thread pool (`ggml_threadpool_t`) backed by `pthread_create`/`pthread_join`. Unikraft's core scheduler provides cooperative fibers (`uksched`), but the POSIX thread API (`pthread_*`) is not automatically available; it requires enabling `lib-pthread-embedded` (catalog: `libs/pthread-embedded`) or Unikraft's `posix_thread` library.
3. *Model file unavailable.* Running inference requires loading a GGUF model weight file. Inside a Unikraft VM the filesystem is absent by default; options are (a) 9pfs host-share via `lib-9pfs` + `posix-vfs`, (b) ramfs with the model embedded in the initramfs image, or (c) a virtio-blk block device.

GPU acceleration is delegated to upstream `ggml-vulkan` built with `-DGGML_USE_VULKAN=1` and dispatched through Mesa Venus. VOGUE does not implement or claim a custom guest-side compute-remoting ABI. The required static Vulkan API surface is routed through `libukggml_vulkan`/`libukvenus`, covered by the 164-check vk.ggml-dispatch test, and now backed by same-run QEMU/Venus llama.cpp runtime evidence. The remaining gap is performance: prefill is strong, while token generation and HTTP serving need optimization and new request-level gates.

== Full llama.cpp/ggml Unikraft Porting Plan (N1 / N2 / vk.ggml-dispatch)

The current substrate (legacy-llama-substrate + legacy-llama-substrate) proves every pre-condition for full inference. Advancing to real token-generation inside Unikraft follows three blocks.

*N1 — Full upstream ggml CPU sources compiled for Unikraft (2–3 weeks):*

`apps/app-llama-cpu/` (scaffold on branch) provides the port skeleton: `Config.uk` selects the full C++ catalog stack (`LIBMUSL`, `LIBCXX`, `LIBCXXABI`, `LIBUNWIND`, `LIBCOMPILER_RT`, `LIBPTHREAD_EMBEDDED`) and inherits the proven ABI from legacy-llama-substrate. `Makefile.uk` follows the APG `addlib_s` pattern, pinning the same upstream llama.cpp commit used for host baselines. Upstream sources are added one-by-one per the N1.D iterative patch loop; expected patch families cover `mmap` flag masks (`MAP_POPULATE`/`MAP_HUGETLB`), `/proc/cpuinfo` thread detection → `CONFIG_UKPLAT_LCPU_MAXCOUNT`, `dlopen` backend registry → compile-time register, and `<chrono>` steady_clock → `ukplat_monotonic_clock()`. `scripts/llama_cpu_runtime_eval.py` captures `pp512`/`tg128` from the guest serial log and writes `results/llama/llama_cpu_run_latest.json`; `legacy-llama-substrate` promotes from "libukllama scalar substrate" to "upstream ggml real model inference."

*N2 — Dual model-delivery transport (3–5 days):*

`Config.uk` for `app-llama-cpu` already provides a Kconfig choice between initramfs (cpio embedded in image) and 9pfs (host directory via `virtio-9p`). Both transports mount `/models/` at the same guest path; application code is identical. `make llama-cpu-run-initramfs` and `make llama-cpu-run-9pfs` exercise both; the acceptance gate requires `pp512`/`tg128` to agree within 5%.

*vk.ggml-dispatch — Full `ggml-vulkan.cpp` linked inside Unikraft (1–3 months):*

`apps/app-llama-vk/` extends N1's source set with `ggml/src/ggml-vulkan/ggml-vulkan.cpp` and the embedded SPIR-V table (C.1 path). `libukvk_icd` is extended from ICD bootstrap to the full Vulkan compute-dispatch surface (Groups 1–7: device+queue, memory, buffers, descriptors, pipelines, command encoding, submission+sync); the `libukvenus` encoders already proven by legacy-llama-substrate cover the wire side. Patch families cover `dlopen` removal (static ICD), validation-layer bypass, and host-coherent flush. Once vk.ggml-dispatch lands, `legacy-llama-substrate` promotes to run-proven with real GPU `pp512`/`tg128`; ENV10 drops the Venus-proxy qualifier.

The dependency graph is: N2 (model I/O) → N1 (CPU ggml in UK, `legacy-llama-substrate`) → vk.ggml-dispatch (Vulkan/Venus, `legacy-llama-substrate` upgrade). N2 is independent of K1; vk.ggml-dispatch shares the `libukvk_icd` runtime extension with G7 (`vkmark`) but lives on its own evidence ladder.

== Future Work

The current artifact has 27 generated evidence rows: 27 PASS and 0 blocked on the evaluation host. The N1/N2/vk.ggml-dispatch milestones described above are now complete for runtime bring-up:

*Completed (this stage):*
- N1 CPU path: `apps/app-llama-upstream` with upstream-unmodified llama.cpp running inside Unikraft. llm.bench.cpu PASS: pp512=9.1 t/s, tg128=7.7 t/s.
- N2 model delivery: 9pfs host-share via VirtIO-9P (`CONFIG_LIBUK9P=y`, `CONFIG_LIB9PFS=y`, `CONFIG_LIBVFSCORE_AUTOMOUNT_CI_RAMFS=y`). Model mounted at `/mnt/model`.
- vk.ggml-dispatch static dispatch and runtime: `apps/app-llama-upstream-vk` with `libukggml_vulkan` builds, dispatch tests pass, and the Vulkan bench/server appliances have same-run Venus artifacts.
- POSIX surface served entirely by upstream `lib-musl` external library on top of Unikraft's posix-mmap/time/fdio/vfs libs — no project-local POSIX shim is required.

*Next stage:*
1. *HTTP server evidence.* `llm.server.vk` now reaches READY with `batch_size=2048`, `ubatch_size=512`, and prompt caching enabled, but there is still no lwIP/netdev-backed request path. Add request serving before reporting TTFT, requests/s, or aggregate server throughput.
2. *Model-load optimization.* `model-load-time-check` still records `use_mmap=false`, `huge_pages=false`; the remaining 4.7-7.1 s model-load path should be reduced before claiming startup wins for larger models.
3. *Linux/QEMU baseline.* Collect matched kmscube/glmark2 runs on Linux+Mesa under the same host for a direct performance comparison.
4. *Full glmark2 scene coverage and frame hashes.* Replace the current scene-clear substrate proof with at least one real visual workload, frame hashes, and a Linux Mesa baseline.
5. *Security audit.* Quantify attack surface reduction, fuzz the command parser, and evaluate malicious-device behavior.
