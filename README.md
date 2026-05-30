# VOGUE: VirtIO-GPU on Unikraft for Graphics and llama.cpp

VOGUE is a research artifact for running graphics and Vulkan-oriented workloads
inside Unikraft unikernels without importing Linux DRM/KMS or Mesa into the
guest. The codebase keeps local code small: reusable Unikraft libraries own the
VirtIO-GPU/Venus/Vulkan glue, while upstream applications keep their own
application logic.

The llama.cpp ports follow Unikraft's *one image, one purpose* principle. There
is one image for CPU `llama-bench`, one image for the CPU server-mode
entrypoint, one image for the Vulkan/Venus bench, and one image for the
Vulkan/Venus server-mode entrypoint. Each image boots straight into a single
entrypoint, has no shell, no native `fork()`/`exec()` launcher, and links only
the source files the selected mode needs (see
`apps/app-llama-upstream*/{bench,server}.cpp` — the unused mode is not
compiled). Build flags `-Os -ffunction-sections -fdata-sections` plus
`-Wl,--gc-sections` let the linker drop every unreached symbol from the final
unikernel.

For llama.cpp, this revision deliberately removes the old synthetic local GGUF,
ggml, and llama substrate libraries. The supported design is now:

```text
upstream llama.cpp bench/server
  -> apps/app-llama-upstream or apps/app-llama-upstream-vk
  -> optional libs/libukggml_vk static Vulkan/Venus dispatch for ggml-vulkan
  -> libukvenus -> libukvirtgpu_drm -> libukvirtio_gpu
  -> QEMU virtio-gpu-gl-pci,blob=true,venus=true -> host Vulkan driver
```

A `PASS` row means the corresponding command reproduced locally. A `blocked:*`
row (for example `blocked:unikraft-image-missing`, `blocked:image-missing`) is a documented blocker and is not acceleration or throughput evidence.

## Repository structure

| Path | Role |
|---|---|
| `libs/` | Project-local Unikraft libraries: DMA/buffer pool, VirtIO-GPU frontend, DRM/GBM/EGL shims, Venus encoder/ring protocol, software renderer, and `libukggml_vk` static ggml-vulkan dispatch. Upstream `lib-musl` covers math and POSIX surface; no local math/POSIX shims live in this tree. |
| `apps/` | Unikraft apps: kmscube, glmark2, Vulkan smoke/vkmark, and true upstream llama.cpp CPU/Vulkan single-app appliances. |
| `kraft/` | Single-purpose appliance Kraftfiles, including llama CPU/Vulkan bench-only and server-only images. |
| `config/` | Governance metadata and environment matrices. `config/llama_env_matrix.json` owns llama.cpp environment/thread/backend selection. |
| `tests/` | Host-native deterministic tests against fake VirtIO-GPU; no QEMU/GPU/model dependency. |
| `scripts/` | Evidence generators and claim gates. |
| `results/` | Latest/generated evidence consumed by README and paper. |
| `docs/` | Architecture, governance, and porting plan docs. |
| `paper/` | Typst paper and generated tables. |

Reproducibility manifests live in sibling `../manifest/`; do not duplicate
manifest locks or experiment runners in this repo.

## VirtIO-GPU Venus/Vulkan v1 roadmap

The bounded graphics roadmap is tracked in
`design/unikraft-virtio-gpu-spec-v1.md` and
`design/virtio-gpu-vulken-v1.md`. Out of scope for this revision: importing
Linux DRM/KMS or Mesa wholesale, claiming full Venus/Vulkan rendering without
same-run frame proof, or treating structured blockers as passes.

## Official-doc-grounded environment and fix plan

This artifact now treats environment setup, blockers, and fixes as explicit
claim gates. The plan is grounded in the upstream documentation for
[KraftKit `kraft build`](https://unikraft.org/docs/cli/reference/kraft/build),
the [Kraftfile target model](https://unikraft.org/docs/cli/reference/kraftfile/latest),
[QEMU virtio-gpu](https://www.qemu.org/docs/master/system/devices/virtio-gpu.html),
[Mesa Venus](https://docs.mesa3d.org/drivers/venus.html), the
[Vulkan loader](https://docs.vulkan.org/guide/latest/loader.html), and
[Typst PDF builds](https://typst.app/docs/reference/pdf/).

| Task / problem | Current reason | Fix / verification plan |
|---|---|---|
| Build the Unikraft/QEMU appliances | KraftKit builds must select a platform/architecture target such as `qemu/x86_64`; this tree also expects the sibling Unikraft checkout and external roots declared in `config/external_paths.json`. | Run the project wrappers such as `make kmscube-build`; for direct diagnosis use `kraft build --target qemu/x86_64` with the matching Kraftfile, then capture `make kmscube-check` evidence. |
| Enable QEMU/Venus transport | QEMU's accelerated virtio-gpu path needs the GL backend plus host blob memory and Venus enabled; otherwise `xport.qemu-vgpu` remains `blocked:probe-incomplete`. | Use a QEMU/virglrenderer stack that accepts `virtio-gpu-gl,hostmem=...,blob=true,venus=true`, then rerun `make venus-check` and `make eval-check`. |
| Prove KMSCube submit/frame rows | Native virgl encoder tests are substrate evidence only; the current KMSCube appliance rows are `blocked:missing-pass-marker` and stale frame proofs are removed. | Rebuild and run KMSCube under the QEMU GL/Venus environment, require a same-run serial `PASS` marker plus pixel colour-band proof, then rerun `make kmscube-check && make eval-check`. |
| Prove Vulkan/LLM runtime rows | Host Linux baseline artifacts are the wrong provenance domain for Unikraft runtime claims, so `host.vk.probe` and `host.bench.vk.*` stay `blocked:wrong-domain-artifact`. | Produce row-compatible Unikraft runtime JSON with the required `evidence_id`, source domain, and schema fields before any PASS promotion. |
| Build the paper | Typst is the release PDF tool; font warnings are acceptable only when the compile exits zero. | Run `make paper-check paper` and keep generated tables synchronized with `results/vogue_latest_evaluation_matrix.json`. |
| Run performance gates | Software-render timings vary on shared hosts, so a single noisy sample can be misleading. | Run `make perf-check`; `scripts/app_perf_eval.py` uses best-of-N for smoke gating and persists median/all samples for reviewer analysis. |


## Apps

| App | Status | Sources | Evidence rows |
|---|---|---|---|
| `app-kmscube` | canonical | `main.c`, `uk_glue.c`, vendored `upstream/cube-smooth.c` | `gfx.kmscube.sw/.submit/.frame` |
| `app-glmark2` | benchmark | `main.c` | `gfx.glmark2.sw` |
| `app-llama-upstream` | canonical | `bench.cpp` *or* `server.cpp` + `common.h`; six 1-line `#include` shims for upstream ggml/llama sources | `llm.bench.cpu`, `llm.server.cpu` |
| `app-llama-upstream-vk` | canonical | `bench.cpp` *or* `server.cpp` + `common.h`; shares the six upstream shims with `app-llama-upstream` | `llm.bench.vk`, `llm.server.vk` |
| `app-vulkan-smoke` | demo | `main.c` | `vk.smoke` |
| `app-vkmark` | experimental | `main.c` | `gfx.vkmark` |

External source roots (`LLAMA_ROOT`, `VENUS_PROTOCOL_ROOT`, `VK_INC`, `VK_LIB`,
`VULKAN_HEADERS_INCLUDE`, `SPIRV_HEADERS_INCLUDE`) are declared in
`config/external_paths.json` and may be overridden via the matching environment
variables; the apps themselves contain no personal-checkout paths.

Library/app ownership is enforced by:

```sh
make governance-check
make lib-readme-check
make app-port-check
```

Every app listed in `config/governance.json` must carry an `apps/*/PORTING.md`
file with upstream provenance, evidence rows, Unikraft build wiring, claim
boundaries, and runnable verification commands. The README status column is kept
in sync with governance metadata; `app-vkmark` is experimental because it is a
Vulkan substrate/evidence path, not a canonical release proof.

## llama.cpp single-application images

| Image | Kraftfile | Source compiled | Entrypoint | Claim boundary |
|---|---|---|---|---|
| CPU bench-only (`llm.bench.cpu`) | `kraft/Kraftfile.llama-upstream-bench` | only `bench.cpp` | `app-llama-upstream` bench mode | Upstream llama.cpp CPU bench when runtime PASS exists. |
| CPU server-only (`llm.server.cpu`) | `kraft/Kraftfile.llama-upstream-server` | only `server.cpp` | `app-llama-upstream` server mode | Server entrypoint boots directly; no shell/unrelated app. |
| Vulkan bench-only (`llm.bench.vk`) | `kraft/Kraftfile.llama-upstream-vk` | only `bench.cpp` | `app-llama-upstream-vk` bench mode | Upstream ggml-vulkan through Venus only with same-run PASS evidence. |
| Vulkan server-only (`llm.server.vk`) | `kraft/Kraftfile.llama-upstream-vk-server` | only `server.cpp` | `app-llama-upstream-vk` server mode | Vulkan server entrypoint boots directly; no shell/fork/exec. HTTP gated on Unikraft lwIP + EGL render-node availability. |

ELF Loader is kept as a discovery-only x86_64 compatibility path for missing
syscalls/VFS/socket assumptions; it is not the release architecture. A future
full HTTP port should call a refactored `llama_server_main(argc, argv)` from
the Unikraft app `main()`, not launch a second binary.

Environment selection, model path, backend flags, and thread counts are defined
in `config/llama_env_matrix.json` for:

- `baremetal+cuda`
- `baremetal+vulkan`
- `baremetal+cpu`
- `qemu+linux+vulkan`
- `qemu+linux+cpu`
- `qemu+unikraft+vulkan`
- `qemu+unikraft+cpu`

Use:

```sh
make llama-env-check
make llama-env-bench
make llama-env-server
```

Porting plan: `docs/llama-cpp-unikraft-porting-plan.md`. Per-app porting docs:
`apps/app-llama-upstream/PORTING.md`, `apps/app-llama-upstream-vk/PORTING.md`.

## Performance optimisation

The cited optimisation plan lives in `plan-optimize.md`. Every entry maps a
public reference (Unikraft EuroSys 2021, llama.cpp / ggml-vulkan issues + PRs,
Mesa Venus docs, QEMU virtio-gpu docs, lwIP tuning) to a concrete change in
this tree and a verification gate:

| Lever | Where | Gate |
|---|---|---|
| Hot/cold compile-flag split | `apps/app-llama-upstream*/Makefile.uk` `<FILE>_FLAGS-y` | `make perf-check` |
| SIMD swrender inner loops | `libs/libukswrender/swrender.c` | `make perf-check` |
| Coalesced VirtIO-GPU 2D fence | `libs/libukvirtio_gpu/virtio_gpu_real.c` + `tests/virtio_gpu_test.c` 200–204 | `make native-tests` |
| Batched SUBMIT_3D (env-gated) | `libs/libukggml_vk/uk_vulkan_dispatch.c` `UK_GGML_VK_DISPATCH_BATCH=1` | `tests/ggml_vk_dispatch_test.c` info-getter assertions |
| Venus encoder scalar fast path | `libs/libukvenus/venus_cs.c` | `tests/venus_cs_test`, `tests/ggml_vk_dispatch_test` |
| Continuous batching / prompt cache | `apps/app-llama-upstream{,vk}/server.cpp` + `Config.uk` `*_PARALLEL` / `*_PROMPT_CACHE` | `make llm-server-vk-check` |
| Model-load latency record (L1.4) | `apps/app-llama-upstream{,vk}/common.h` | `make model-load-time-check` |
| QEMU `hostmem=…,blob=true,venus=true` + `egl-headless` | `kraft/Kraftfile.llama-upstream-vk-server` + `scripts/run_llama_upstream_vk_server.sh` | `make llm-server-vk-check` |

Gates:

```sh
make perf-check                 # fps + pp512/tg128 regression vs baseline
make image-size-check           # per-appliance byte counts
make boot-time-check            # boot-to-READY ms
make model-load-time-check      # plan-optimize.md L1.4
make llm-server-vk-check        # plan-optimize.md Phase-2 static contract
```

`config/perf_baseline.json` is the authoritative reference; every gate writes
`results/<gate>/latest.{json,md}` so a CI run can diff against the captured
baseline.

The software-render rows (`gfx.kmscube.sw`, `gfx.glmark2.sw`) are deterministic
in frame/transfer/flush/fence counts but carry ±5–10% wall-clock variance on a
non-isolated host. `scripts/app_perf_eval.py` therefore samples the benchmark
best-of-N (default 5, override with `VOGUE_APP_PERF_REPS`) and keeps the lowest
`avg_frame_ms` per row for the noisy-host smoke gate. The JSON artifact also
persists the median and every sample, so reviewer-facing performance analysis can
separate best-case capability from typical-case timing. This removes false
`frame_ms` regressions without changing any threshold or acceleration claim
boundary.

## libukvenus autogeneration

`libukvenus` is the guest-side Venus encoder. Rather than fork Mesa's
[venus-protocol](https://gitlab.freedesktop.org/mesa/venus-protocol) generator
(3k+ lines of Python + 30 Mako templates), VOGUE consumes it through the
sibling `../venus-protocol/` checkout:

```sh
make gen-libukvenus-plan      # JSON: artifacts, sliced extensions, source
make gen-libukvenus-check     # validate ../venus-protocol/ + Mako install
make gen-libukvenus           # write libs/libukvenus/generated/*
```

Design notes: `libs/libukvenus/GENERATOR.md`. Only the Vulkan extensions
needed by upstream `ggml-vulkan.cpp` are sliced into the generated headers, so
the resulting unikernel image stays minimal.

## Main verification targets

```sh
make help
make test-fast                    # governance + docs + env matrix + native/proto tests
make artifact-quick               # fast artifact gate: native + Vulkan API/dispatch + docs/paper
make artifact-check               # artifact-quick + eval/current-stage
make verify                       # broadest release gate; may run QEMU-dependent checks
```

Focused targets:

```sh
make native-tests                 # deterministic host-native library tests
make vulkan-tests                 # optional host Vulkan compute baseline
make venus-check                  # VirtIO-GPU/Venus static and QEMU probe gates
make app-perf-check               # kmscube/glmark2 software-substrate benchmark
make app-multi-env-bench          # per-app multi-environment evidence summary
make llama-check                  # env matrix + ggml-vulkan API/dispatch gates
make llama-vulkan-api-coverage    # compare upstream ggml-vulkan.cpp to static dispatch
make llama-ggml-vk-dispatch     # host regression for libukggml_vulkan
make eval-check                   # regenerate evidence matrix
make venus-check                  # VirtIO-GPU/Venus static checks and QEMU probes
make stage-check                  # design/stage audit; includes Venus readiness
make benchmark-check              # native/readiness benchmark summary
make current-stage-check          # reviewer-facing completeness report
make paper-check paper            # paper consistency and PDF build
make claim-check                  # reject stale custom compute-remoting vocabulary
```

## Evidence and claim boundaries

Key rows:

| Row | Meaning |
|---|---|
| `gfx.kmscube.sw`, `gfx.glmark2.sw` | kmscube/glmark2 software-render ports execute through the Unikraft compatibility stack. |
| `proto.real-driver`, `vk.readiness`, `proto.venus-enc`, `proto.venus-ring` | VirtIO-GPU ABI, Venus readiness, command encoding, and ring protocol gates. |
| `vk.drm-shim`, `vk.smoke`, `gfx.vkmark` | DRM virtgpu shim and bounded Vulkan smoke/vkmark evidence. |
| `vk.ggml-dispatch` | `libs/libukggml_vk` covers the required upstream ggml-vulkan API surface and passes host-native dispatch checks. |
| `gfx.kmscube.submit`, `gfx.kmscube.frame` | Virgl submit/frame claims; current status is `blocked:missing-pass-marker` until same-run serial PASS plus pixel proof exists. |
| `xport.qemu-vgpu` | QEMU GL/Venus transport; current status is `blocked:probe-incomplete` until the real backend probe is complete. |
| `host.baseline.vk` | Host Linux/QEMU/Venus baseline only; useful environment evidence, not a Unikraft runtime claim. |
| `host.vk.probe`, `host.bench.vk.run`, `host.bench.vk` | Current status is `blocked:wrong-domain-artifact` if a host-baseline JSON is offered for a Unikraft row. |
| `llm.bench.cpu`, `llm.server.cpu` | Upstream llama.cpp CPU single-app runtime/server evidence or structured blocker. |
| `llm.bench.vk`, `llm.server.vk`, `llm.bench.vk.real` | Upstream llama.cpp Vulkan/Venus runtime evidence or structured blocker; no throughput claim while blocked. |

Allowed in this revision:

- Unikraft can host a compact VirtIO-GPU frontend and bounded graphics app
  substrates without Linux DRM/KMS in the guest.
- The real VirtIO-GPU protocol path, Venus command encoding, and ring protocol
  have native/static evidence.
- The static ggml-vulkan dispatch layer covers upstream llama.cpp/ggml Vulkan
  calls required by the pinned source and native tests.
- llama.cpp CPU/Vulkan appliances are single-application images with explicit
  bench/server modes, no native fork/exec launcher, and environment-matrix
  arguments.

Forbidden without same-run PASS artifacts:

- Unikraft llama.cpp token/s or GPU throughput.
- Treating a structured `blocked:*` row as a pass.
- Using host Linux baseline artifacts as Unikraft runtime evidence.
- Reintroducing a custom GGUF/ggml/llama runtime under local `libs/`.

## Documentation map

- Architecture: `docs/ARCHITECTURE.md`
- Governance: `docs/GOVERNANCE.md`, `config/governance.json`
- llama.cpp porting plan: `docs/llama-cpp-unikraft-porting-plan.md`
- Environment matrix: `config/llama_env_matrix.json`
- Library contracts: `libs/*/README.md`
- Application provenance/porting: `apps/*/PORTING.md` (including `apps/app-kmscube/PORTING.md`)
- Evidence matrix: `results/vogue_latest_evaluation_matrix.{json,csv,md}`
- Current-stage report: `results/stage/current_stage_report_latest.{json,md}`
- Paper: `paper/main.typ`, `paper/sections/*`, generated tables in `paper/generated/*`
