# VOGUE next-stage optimization plan

This file is the single authoritative optimization plan for VOGUE on branch
`dev-jerry`. It merges the valid parts of the former `plan-optimize.md` and
`plan-optimize-patch.md` into one evidence-first document.

## Scope, branch, and requirements

- Current VOGUE branch: `dev-jerry`
- Pinned sibling Unikraft evidence: `../unikraft` branch `stable`, commit `7351f8b`
- This plan must use current repo artifacts as the source of truth for this branch.
- This plan must use official documentation or upstream source files for capability
  claims.
- Any direction without same-branch proof must be labeled **Candidate only** or
  **Not yet validated on this branch**.
- Non-official / non-upstream references are intentionally excluded as
  optimization proof.

## Measured baseline

The current branch is already out of the old bring-up stage. The baseline for
next-stage work is the current passing repo state, not the historical blocked
state.

### Current branch evidence bundle

Primary local evidence files:

- `results/llama/server_vk_throughput.json`
- `results/llama/llama_server_vk.json`
- `python3 -m scripts.vogue capture vk-server`
- `kraft/Kraftfile.llama-vk-server`
- `apps/app-llama-vk/common.h`
- `apps/app-llama-vk/server.cpp`
- `results/llama/llama_vk.json`
- `results/llama/vulkan_linux_baseline.json`

### Current measured numbers

From `results/llama/server_vk_throughput.json`:

- `generated_utc=2026-06-02T10:45:55.097638Z`
- `concurrency=1`
- `decode_tps_mean=140.6`
- `decode_tps_max=154.38`
- `prompt_tps_mean=865.66`
- `requests_per_s=0.412`
- `ttft_s=1.2376`
- device `Tesla V100-SXM2-16GB`

From `results/llama/llama_server_vk.json`:

- `llm.bench.vk elapsed_ms=4691.79`
- `llm.server.vk elapsed_ms=6442.77`
- `use_mmap=false`
- `huge_pages=false`

From `results/llama/llama_vk.json`:

- in-guest Vulkan bench `pp512=2232.1`
- in-guest Vulkan bench `tg128=160.2`

From `results/llama/vulkan_linux_baseline.json`:

- host Vulkan baseline `pp512_t_per_s=5586.91`
- host Vulkan baseline `tg128_t_per_s=239.16`

From `python3 -m scripts.vogue capture vk-server`:

- the canonical QEMU command currently has **no `-smp`**, so the server path is
  still running with a single guest vCPU.

From `kraft/Kraftfile.llama-vk-server` and
`apps/app-llama-vk/server.cpp`:

- the server is configured for `--parallel 4`
- `ctx-size` is scaled to `parallel * per-slot ctx`
- the Vulkan server forces `--no-mmap`
- the Vulkan server forces `--flash-attn off`
- the readiness marker records `threads=1`

From `apps/app-llama-vk/common.h`:

- model loading explicitly keeps `use_mmap=false`
- the comment states current `9pfs` does not support mmap in this path
- `huge_pages=0` is still recorded in the loader path

Repository status gate:

- Current status is evidenced by the canonical `results/llama/*.json` captures
  (see *Latest verified runs* below); `make test-fast` is the host-native gate.

### Latest verified runs (x86_64 bring-up host, 2026-06-10)

The numbers above are the dated V100/NVIDIA capture. A subsequent x86_64 bring-up
on a software-Vulkan host (lavapipe over Venus) re-verified all four llama
appliances end-to-end (gemma-3-1b Q4_K_M):

- `results/llama/llama_cpu.json` → `pass` (`pp512=31.2`, `tg128=10.9`)
- `results/llama/llama_vk.json` → `pass` (`pp512=4582.6`, `tg128=353.8`)
- `results/llama/llama_server_vk.json` → `pass` (`/health` 200, `/completion` 200, `tokens_per_s=79.9`)

This required getting the host Venus stack working (Venus-capable virglrenderer +
QEMU rebuilt against it, software EGL render node, KVM) and the in-tree fixes
documented in the README "x86_64 Vulkan-server host bring-up" section and
`docs/VENUS-BRINGUP.md`: `CONFIG_LIBUKPAGING` on all four llama Kraftfiles, the
registered modern virtio-pci patch with `virtio_pci_shm_region_get` + BAR mapping,
a page-consistent `minMemoryMapAlignment`, and `--no-host` (device-local weights).
The appliance entrypoints are `apps/app-llama-vk/llama-vk-server-entry.cpp` and the
shared `apps/llama-common/llama-uk-common.h` loader (the older `server.cpp` /
`common.h` filenames referenced above have been superseded).

## Already shipped background

These are not the main next-stage targets anymore; they are already part of the
current baseline and should stay documented as shipped background:

1. **Already shipped — Venus batching and dispatch cleanup.** The current branch
   already moved far beyond the old `tg128=3.4` state. The in-guest Vulkan bench
   is now `tg128=160.2`, so the main decode rescue step has already landed.
2. **Already shipped — real HTTP serving over `virtio-net -> libuknetdev -> lwIP`.**
   The server path is no longer a blocker; it serves HTTP and has same-run
   throughput evidence in `results/llama/server_vk_throughput.json`.
3. **Already shipped — continuous batching / parallel slots at the application layer.**
   The appliance already carries `--parallel 4`, batch sizing, prompt cache, and
   the server-specific `ctx-size` scaling.
4. **Already shipped — `--flash-attn off` on the V100 Vulkan path.** This is a
   branch decision already encoded in `apps/app-llama-vk/server.cpp`.

## Merge decisions from the patch

| Patch direction | Status in merged plan | Why |
|---|---|---|
| SMP / multi-vCPU | **Not yet validated on this branch** and promoted to P0 | The current run script has no `-smp`, the server evidence is `concurrency=1`, and the readiness marker shows `threads=1`. This is the clearest remaining aggregate-throughput gap. |
| `--threads`, `--threads-batch`, `--threads-http` | **Not yet validated on this branch** and folded into P0 | The upstream server officially exposes these knobs, and they are more direct than guessing scheduler changes first. |
| model loading / paging policy | **Not yet validated on this branch** and promoted to P1 | Current code and artifacts prove `use_mmap=false` / `huge_pages=false`; the next optimization target is the model-load path, not generic Linux swap tuning. |
| `GGML_NATIVE`, `GGML_LTO`, target CPU flags | **Candidate only** | Upstream `ggml/CMakeLists.txt` confirms cross-compilation disables the native default, so this is real, but it is not yet benchmarked on this branch. |
| `mimalloc` | **Candidate only** | Unikraft performance docs and the mimalloc benchmarking write-up make it reasonable to test, but this branch has no measured win yet. |
| VirtioFS + DAX | **Candidate only** | The sibling Unikraft tree proves there is VirtioFS code and IOMEM support, but VOGUE has not yet switched the llama appliance to a validated VirtioFS path, and DAX is not branch-proven here. |
| preemptive scheduler | **Do not present as available** | The pinned local evidence confirms `lib/ukschedcoop/Config.uk`; this branch should not assume a tested preemptive scheduler path exists. |
| direct `uknetdev` bypass of the socket path | **Candidate only, low priority** | The current HTTP server already works over lwIP, and current throughput evidence does not show networking as the first bottleneck. |
| speculative decoding | **Candidate only** | Upstream supports it, but this branch has no draft-model artifact or server benchmark proving a win yet. |

## Prioritized next-stage plan

### Execution summary (P1–P4, `dev-jerry`, 2026-06-02)

Each item was analyzed and taken as far as the branch allows; per the stop
condition, no performance win is claimed without a branch-local artifact that
beats its baseline.

| Item | Verdict | Evidence | Unblock |
|---|---|---|---|
| **P1** model-load | Candidate — blocked on host `virtiofsd`; 9p `msize≈520 KB` already, `NUM_SEGMENTS` vq-matched (not safely tunable) | `virtio_9p.c:46,106,308`; `which virtiofsd`→none | install virtiofsd + `ukfs-virtiofs` mmap path, drop `--no-mmap` |
| **P2** build flags | **Executed** — `-march`/`-mtune` now explicit + overridable (`LLAMA_MARCH ?= native`); perf-neutral on eval host (build CPU == run CPU = Broadwell), portability win | `apps/app-llama-cpu/Makefile.uk`, `apps/app-llama-vk/Makefile.uk` | cross-host build sets `LLAMA_MARCH=<arch>` |
| **P3** mimalloc | Candidate — blocked: only in-tree allocators are bbuddy (current) + region (no free); mimalloc/tlsf/tinyalloc are external, and `lib-mimalloc select LIBNEWLIBC` conflicts with the appliance's musl/libc++ | `ukboot/Config.uk`; `lib-mimalloc/Config.uk:11` | add a musl-compatible allocator lib (lib-tlsf, or musl mimalloc glue) |
| **P4** speculative | Candidate — blocked: no same-vocab draft model on the branch | `../models/` (only qwen3-0.6b main + gemma) | add a vocab-compatible draft GGUF + `--model-draft` |

**Where the real ROI is.** P1–P4 are blocked/neutral on this branch, so the
next measured win lives in **P0 (guest SMP)** and in **VOGUE's own guest-side
Venus/dispatch stack**: the stock-Linux-guest-over-Venus baseline
(`results/llama/vulkan_qemu_linux_baseline.json`, `make linux-guest-vk-baseline`)
hits `pp512=4948 / tg128=323` over the *same* QEMU Venus device, i.e. ~2× the
Unikraft bench — proving the gap is the `libukvulkan_venus` driver + `libvulkan` dispatch
dispatch (and the single vCPU), not the Venus transport. Prioritize those next.

### P0. Guest SMP and thread split

**Status:** **Not yet validated on this branch**

**Why this is first:**

- `python3 -m scripts.vogue capture vk-server` currently records a single-vCPU runtime path
- current throughput evidence is only `concurrency=1`
- the READY line records `threads=1`
- the current server already carries `--parallel 4`, so the remaining obvious
  gap is that multiple slots still cannot overlap guest-side CPU work on a
  single vCPU

**Requirements for this step:**

1. Add explicit `-smp N` to the canonical server run path.
2. Verify the required local CPU support remains coherent with the pinned
   Unikraft files:
   - `../unikraft/lib/uklcpu/Config.uk`
   - `../unikraft/lib/ukpcpuvar/Config.uk`
   - `../unikraft/lib/ukschedcoop/Config.uk`
3. Tune application-level knobs from the official upstream server interface:
   - `--threads`
   - `--threads-batch`
   - `--threads-http`
4. Re-run the bounded throughput check with `concurrency > 1` and compare
   aggregate decode/request throughput against the current baseline.

**Success gate:**

- same-run throughput beats the current baseline without regressing correctness:
  `decode_tps_mean=140.6`, `prompt_tps_mean=865.66`, `ttft_s=1.2376`
- the new artifact must prove a real multi-vCPU server run, not just a config-only
  change

### P1. Model-load path replacement

**Status:** **Analyzed on this branch — blocked on host tooling (VirtioFS); no safe quick win in 9pfs**

**Result on `dev-jerry` (2026-06-02):**

- The mmap-capable lever is **VirtioFS**, but `virtiofsd` is **not installed on
  the evaluation host** (`which virtiofsd` → none; not in `qemu-src/build`), so a
  VirtioFS/DAX appliance path cannot be built or measured here yet. The sibling
  `../unikraft/lib/ukfs-virtiofs/{Config.uk,virtiofs.c}` exists, so the guest
  side is feasible; the blocker is the host daemon + a `vhost-user-fs-pci` wiring.
- The existing 9pfs read is **not pathologically chunked**: the virtio-9p
  transport already negotiates `max_msize = (NUM_SEGMENTS-1) * PAGE_SIZE ≈ 520 KB`
  (`../unikraft/drivers/virtio/9p/virtio_9p.c:46,106`). `NUM_SEGMENTS` is **exact-
  matched to the virtqueue descriptor count** (`virtio_9p.c:308` errors if
  `qdesc_size != NUM_SEGMENTS`), so it is **not safely tunable** without matching
  the QEMU queue size — no free win there.
- Conclusion: P1 stays **Candidate**. Unblock = install `virtiofsd`, mount the
  model over `ukfs-virtiofs`, switch the appliance off `--no-mmap`, and re-measure
  `make model-load-time-check` against `elapsed_ms=6442.77`. Not fabricated as a
  win until that path is built and measured.

**Why this is second:**

- the current model-load path is explicitly constrained by `use_mmap=false`
- current measured load time is still large: `llm.server.vk elapsed_ms=6442.77`
  and `llm.bench.vk elapsed_ms=4691.79`
- current comments in `apps/app-llama-vk/common.h` explain that the
  active `9pfs` path cannot use mmap here

**Requirements for this step:**

1. Keep the problem framing exact: this is a **model delivery / mmap-capable file
   path** problem, not a Linux swap/`mlock` tuning problem.
2. Evaluate a path that can replace the current `9pfs` limitation.
3. Keep VirtioFS as a candidate transport only after a branch-local integration
   path exists.
4. If VirtioFS is explored, pin evidence to the local Unikraft files:
   - `../unikraft/lib/ukfs-virtiofs/Config.uk`
   - `../unikraft/lib/ukfs-virtiofs/virtiofs.c`
5. Do **not** write VirtioFS DAX as a requirement until VOGUE has a working,
   measured appliance path using it.

**Success gate:**

- reduce model-load latency versus the current baseline
- update the canonical server runtime artifact
- prove the new path with branch-local artifacts rather than documentation alone

### P2. Build-target tuning

**Status:** **Executed on this branch — explicit + overridable target arch (perf-neutral on the eval host, portability win)**

**Result on `dev-jerry` (2026-06-02):**

- Audited against upstream `ggml/CMakeLists.txt` (native default OFF under
  cross-compilation). The appliance previously hard-coded `-march=native
  -mtune=native` in `apps/app-llama-cpu/Makefile.uk` and `apps/app-llama-vk/Makefile.uk`.
- **Change landed:** both Makefile.uk files now use `-march=$(LLAMA_MARCH)
  -mtune=$(LLAMA_MARCH)` with `LLAMA_MARCH ?= native`. This makes the target
  arch explicit and overridable for cross-host builds.
- **Measured expectation:** on the evaluation host the build CPU == the KVM run
  CPU (`-cpu host`, Xeon E5-2667 v4 = Broadwell), so `native` already resolves to
  the correct arch and the default produces **identical codegen** — P2 is
  **perf-neutral here by construction** and makes no throughput claim; its value
  is correctness/portability for hosts where build CPU != run CPU. Note also that
  the VK server offloads all layers to the GPU (`-ngl 99`), so CPU `-march` has
  little effect on its decode path regardless.

**Why it stays in scope:**

- upstream `ggml/CMakeLists.txt` explicitly sets `GGML_NATIVE_DEFAULT` to OFF
  under cross-compilation and exposes `GGML_NATIVE` and `GGML_LTO`
- VOGUE is built in a cross-build-like environment, so relying on upstream
  native defaults is unsafe

**Requirements for this step:**

1. Audit the current build against upstream `ggml/CMakeLists.txt`.
2. Make target CPU flags explicit instead of assuming `-march=native` is correct.
3. Benchmark before keeping any new flag set in the default plan.

**Success gate:**

- new build settings produce a measurable gain on this branch
- no portability regression is introduced for the evaluation host path

### P3. mimalloc evaluation

**Status:** **Analyzed on this branch — blocked: no musl-compatible drop-in allocator available**

**Result on `dev-jerry` (2026-06-02):**

- Unikraft's allocator menu (`../unikraft/lib/ukboot/Config.uk`) offers BBUDDY
  (current), REGION (**no `free()`** — unusable for llama), MIMALLOC, TINYALLOC,
  and TLSF. The faster ones are **external libraries that are not part of this
  branch**: MIMALLOC/TINYALLOC/TLSF each `depends on LIB*_INCLUDED` and the only
  in-tree alloc libs are `ukallocbbuddy`, `ukallocregion`, `ukallocpool`,
  `ukallocstack`.
- `lib-mimalloc` (cloned for evaluation, `../lib-mimalloc`, branch `stable`)
  additionally `select LIBNEWLIBC` (`../lib-mimalloc/Config.uk:11`). The llama
  appliance is built on **musl + libc++** (`CONFIG_LIBMUSL`), and newlib + musl
  are mutually exclusive libc providers — selecting mimalloc would force a second
  libc and break the link. So mimalloc is **not a drop-in** here.
- Conclusion: P3 stays **Candidate / blocked**. Unblock = add a musl-compatible
  external allocator (e.g. a `lib-tlsf` checkout, or a musl port of the mimalloc
  glue) and only then benchmark allocator vs `bbuddy` for the llama path. Note the
  expected upside is small for the GPU-bound VK decode; allocator churn mainly
  touches load + sampling + container paths.

**Why it is not P0/P1:**

- Unikraft's performance documentation notes that their measurements use Mimalloc,
  so allocator choice is relevant.
- The Unikraft mimalloc benchmarking write-up also shows multithreading/TLS work,
  which means this should follow, not precede, the SMP baseline cleanup.
- This branch does not yet have a measured allocator comparison for the llama
  server path.

**Requirements for this step:**

1. Benchmark allocator changes only after the SMP/server baseline is stable.
2. Compare load time, throughput, and stability against the default allocator.
3. Treat any gain as branch-specific until VOGUE artifacts prove it.

**Success gate:**

- measured win on this branch
- no regression in server stability or SMP behavior

### P4. Speculative decoding

**Status:** **Analyzed on this branch — blocked: no compatible draft model present**

**Result on `dev-jerry` (2026-06-02):**

- Speculative decoding requires a **smaller draft model that shares the main
  model's vocabulary**. The only GGUFs on the branch are `../models/qwen3-0.6b`
  (the main model — already tiny at 0.6 B) and `../models/gemma4-e2b` (a
  different family/vocabulary). There is **no smaller same-vocab Qwen3 draft**, so
  a `--model-draft` pairing cannot be formed and no server-side speedup can be
  measured.
- Conclusion: P4 stays **Candidate / blocked:no-draft-model**. Unblock = place a
  vocab-compatible draft GGUF (e.g. a distilled/smaller Qwen3) at
  `/mnt/model/draft.gguf`, add `--model-draft` (and draft `-ngld`) to
  `apps/app-llama-vk/server.cpp`, and benchmark through
  `make llm-server-vk-throughput-check`. Larger functional change than P1–P3; do
  not activate until the draft artifact exists.

**Why it is later:**

- upstream officially supports speculative decoding in `docs/speculative.md`
- the current branch has no draft-model artifact, no server-side benchmark, and
  no measured VOGUE result showing it helps this workload
- it is a larger functional change than SMP, thread tuning, or model-load path
  cleanup

**Requirements for this step:**

1. Do not activate this track until a draft-model or supported self-speculative
   setup is prepared for VOGUE.
2. Benchmark through the server path, not only through synthetic microbenchmarks.
3. Keep it out of the default optimization claim set until branch-local numbers exist.

**Success gate:**

- measured server-side speedup on this branch
- no correctness or stability regression

## Explicit de-prioritizations

### Preemptive scheduler

**Not yet validated on this branch**. The pinned local capability evidence is the
cooperative scheduler file `../unikraft/lib/ukschedcoop/Config.uk`. This plan
must not assume a tested preemptive scheduler implementation exists for VOGUE.

### VirtioFS DAX as a hard requirement

**Not yet validated on this branch**. The official virtio-fs project explains the
purpose and status of VirtioFS, and the pinned Unikraft sibling tree shows
VirtioFS code plus IOMEM support. That is enough to justify investigation, but
not enough to claim a working VOGUE DAX path today.

### Direct `uknetdev` optimization ahead of SMP/model-load work

**Candidate only**. The current socket/lwIP path already serves HTTP successfully,
so this is not the first optimization target unless a future artifact proves a
network bottleneck.

## Verification gates

Repo checks that exist today and gate this plan and future updates built from it:

- `make test-fast` — host-native suite + protocol/ABI checks
- `make verify` — broad release gate (adds Venus/Vulkan/llama runtime captures)

Per-metric gates (throughput / model-load-time) are **proposed** for this plan
and are not yet implemented as make targets.

When the next optimization step lands, the new artifact must beat the exact
baseline it claims to improve. Do not replace a baseline statement with a new
claim unless the new artifact is checked in and points to the same scope.

## Official documentation

Official / upstream documentation and source files used to justify the merged
plan:

- llama.cpp server options: `tools/server/README.md`
  - upstream path in sibling repo: `../llama.cpp/tools/server/README.md`
  - upstream URL: `https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md`
- llama.cpp speculative decoding: `docs/speculative.md`
  - upstream path in sibling repo: `../llama.cpp/docs/speculative.md`
  - upstream URL: `https://github.com/ggml-org/llama.cpp/blob/master/docs/speculative.md`
- ggml build defaults: `ggml/CMakeLists.txt`
  - upstream path in sibling repo: `../llama.cpp/ggml/CMakeLists.txt`
  - upstream URL: `https://raw.githubusercontent.com/ggml-org/llama.cpp/master/ggml/CMakeLists.txt`
- Unikraft performance documentation:
  - `https://unikraft.org/docs/concepts/performance`
- Unikraft architecture documentation:
  - `https://unikraft.org/docs/internals/architecture`
- Unikraft mimalloc benchmarking write-up:
  - `https://unikraft.org/blog/2024-08-22-unikraft-gsoc-benchmarking-mimalloc`
- QEMU virtio-gpu documentation:
  - `https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html`
- virtio-fs official site:
  - `https://virtio-fs.gitlab.io/`
- Pinned local Unikraft capability files used for branch-local support checks:
  - `../unikraft/lib/uklcpu/Config.uk`
  - `../unikraft/lib/ukpcpuvar/Config.uk`
  - `../unikraft/lib/ukschedcoop/Config.uk`
  - `../unikraft/lib/ukfs-virtiofs/Config.uk`
  - `../unikraft/lib/ukfs-virtiofs/virtiofs.c`

## Stop condition for the next optimization round

The next edit that claims an optimization win must satisfy all of the following:

1. the changed item is marked with the correct status label
2. the updated claim cites a new branch-local artifact
3. the new artifact beats the exact baseline it replaces
4. the branch still passes the required verification gates
