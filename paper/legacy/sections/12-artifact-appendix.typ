= Artifact Appendix

== Artifact Overview

The VOGUE artifact contains the Unikraft VirtIO-GPU library stack, application ports, build scripts, native test suite, evaluation scripts, paper-generation scripts, and raw result logs used to produce the evidence matrix in this paper. The artifact is deliberately evidence-gated: software-render display results, real-driver ABI/readiness checks, QEMU/Venus transport status, native substrate performance, and future accelerated-rendering claims are separate rows.

== Artifact Claims

Generated performance and stage artifacts are written to `results/` and pulled
into the paper through generated tables. The central rule is unchanged across
all tiers: if the host, build, or runtime environment cannot support a claim,
the artifact records a structured blocker instead of promoting a weaker result.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 4pt,
    align: (left, left, left),
    table.header([Paper claim / row family], [Artifact component], [Validation source]),
    [2D display pipeline], [native 2D render test], [`make native-tests` PASS log],
    [VirtIO-GPU wire ABI], [`make -C tests proto-abi`], [struct/offset/feature checks against `virtio_gpu_proto.h`],
    [Software graphics rows], [application perf scripts + `app-kmscube` / `app-glmark2`], [`make app-perf-check`],
    [VirtIO-GPU real-driver readiness], [real backend static/readiness checks], [`make venus-check`],
    [Venus wire-format + ring protocol], [`venus_cs_test`, registry checks, ring-protocol tests], [`make native-tests`, `make venus-check`],
    [vk.drm-shim / vk.icd substrate], [`virtgpu_drm_ioctl_test`, `vk_icd_bootstrap_test`], [`make native-tests`],
    [Static ggml-Vulkan dispatch], [`ggml_vk_dispatch_test`, API coverage scripts], [`make llama-vulkan-api-coverage`, `make llama-ggml-vk-dispatch`],
    [QEMU/Venus transport], [QEMU probe + runtime artifacts], [`make venus-check`, evaluation matrix rows],
    [Upstream llama.cpp CPU appliance], [`results/llama/upstream_cpu.json`], [`make llama-upstream-cpu-check`],
    [Upstream llama.cpp Vulkan appliance], [`results/llama/upstream_vk.json`, `results/llama/post_opt_runs/`], [`make llama-upstream-vk-check` + same-run runtime logs],
    [Upstream llama.cpp Vulkan server readiness], [`results/llama/upstream_server_vk.json`], [`make llm-server-vk-check`],
    [Current-stage consistency], [`results/stage/current_stage_report.json`], [`make current-stage-check`],
  )),
  caption: [Current artifact claim map. The evaluation host records 27 PASS rows, but new hosts must still generate the corresponding same-run artifacts before promoting transport or runtime claims.]
)

== External Dependency Inventory

All external code referenced by this artifact is listed below with exact local paths and versions. This table allows a reviewer to verify reproducibility and identify what must be installed before running any gate.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto, auto),
    inset: 4pt,
    align: (left, left, left, left),
    table.header([Component], [Local Path], [Version / SHA], [Purpose]),
    [Mesa Venus protocol headers], [`$VENUS_PROTOCOL_ROOT/include/`], [KhronosGroup/venus-protocol], [Venus wire-format type definitions; packed struct layout],
    [Vulkan Headers], [`$VULKAN_HEADERS_INCLUDE`], [Khronos Vulkan-Headers v1.3.352], [Vulkan API headers for `libukggml_vulkan`, `libukvk_icd`, `apps/app-llama-upstream-vk`],
    [SPIR-V Headers], [`$SPIRV_HEADERS_INCLUDE`], [KhronosGroup/SPIRV-Headers (matched to Vulkan 352)], [SPIR-V encoding enums for `ggml-vulkan.cpp` shader table],
    [llama.cpp (upstream)], [`$LLAMA_ROOT` (host build); in-tree at `apps/app-llama-upstream*/`], [SHA `fcae601e4` (ggml-org/llama.cpp)], [CPU baseline benchmarks; upstream-unmodified Unikraft port; ggml-vulkan compute],
    [libvulkan.so], [`/usr/lib/x86_64-linux-gnu/libvulkan.so.1`], [system Vulkan loader], [Host-side Vulkan tests (`make vulkan-tests`)],
    [virglrenderer], [host QEMU dependency], [linked into QEMU 11.0], [Host-side Venus capset decode and SUBMIT_3D dispatch],
    [QEMU], [`qemu-system-x86_64`], [11.0 (required for Venus capset id=4)], [VM execution for all appliance eval targets],
    [KraftKit / `kraft`], [`kraft` in PATH], [v0.9+ recommended], [Unikraft appliance build: `make kmscube-build`, `make llama-vulkan-n3-build`],
    [Unikraft tree], [`../unikraft` (sibling checkout)], [aligned with Kraftfiles in `kraft/`], [Unikraft kernel and library build system],
    [CMake toolchain], [`cmake/unikraft-clang.cmake`], [in-tree], [Forces SPIR-V + Vulkan v352 headers for clang cross-builds],
    [typst], [`typst` in PATH], [v0.11+], [Paper compilation: `make paper`],
  )),
  caption: [External dependency inventory. Paths are referenced through the variables defined in `config/external_paths.json`; the defaults match the evaluation host and can be overridden via the corresponding environment variables. The `cmake/unikraft-clang.cmake` toolchain file consumes the same variables.]
)

=== Venus Protocol Header Inventory

The Venus protocol wire format is defined by Mesa's `vn_protocol_driver_defines.h`. The following tokens from that header are the load-bearing pieces for VOGUE's encoder correctness:

#figure(
  text(size: 8pt, table(
    columns: (auto, auto),
    inset: 4pt,
    align: (left, left),
    table.header([Token / Field], [Significance for VOGUE]),
    [`VN_CS_ENCODER_BUFFER_SIZE`], [Ring command-buffer segment size; must match virglrenderer decoder expectations],
    [`vn_encode_array_size(enc, n)`], [Encodes array presence as `uint32_t n`; zero means absent; VOGUE encoder replicates this protocol in `libukvenus`],
    [`VN_ENCODE_*` PACKED layout], [No inter-field alignment padding; all structs use `__attribute__((packed))` or equivalent; virglrenderer assumes this],
    [`VIRTIO_GPU_F_VIRGL`, `VIRTIO_GPU_F_RESOURCE_BLOB`], [Feature bits negotiated at probe; required before any 3D or blob command is issued],
    [`VIRTIO_GPU_CMD_SUBMIT_3D`, `VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB`, `VIRTIO_GPU_RESP_OK_MAP_INFO`], [Control-queue command IDs; verified by `real_virtio_gpu_path_check.py`],
  )),
  caption: [Venus protocol header tokens verified in the VOGUE artifact. The `real_virtio_gpu_path_check.py --check` gate fails if any token is absent from `libs/libukvirtio_gpu/virtio_gpu_proto.h`.]
)

== Hardware and Software Requirements

VOGUE's evidence rows span three tiers with different hardware requirements. The native-test and software-render tiers require only a Linux host; the Venus/Vulkan tier additionally requires a Vulkan-capable host GPU and QEMU 11.0.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 4pt,
    align: (left, left, left),
    table.header([Component], [Minimum Requirement], [Notes]),
    [Host CPU architecture], [x86_64 (primary); arm64 untested], [KVM optional for native-test tier; recommended for appliance tier],
    [Host OS], [Linux (kernel ≥ 5.4 for VirtIO-GPU 2D; kernel ≥ 5.16 for blob/Venus features)], [Ubuntu 22.04 or later recommended; any distro with QEMU 11.0 available],
    [Host GPU — native and sw-render tiers], [None required], [`make native-tests`, `make app-perf-check`, `make eval-check`, `make paper` run without any GPU],
    [Host GPU — Venus/Vulkan tier], [Any GPU with a Vulkan 1.1 driver on Linux], [Tested drivers per Mesa Venus documentation: ANV ≥ 21.1, RADV ≥ 21.1, NVIDIA proprietary ≥ 570.86, Turnip ≥ 22.0, Lavapipe ≥ 22.1 (software). Requires `VK_KHR_external_memory_fd`. GPU vendor, architecture, and model are not constrained beyond Vulkan 1.1 support.],
    [VMM], [QEMU ≥ 7.0 for 2D VirtIO-GPU; QEMU 11.0 for Venus capset id=4], [Must be built with virglrenderer support (`--enable-virglrenderer`). Venus requires `virtio-gpu-gl-pci,blob=true,venus=true`. The recorded probe used QEMU 11.0.],
    [QEMU KVM flag], [`-accel kvm,honor-guest-pat=on` recommended for Venus], [Per Mesa Venus documentation: `honor-guest-pat=on` required for correct Venus host-visible memory behavior. KVM without this flag may cause coherency issues.],
    [virglrenderer], [Version linked into QEMU build], [Must include Venus support (Vulkan/capset-4 path). No separate minimum version number specified; use the version distributed with your QEMU 11.0 build.],
    [Guest OS], [Unikraft (this repository)], [Unikraft replaces Linux DRM/KMS; guest kernel ≥ 5.16 requirement applies to Linux guests only],
    [Host Vulkan loader], [`libvulkan.so.1` for host-side Vulkan tests], [Used by `make vulkan-tests` and `tests/vk_icd_bootstrap_test`; not required for native-test tier],
    [Build toolchain], [clang or gcc; KraftKit + sibling `../unikraft` tree], [clang/gcc for native tests; KraftKit for appliance builds; typst for paper],
  )),
  caption: [Hardware and software requirements by tier. Source for Venus host GPU requirements: Mesa Venus documentation at #link("https://docs.mesa3d.org/drivers/venus.html"). Any Vulkan 1.1 GPU driver on Linux is sufficient; no specific GPU vendor or model is required.]
)

== Official-Documentation-Grounded Environment Plan

The build environment follows the upstream documentation rather than project-local assumptions. KraftKit's official `kraft build` reference defines target selection by platform/architecture (for example `qemu/x86_64`) @kraft-build, and the Kraftfile reference defines targets as platform/architecture tuples @kraftfile-ref. QEMU documents the relevant graphics backend as `virtio-gpu-gl` with `hostmem`, `blob=true`, and `venus=true` for Venus/Vulkan @qemu-vgpu; Mesa documents Venus as Vulkan command serialization over VirtIO-GPU @venus. Khronos documents that the Vulkan loader maps applications to layers and installable client drivers @vulkan-loader. Typst documents that `compile` produces the paper PDF @typst-pdf.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 4pt,
    align: (left, left, left),
    table.header([Task / problem], [Observed reason], [Fix / verification plan]),
    [Kraft/QEMU appliance build], [Requires a KraftKit target and sibling Unikraft tree; interrupted or missing builds leave runtime rows blocked.], [`kraft build --target qemu/x86_64` through `make kmscube-build`; then `make kmscube-check`.],
    [QEMU/Venus runtime], [QEMU must provide a GL/Venus-capable VirtIO-GPU backend with hostmem/blob/Venus enabled; otherwise QEMU probes/runtime rows stay `blocked:*`.], [Install/choose QEMU with virglrenderer/Venus, use a GL-capable display backend, then rerun `make venus-check` and `make eval-check`.],
    [KMSCube submit/frame proof], [Native encoder tests are not same-run host proof; stale frame artifacts must not pass.], [Require current run-log PASS marker plus pixel colour-band proof; run `make kmscube-check && make eval-check`.],
    [Vulkan/LLM runtime rows], [Host baseline JSON is the wrong domain for Unikraft runtime claims.], [Require row-compatible `evidence_id`, Unikraft run source, and required fields before PASS; same-run Unikraft artifacts now exist on the evaluation host.],
    [Paper build], [Typst and fonts are host tools; missing fonts are warnings if `typst compile` exits zero.], [`make paper-check paper`; treat non-zero Typst exit as failure.],
    [Performance noise], [Native software/substrate timing varies on non-isolated hosts.], [`make perf-check`; best-of-N is the smoke-gate value, JSON persists median/all samples for analysis.],
  )),
  caption: [Environment, problem, root-cause, and fix plan grounded in upstream documentation and current generated artifacts.]
) <tab:env-plan>

== Build and Evaluation Instructions

The full local verification gate runs native tests, application substrate benchmarks, paper compilation, claim audit, and QEMU/Kraft blocker capture.

#figure(
  raw(lang: "bash", block: true,
"# Clone with submodules
git clone <repo> vogue && cd vogue

# Full local verification gate
make verify

# Fast developer gate (no QEMU required)
make test-fast

# Individual gates used by reviewers
make native-tests
make app-perf-check
make eval-check
make paper
make claim-check
make stage-check
make benchmark-check
make venus-check
make paper-check
make kmscube-build
make kmscube-check"),
  caption: [Build and evaluation commands. Run "make verify" for the complete gate; use individual targets to reproduce specific evidence rows.]
)

The native test suite is organized into five focused groups. Reviewers can reproduce individual evidence chains without running the full suite:

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 4pt,
    align: (left, left, left),
    table.header([Target], [Test group], [Evidence rows]),
    [`make test-core`], [VirtIO-GPU core, DMA, shims], [proto.real-driver, gfx.kmscube.sw, disp.2d],
    [`make test-venus`], [VirtIO-GPU 3D, Venus encoder, virgl], [vk.drm-shim, vk.icd, proto.venus-enc, proto.venus-ring],
    [`make test-llm`], [LLM/ggml substrate], [llm.bench.cpu, llm.bench.vk, vk.ggml-dispatch],
    [`make test-dispatch`], [vk.ggml-dispatch static Vulkan dispatch (164 checks)], [vk.ggml-dispatch],
    [`make proto-abi`], [VirtIO wire-ABI struct/offset/feature], [proto.real-driver],
  )),
  caption: [Native test group targets. All run on the host without QEMU, Unikraft, or a GPU. See `tests/README.md` for expected pass strings and evidence mapping.]
) <tab:testgroups>

Expected developer test output includes all host-native substrate tests plus the ABI guard:

#figure(
  raw(block: true,
"make native-tests
dma_buf_test passed alignment=4096 sg=1 pool=2
virtio_gpu_full_api_test passed capsets=5 fences=5 submits_3d=1 blobs=1
virtio_gpu_2d_render_test: PASS frames=3 transfers=3 flushes=3 fences=6
kmscube_compat_test passed mode=1280x800
virtgpu_drm_ioctl_test: all checks passed
vk_icd_bootstrap_test: all checks passed
venus_cs_test: all checks passed
venus_compute_test: all checks PASS
virgl_encoder_test: all checks passed
ukmodel_test: all checks passed
ggml_uk_test: all checks passed
ukllama_test: all checks passed
ggml_vk_dispatch_test: all checks PASS  [164 passed, 0 failed]
make -C tests proto-abi
virtio_gpu_proto_abi_test passed ctrl_hdr=24 display_info=408 edid=1056"),
  caption: [Expected `make native-tests` output. All listed native tests plus the ABI guard pass without QEMU, a GPU, or prebuilt ggml libraries.]
)

Expected application substrate output:

#figure(
  raw(block: true,
"gfx.kmscube.sw: kmscube avg_frame_ms=<measured> fps=<measured> transfers=60 flushes=60 fences=120
gfx.glmark2.sw: glmark2 scene clear avg_frame_ms=<measured> ..."),
  caption: [Expected application substrate benchmark output. Measured values vary by host; the structure and row labels must match.]
)

If the external Kraft/Unikraft appliance build or QEMU/Venus probe fails, the target records a structured blocker rather than promoting K1. A K1 pass requires a same-run log with an accepted VirtIO-GPU device, capset/feature evidence, 3D or Venus command submission, frames ≥ 3, and frame hashes or screenshots tied to the run.

== Troubleshooting

#figure(
  table(
    columns: (auto, auto, auto),
    inset: 5pt,
    align: (left, left, left),
    table.header([Symptom], [Likely Cause], [Fix]),
    [native tests fail], [missing compiler/header path], [check tests/Makefile and production library includes],
    [kmscube-build records blocked build], [external Kraft/Unikraft compiler mismatch], [inspect the build log; fix toolchain before claiming K1],
    [black QEMU display], [scanout or flush not issued], [verify SET_SCANOUT, TRANSFER_TO_HOST_2D, and RESOURCE_FLUSH order],
    [virtio-gpu-gl init fails], [no host OpenGL/virglrenderer, or virglrenderer not linked into QEMU], [use 2D software path or install GL-capable QEMU stack built with `--enable-virglrenderer`],
    [venus probe regresses to `blocked:modern-pci-unsupported`], [Unikraft `libvirtio_pci` patch for modern VirtIO-GPU ID `0x1050` missing or not selected], [apply/reuse the modern virtio-pci support patch before claiming xport.qemu-vgpu; still require K1 frame proof before claiming Venus acceleration],
    [`blocked:egl-not-initialized` or `blocked:no-pass-line` on Venus/Vulkan targets], [missing image, missing KVM/Venus support, or stale evidence from old QEMU EGL launch], [Use a GL-capable QEMU display backend (for example `egl-headless,gl=on`) on a host with a working render node; inspect structured JSON before promoting any claim. These blockers are resolved on the evaluation host.],
    [Host Vulkan 1.1 not available], [Host GPU driver older than listed minimums, or Vulkan loader not installed], [Install a Vulkan 1.1 driver (any of: ANV ≥ 21.1, RADV ≥ 21.1, NVIDIA proprietary ≥ 570.86, Lavapipe ≥ 22.1). `vulkaninfo` confirms capabilties. `VK_KHR_external_memory_fd` must be present.],
  ),
  caption: [Common issues and fixes.]
)

== Artifact Badge Target

We target *Artifacts Available* and *Artifacts Evaluated -- Functional* for the bounded PASS claims in the current generated matrix. The current artifact has 27 evidence rows: 27 PASS and 0 blocked on the evaluation host, including xport.qemu-vgpu, gfx.kmscube.submit/gfx.kmscube.frame, host.vk.probe, host.bench.vk.run, host.bench.vk, llm.bench.vk, llm.server.vk, and llm.bench.vk.real. The artifact still requires fresh row-compatible same-run artifacts before promoting those rows on any different host.

=== Reproduction Notes for the Vulkan Path

The pinned upstream `ggml-vulkan.cpp` API surface is covered by the static
dispatch table and checked by `make llama-vulkan-api-coverage`. Reproducing the
runtime path additionally requires a Venus-capable QEMU stack plus a readable
render node. On the evaluation host, those requirements are satisfied and the
same-run artifacts are already present; on a different host, the same commands
either regenerate PASS artifacts or emit structured blockers.

#figure(
  raw(lang: "bash", block: true,
"make llama-vulkan-api-coverage   # required API surface
make llama-ggml-vk-dispatch      # native static-dispatch substrate
make llama-upstream-vk-build     # build the Vulkan appliance
make llama-upstream-vk-run       # runtime PASS or structured blocker
make llm-server-vk-check         # Vulkan server readiness contract"),
  caption: [Verification commands for the ggml-Vulkan/Venus path. Runtime claims require same-run PASS evidence; blocked rows remain non-claims.]
)
