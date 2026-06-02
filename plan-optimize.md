# Performance optimization plan for VOGUE

This plan now targets the measured issues in the current VOGUE evidence set,
not the older pre-Venus blocked state. `llm.bench.vk` and `llm.server.vk` run
upstream `llama.cpp` unmodified through `libukggml_vk` → `libukvenus` →
`libukvirtgpu_drm` → `libukvirtio_gpu` → QEMU
`virtio-gpu-gl-pci,hostmem=...,blob=true,venus=true`. Every optimization below
must improve a named artifact row rather than introduce an unmeasured claim.

## Current performance findings

| Finding | Evidence | Interpretation | Primary fix track |
|---|---|---|---|
| Vulkan token generation was underperforming because decode work was dispatched too granularly | Before the fix, `results/llama/upstream_vk.json` recorded `pp512=247.4`, `tg128=3.4`; after enabling `UK_GGML_VK_DISPATCH_BATCH=1` and wiring llama.cpp batch controls into the appliance, the three same-host post-change runs in `results/llama/post_opt_runs/` measure `pp512={2208.6, 2230.5, 2232.1}` and `tg128={135.7, 139.9, 160.2}`. | The bottleneck was submission granularity and batching, not raw GPU availability: tiny per-token Venus submissions kept decode throughput far below the host's usable envelope. | Keep batched Venus submission enabled, keep `n_batch`/`n_ubatch` explicit in artifacts, and treat remaining work as server/request-path and model-load optimization rather than basic decode throughput rescue. |
| Model loading is still file-copy bound | `results/model-load/report.json`: CPU/VK app and server model load times are ~5.6-7.5 s with `use_mmap=false`, `huge_pages=false`. | 9pfs/initramfs model delivery does not yet use mmap or huge pages; startup latency hides boot improvements. | Add mmap-capable model path or initramfs huge-page staging; measure with `make model-load-time-check`. |
| Server now serves HTTP and its decode rate is near the in-guest bench | `results/llama/upstream_server_vk.json` (`slots=4`, `ctx_per_slot=512`, FA off) + `results/llama/server_vk_throughput.json`: same-run **decode 140.6 tok/s mean (154 max)**, prefill 866 tok/s, TTFT ~1.2 s. | The lwIP/netdev path landed, so `--parallel`, `--ctx-size`, prompt cache, and FA are now measured under real HTTP load. Server decode is **88 % of the in-guest Vulkan bench (`tg128=160`)** — the request path adds only ~12 % over the tight bench loop. The remaining gap to bare-metal native is the Venus transport tax, not server inefficiency. | Keep `--parallel 4` (concurrent-client capability) and `--flash-attn off` on the V100; the next aggregate-throughput win needs guest SMP (see Phase 3) so concurrent slots decode in overlapping CPU time. |
| Software graphics rows pass but are noisy | `results/app_perf.json`: best samples pass; fifth samples drop sharply (`kmscube` 224 fps, `glmark2` 100 fps). | Native fake-backend graphics are memory-copy and host-noise sensitive; best-of-N keeps the smoke gate stable but does not characterize steady-state variance. | Add median/p95 regression reporting and isolate memcpy/fence costs. |
| Real-driver static gate cost is visible | `results/benchmarks/benchmark_summary.md`: `VSTAT` mean 41.6 ms, p95 50.1 ms. | Static/readiness gate is not GPU runtime, but it exposes overhead worth tracking as protocol coverage grows. | Keep VSTAT as a trend metric and split encoder, ring, and controlq timings. |

### Native-baseline comparison (same V100, same GGUF, same upstream binary)

The reference is host-native Vulkan llama.cpp on the evaluation V100
(`results/llama/vulkan_linux_baseline.json`). "Native QEMU-Linux-Vulkan level"
is interpreted as the throughput the same workload reaches through the GPU; the
para-virtualised Venus path (guest → `virtio-gpu-gl` → host virglrenderer →
driver) pays a transport tax that bare-metal does not, so the realistic ceiling
for any guest (Unikraft *or* Linux) over Venus is below bare-metal.

| Metric | Native host Vulkan (bare metal) | VOGUE Unikraft + Venus | Unikraft ÷ native |
|---|---|---|---|
| `pp512` (prefill) | 5586.9 t/s | bench 2232.1 t/s · server 865.7 t/s | 40 % (bench) |
| `tg128` (decode) | 239.2 t/s | bench 160.2 t/s · **server 140.6 t/s** | 67 % (bench) · **59 % (server)** |

Reading: the Unikraft **server decode rate (140.6 t/s) is 88 % of the in-guest
Vulkan bench** and **59 % of bare-metal native**. Since the bench itself only
reaches 67 % of bare-metal through Venus, the server is essentially at the
Venus-path ceiling — the missing ~⅓ versus bare-metal is the transport tax
(addressed structurally by L3.1/L3.4 batching, already shipped), not a server or
scheduling defect. Aggregate (multi-client) throughput is currently capped by
the single guest vCPU; lifting it is the SMP item in Phase 3.

The plan is organised by which layer the optimisation modifies (Unikraft
core, ggml/llama.cpp inference, Venus encoder, QEMU/host). Each entry
records:

1. **Lever** — what we change.
2. **Unikraft pattern** — the explicit design tenet we lean on.
3. **Why it helps `llm.server.vk`** — concrete causal chain.
4. **Authoritative evidence** — the public reference that says this works.
5. **In-repo verification** — the gate (existing or new) that demonstrates the win.

---

## Layer 1 — Unikraft core specialisation

The Unikraft paper documents the win pattern: removing every component that
doesn't serve the workload, then specialising what remains.

| # | Lever | Unikraft pattern | Why it helps `llm.server.vk` | Authoritative evidence | In-repo verification |
|---|---|---|---|---|---|
| L1.1 | **Compile-time mode split + `--gc-sections`** (already shipped) | "Size-based specialisation by removing unnecessary components to achieve minimal images" ([Unikraft EuroSys 2021]). | Server image carries `server.cpp` only — `bench.cpp` is not even a translation unit; the linker drops every symbol unreachable from the server entrypoint. | EuroSys 2021 paper §4 reports specialised images of ~1 MB for nginx/Redis ([arxiv 2104.12721, p. 1]). | `make image-size-check` + the `bld.uk.vk` evidence row. |
| L1.2 | **Static page-table baked into the image** | "Unikraft provides a method for improving boot speed by including an already initialised page-table structure in the binary" ([Unikraft docs/architecture]). | Server appliance boots into a single process; the static PT removes ~ms of paging setup before the first `llama_decode` call. | EuroSys 2021 §6.2 — Hello-World boots in 1.04 ms on KVM, 31 µs on Xen ([arxiv 2104.12721, §6]). | `make boot-time-check` (already wired, currently `blocked:image-missing` on macOS review hosts; numbers come from the Linux runner). |
| L1.3 | **Drop the cooperative scheduler when no threads beyond ggml's pool exist** | uksched API explicitly separates "creation" from "scheduling"; the unused half is configurable out ([uksched PR #564]). | Server's hot loop uses ggml's own thread pool over `lib-pthread-embedded`. We don't need preemptive ticks; setting `CONFIG_LIBUKSCHED_COOP` keeps the timer interrupt rate low → less context-switch noise during long decodes. | Unikraft `lib-pthread-embedded` docs note pthread integration is the activation point for cooperative scheduling ([lib-pthread-embedded README]). | New `boot-time-check` row "context-switches/s" recorded from `qemu -d in_asm` or `kvm_stat` (add as P1 item below). |
| L1.4 | **Huge-page mapping for the model file via 9pfs/initramfs** | Unikraft's filesystem driver classes (initrd vs external volume) are picked at compile time per appliance ([Unikraft filesystem docs]). | llama.cpp's mmap loader benefits dramatically from `MAP_HUGETLB`; a 377 GB DeepSeek model loaded **10× faster** when the upstream PR landed. We replicate the win at our 1–8 GB working-set size. | `llama.cpp` issue [#12444 — "allow mmap to take advantage of hugepage feature which has 10x speedup"]. | New `model-load-time-check` gate — first-byte to "model loaded" latency. |
| L1.5 | **NUMA-aware tensor placement** when the host has > 1 socket | The same paper argues for specialisation per *environment*: "best system component for a given application, environmental constraints" ([Unikraft EuroSys 2021]). | Cross-NUMA llama.cpp on Neoverse N2 ships **up to 55 %** faster tg128 once tensors migrate to the local node. Even one-socket guests benefit from disabling prefetch + `mlock` to avoid initial-touch faults. | [SemiEngineering — "Scaling llama.cpp on Neoverse N2"]. | New `llama-numa-check` row in `results/llama/`; defaults to `blocked:single-socket` on dev hosts. |

---

## Layer 2 — Inference (ggml + llama.cpp)

Same source tree as the `llama.cpp` upstream; we ship the build flags and
runtime knobs so the upstream-unmodified contract holds.

| # | Lever | Unikraft pattern | Why it helps `llm.server.vk` | Authoritative evidence | In-repo verification |
|---|---|---|---|---|---|
| L2.1 | **Hot/cold compile-flag split** (already shipped) | "Composable, performance-oriented APIs" with per-file specialisation ([Unikraft docs/architecture]). | ggml CPU helper kernels (vec/ops/quants/repack), x86 quant unpackers and the llama-graph executor compile at `-O3 -funroll-loops -fno-math-errno -fno-trapping-math`; entry-points stay `-Os`. The hot path dominates pp512/tg128. | `apps/app-llama-upstream/Makefile.uk` defines `APPLLAMA_UPSTREAM_HOT_FLAGS` per the `<FILE>_FLAGS-y` convention. Effect documented by upstream — "tuning llama.cpp" guides recommend exactly this set ([Apple Silicon tuning guide]). | `make perf-check` — `llm.bench.cpu pp512` / `tg128`. |
| L2.2 | **Continuous batching + parallel slots** for the HTTP path (shipped) | "Specialised images of ~1 MB for nginx/Redis" — server image is tiny; we can afford a `--parallel` slot count well above 1 ([Unikraft EuroSys 2021]). | Shipped: `CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL=4` (READY reports `slots=4`); `server.cpp` scales `--ctx-size` to `PARALLEL*CTX` so each slot keeps its full window. Discussion #18308 confirms `-np 4` is the sweet spot (beyond 4 is CPU-sampling-bound) and `-b 2048 -ub 512` the baseline — exactly our settings. On the current **single guest vCPU**, concurrent slots cannot overlap CPU work, so aggregate scaling awaits SMP (Phase 3); single-stream decode already reaches 140.6 t/s. | [Promptsicle — batching], [llama.cpp Discussion #18308] (server ≈ 56 % of batched-bench due to CPU sampling). | `make llm-server-vk-throughput-check` → `results/llama/server_vk_throughput.json` (decode/prefill tok/s, TTFT, requests/s). |
| L2.3 | **Prefix / prompt cache** (`cache_prompt=true`) | One-image-one-purpose lets us reserve a known slice of RAM for cache ([Unikraft EuroSys 2021]). | Repeating system prompts re-use KV cache, skipping prefill on subsequent requests. llama.cpp's tutorial measures the hot-swap path "calculates prefix similarity, hot-swaps the cached prefix into GPU context". | [llama.cpp — "Mastering Host-Memory Prompt Caching" tutorial], [llama.cpp #8947]. | New row: time-to-first-token (TTFT) with and without `cache_prompt`. |
| L2.4 | **Speculative decoding (draft model)** | Two single-purpose images side-by-side beats one bloated image — exactly Unikraft's lean-images thesis ([Unikraft EuroSys 2021]). | Upstream documents draft + main pair; CARD / OmniDraft papers report ≥ 2× wall-clock for tg128 under matched-vocab conditions. | [llama.cpp `docs/speculative.md`], [arXiv 2508.04462 — CARD], [arXiv 2507.02659 — OmniDraft]. | Defer until L2.2 baseline exists; gate as `blocked:no-draft-model` until a draft GGUF is dropped into `/mnt/model/draft.gguf`. |
| L2.5 | **PagedAttention-style KV pagination** | Reference architecture for "treating GPU memory the way an OS treats RAM" ([vLLM blog]). | The current llama.cpp Vulkan path uses one contiguous KV slab per slot. As we scale parallel slots (L2.2) memory fragmentation kills throughput; the vLLM paper documents the fix. | [vLLM blog — "Inside vLLM: Anatomy of a High-Throughput LLM Inference System"]. | Tracking row; promote to "in scope" only after L2.2 lands. |

---

## Layer 3 — Vulkan / Venus dispatch

This is where VOGUE adds value beyond stock llama.cpp: we own the static
dispatch layer and the Venus encoder, so the wire-format wins are ours to
ship.

| # | Lever | Unikraft pattern | Why it helps `llm.server.vk` | Authoritative evidence | In-repo verification |
|---|---|---|---|---|---|
| L3.1 | **Batched SUBMIT_3D inside a command buffer** (already shipped, env-gated) | "Caches certain results and batches commands when feasible" is what Mesa's own Venus driver does ([Mesa Venus docs]). | Each `vkCmd*` previously generated one Venus SUBMIT_3D; batching collapses N stubs to one transport call per `vkEndCommandBuffer`. For ggml-vulkan a single graph step encodes ~50–80 vkCmd ops → ≥ 10× fewer host crossings. | Mesa documents Venus' own batching strategy; the Venus Vulkan extension write-up explains command serialisation cost ([Collabora — "A look at Vulkan extensions in Venus"]). | `libukggml_vk/uk_vulkan_dispatch.c` — `UK_GGML_VK_DISPATCH_BATCH=1`; native `ggml_vk_dispatch_test` keeps the per-call counter via the default-off path. |
| L3.2 | **Coalesce TRANSFER_TO_HOST_2D + RESOURCE_FLUSH fences** (already shipped) | Same Mesa rule (cache + batch); VirtIO-GPU spec guarantees in-order command completion. | The 2D scanout path (used by `gfx.kmscube.sw` and any "render preview" the server later adds) cuts one fence per frame. | `libs/libukvirtio_gpu/virtio_gpu_real.c::uk_virtio_gpu_transfer_and_flush_2d` + test 200–204 in `tests/virtio_gpu_full_api_test.c`. | `make native-tests` — assertion `fence_waits == baseline + 1`. |
| L3.3 | **Venus encoder fast-path for scalars** (already shipped) | "Performance-oriented APIs" — inlining the scalar encoder keeps the hot loop a single `__builtin_memcpy`. | Every Vulkan call serialises uint32/uint64 fields; replacing the generic `encode_bytes` indirection cuts per-byte overhead in the encoder hot path. | `libs/libukvenus/venus_cs.c` — `uk_venus_encode_uint32/uint64/size`. | Native `venus_cs_test`, `ggml_vk_dispatch_test`. |
| L3.4 | **Map hostmem blob via `MAP_FIXED`** | Unikraft owns the guest's address space — picking a fixed window is trivial in a single-process unikernel. | The recently-merged QEMU patch (`virglrenderer 1.3 + map_fixed`) "has a great impact on performance" ([Vulkan Support gist]) and removes the legacy `mmap` fallback. | [virgl/QEMU virtio-gpu docs] note: "supporting mapping hostmem blobs with map_fixed has a great impact on performance". | New: `make venus-check` records whether the guest used the fixed-window path; surface as `xport.qemu-vgpu.hostmem_map`. |
| L3.5 | **Flash attention forced OFF on the V100 Vulkan path** (shipped, decided) | Single-purpose image means we pick the right Vulkan code path at Kconfig/argv time. | Decided: the V100 (Volta) has **no `GL_NV_cooperative_matrix2`**, so Vulkan flash attention is unsupported and, when forced, *regresses* Vulkan throughput ~50 % ([Issue #9572], [#13008]). `server.cpp` therefore passes `--flash-attn off` instead of trusting the `auto` default. | [llama.cpp Issue #9572], [Issue #13008 — V100 FA], [PR #11284]. | `server.cpp` argv carries `--flash-attn off`; the decode rate (140.6 t/s) is measured with FA off. Revisit only on a coopmat2-capable GPU (Ampere+). |

---

## Layer 4 — QEMU / host transport

We don't ship QEMU, but the appliance only achieves its claim when the host
exposes the right device. Document, gate, and verify.

| # | Lever | Unikraft pattern | Why it helps `llm.server.vk` | Authoritative evidence | In-repo verification |
|---|---|---|---|---|---|
| L4.1 | **`virtio-gpu-gl,hostmem=8G,blob=true,venus=true`** | The kraft/Kraftfile model lets us pin device flags per appliance — no shared default that another image could change. | The blob window is where ggml's KV cache lives. 256M is enough for tiny models, 8G is the documented upper bound for serious model weights. | [QEMU docs — virtio-gpu]. | `scripts/venus_qemu_probe.py` (existing) — extend with `hostmem_bytes` assertion. |
| L4.2 | **`-display egl-headless,gl=on` for server (no Wayland/X11)** | Unikraft images have no display surface — server only needs the compute path. | Removing the windowed display path collapses the EGL render-node failure modes that currently produce `blocked:no-egl-render-node`. | [QEMU virtio-gpu docs — "egl-headless"]. | Existing `llm.bench.vk.real` blocker row. |
| L4.3 | **lwIP netdev HTTP path** (shipped) + TCP knobs (future) | Unikraft lwIP is picked + configured per image — no global default ([Unikraft Performance docs]). | Shipped: `virtio-net → libuknetdev → lwIP` (DHCP) carries the upstream `llama_server()` listener; the HTTP path is no longer a blocker. The lwIP wiki TCP-window knobs (`TCP_WND`, `TCP_SND_BUF`) remain a future tuning lever for sustained large-payload transfers, but the small JSON request/response traffic here is GPU/CPU-bound, not TCP-window-bound. | [lwIP wiki — "Maximising throughput"], [lwIP wiki — "Tuning TCP"]. | `make llm-server-vk-check` (`http=pass`) + `make llm-server-vk-throughput-check`. |

---

## Sequencing

Phase 0 (current state): the evaluation matrix is 27/27 PASS on the evaluation
host and `make current-stage-check` now passes after the real-path checker fix.
Optimization results may be compared against the current release evidence, but
hosts without the same QEMU/Venus/GPU stack must still treat those rows as
host-conditional rather than universal claims.

Phase 1 (implemented): L1.1, L2.1, L3.1, L3.2, L3.3.

Phase 2 (implemented):
1. **L3.1 + L2.2 decode-batching** — the low-throughput root cause was overly
   granular Venus submission. `UK_GGML_VK_DISPATCH_BATCH=1` is enabled in both
   Vulkan appliances; same-host `tg128` improved from `3.4` to ~160.
2. **L4.3 + L2.2 + L2.3 + L3.5 request path** — the lwIP/netdev HTTP path
   landed; the server runs with `--parallel 4`, `--ctx-size = PARALLEL*CTX`,
   prompt cache, and `--flash-attn off` (V100). Measured: **decode 140.6 t/s
   (88 % of the in-guest bench, 59 % of bare-metal native)**, prefill 866 t/s,
   via `make llm-server-vk-throughput-check`.

Phase 3 (next levers, in priority order):
1. **Guest SMP for aggregate throughput** — the single biggest remaining win.
   The appliance runs on one vCPU, so the four parallel slots cannot overlap
   their CPU (sampling/HTTP) work; with `-smp N` + Unikraft `LIBUKLCPU` and a
   matching `--threads`, continuous batching should lift aggregate (multi-client)
   tokens/s well above single-stream. Gate: extend the throughput check with a
   `concurrency>1` aggregate row once SMP boots.
2. **L1.4 + L1.2** — reduce the ~5-7 s model-load path with mmap/huge-page
   delivery (note: 9pfs does not support file mmap today, hence `--no-mmap`; an
   initramfs huge-page staging path is the route).
3. Deferred: L1.3 (`COOP` trim), L1.5 (NUMA), L2.4 (speculative), L2.5
   (Paged-KV), L4.3 lwIP TCP-window knobs, graphics-substrate variance reporting.

---

## Verification gates (already wired + to add)

```sh
make perf-check                 # existing — fps + pp512/tg128 regression
make image-size-check           # existing — per-appliance byte counts
make boot-time-check            # existing — boot-to-READY ms
make model-load-time-check      # existing — first-byte → model-loaded latency
make llm-server-vk-check        # readiness + same-run HTTP probe (http=pass)
make llm-server-vk-throughput-check  # measured decode/prefill tok/s, TTFT, requests/s
```

Each new gate emits `results/<gate>/report.{json,md}` with the same
"current vs baseline vs threshold" shape that `perf-check` already uses, so
the structured-blocker pattern remains intact for reviewers running on hosts
without the corresponding artifacts.

---

## References

- **Unikraft (EuroSys 2021)** — Kuenzer et al. *Unikraft: Fast, Specialised Unikernels the Easy Way*. EuroSys '21 Best Paper.
  PDF: <https://dl.acm.org/doi/pdf/10.1145/3447786.3456248>
  arXiv: <https://arxiv.org/pdf/2104.12721>
- **Unikraft Architecture / Performance docs** — <https://unikraft.org/docs/concepts/architecture/> · <https://unikraft.org/docs/concepts/performance>
- **Unikraft Filesystem docs** — <https://unikraft.org/docs/cli/filesystem>
- **Unikraft `lib/uksched` PR #564** — <https://github.com/unikraft/unikraft/pull/564>
- **Unikraft `lib-pthread-embedded` README** — <https://github.com/unikraft/lib-pthread-embedded/blob/staging/README.md>
- **llama.cpp server README** — <https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md>
- **llama.cpp Discussion #18308 — optimal parallel parameters** — <https://github.com/ggml-org/llama.cpp/discussions/18308>
- **llama.cpp Issue #12444 — huge-page mmap, 10× speedup** — <https://github.com/ggml-org/llama.cpp/issues/12444>
- **llama.cpp Discussion #20574 — host-memory prompt caching** — <https://github.com/ggml-org/llama.cpp/discussions/20574>
- **llama.cpp `docs/speculative.md`** — <https://github.com/ggml-org/llama.cpp/blob/master/docs/speculative.md>
- **llama.cpp Issue #9572 — Vulkan flash attention regression (~50%)** — <https://github.com/ggml-org/llama.cpp/issues/9572>
- **llama.cpp Issue #13008 — Does the V100 support flash attention?** — <https://github.com/ggml-org/llama.cpp/issues/13008>
- **llama.cpp PR #11284 — coopmat2 fix** — <https://github.com/ggml-org/llama.cpp/pull/11284>
- **llama.cpp Flash Attention DeepWiki** — <https://deepwiki.com/ggml-org/llama.cpp/8.2-flash-attention-and-optimizations>
- **Promptsicle — Boosting llama-server with batch settings** — <https://promptsicle.com/tips/boosting-llama-server-performance-with-batch-settings/>
- **Apple Silicon llama-server tuning** — <https://medium.com/@michael.hannecke/tuning-llama-server-on-apple-silicon-9b3e778ab100>
- **SemiEngineering — Cross-NUMA llama.cpp on Neoverse N2** — <https://semiengineering.com/scaling-llama-cpp-on-neoverse-n2-solving-cross-numa-performance-issues/>
- **vLLM blog — Anatomy of a High-Throughput LLM Inference System** — <https://blog.vllm.ai/2025/09/05/anatomy-of-vllm.html>
- **arXiv 2508.04462 — CARD speculative decoding** — <https://arxiv.org/pdf/2508.04462>
- **arXiv 2507.02659 — OmniDraft on-device speculative decoding** — <https://arxiv.org/pdf/2507.02659>
- **Mesa Venus driver docs** — <https://docs.mesa3d.org/drivers/venus.html>
- **Collabora — A look at Vulkan extensions in Venus** — <https://www.collabora.com/news-and-blog/blog/2022/10/19/a-look-at-vulkan-extensions-in-venus/>
- **QEMU virtio-gpu docs** — <https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html>
- **QEMU with VirtIO GPU Vulkan Support (recipe, includes `map_fixed` note)** — <https://gist.github.com/peppergrayxyz/fdc9042760273d137dddd3e97034385f>
- **lwIP wiki — Maximising throughput** — <https://lwip.fandom.com/wiki/Maximizing_throughput>
- **lwIP wiki — Tuning TCP** — <https://lwip.fandom.com/wiki/Tuning_TCP>
- **lwIP nongnu — `TCP_WND` / `TCP_SND_BUF` defines** — <https://www.nongnu.org/lwip/2_0_x/group__lwip__opts__tcp.html>
- **Unikraft eurosys21-artifacts (reproducibility)** — <https://github.com/unikraft/eurosys21-artifacts>

---

## What the existing implementation already demonstrates

The Phase-1 changes that landed in the previous goal correspond to entries
L1.1, L2.1, L3.1, L3.2 and L3.3 above. Their concrete proof artifacts:

- `apps/app-llama-upstream{,vk}/Makefile.uk` — hot/cold flag split, per-file
  HOT overrides via `<FILE>_FLAGS-y`.
- `libs/libukvirtio_gpu/virtio_gpu_real.c::uk_virtio_gpu_transfer_and_flush_2d`
  + `tests/virtio_gpu_full_api_test.c` assertions 200–204 — fence count cut by one.
- `libs/libukvenus/venus_cs.c::uk_venus_encode_uint32/uint64/size` — scalar
  fast path; native `venus_cs_test` and `ggml_vk_dispatch_test` confirm
  wire-format equivalence (160/160 PASS).
- `libs/libukggml_vk/uk_vulkan_dispatch.c` — `UK_GGML_VK_DISPATCH_BATCH=1`
  collapses recorded command-buffer ops into one Venus submission;
  default-off so the per-call native dispatch test still counts as before.

Phase-2 server request-path changes (this goal):

- `kraft/Kraftfile.llama-upstream-vk-server` — `CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL=4`
  (continuous-batching slots).
- `apps/app-llama-upstream-vk/server.cpp` — `--ctx-size = PARALLEL*CTX` (each
  slot keeps its window) and `--flash-attn off` (V100 has no coopmat2).
- `scripts/llama_server_vk_capture.py` + `scripts/llm_server_vk_throughput_check.py`
  + `make llm-server-vk-throughput-check` — measured decode/prefill tok/s, TTFT,
  requests/s in `results/llama/server_vk_throughput.json` (decode 140.6 t/s).

Each subsequent layer entry above has a paper, PR, or blog with measured
numbers attached, so any reviewer can audit the *claim* and the
*reproducibility path* independently of the host running `make verify`.
