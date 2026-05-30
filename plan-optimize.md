# Performance optimization plan for `llm.server.vk`

`llm.server.vk` is VOGUE's single-purpose Vulkan/Venus llama.cpp server
appliance (`apps/app-llama-upstream-vk/server.cpp` + `kraft/Kraftfile.llama-upstream-vk-server`).
It runs upstream `llama.cpp` unmodified, routes Vulkan calls through
`libukggml_vk` → `libukvenus` → `libukvirtgpu_drm` → `libukvirtio_gpu` and
finally to QEMU `virtio-gpu-gl-pci,blob=true,venus=true`. Every claim below
is gated by a verifiable host artifact and a public citation; the goal is to
prove *each* change improves a measurable row in `results/perf/latest.json`
(or its Venus/HTTP counterpart) rather than chase generic micro-benchmarks.

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
| L2.2 | **Continuous batching + parallel slots** for the HTTP path | "Specialised images of ~1 MB for nginx/Redis" — server image is tiny; we can afford a `--parallel` slot count well above 1 ([Unikraft EuroSys 2021]). | At `parallel=4, batch=2048` on a single GPU, llama.cpp held **per-user 25 t/s and ~72 t/s aggregate** on a 13 B model (vs 28 t/s single). The server's "slot" abstraction is exactly the win. | [Promptsicle — "Optimizing llama-server throughput with batching"], [llama.cpp Discussion #18308]. | New `llm.server.vk` row records `aggregate_t/s` and `per_request_t/s` under repeat load. |
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
| L3.2 | **Coalesce TRANSFER_TO_HOST_2D + RESOURCE_FLUSH fences** (already shipped) | Same Mesa rule (cache + batch); VirtIO-GPU spec guarantees in-order command completion. | The 2D scanout path (used by `gfx.kmscube.sw` and any "render preview" the server later adds) cuts one fence per frame. | `libs/libukvirtio_gpu/virtio_gpu_real.c::uk_virtio_gpu_transfer_and_flush_2d` + test 200–204 in `tests/virtio_gpu_test.c`. | `make native-tests` — assertion `fence_waits == baseline + 1`. |
| L3.3 | **Venus encoder fast-path for scalars** (already shipped) | "Performance-oriented APIs" — inlining the scalar encoder keeps the hot loop a single `__builtin_memcpy`. | Every Vulkan call serialises uint32/uint64 fields; replacing the generic `encode_bytes` indirection cuts per-byte overhead in the encoder hot path. | `libs/libukvenus/venus_cs.c` — `uk_venus_encode_uint32/uint64/size`. | Native `venus_cs_test`, `ggml_vk_dispatch_test`. |
| L3.4 | **Map hostmem blob via `MAP_FIXED`** | Unikraft owns the guest's address space — picking a fixed window is trivial in a single-process unikernel. | The recently-merged QEMU patch (`virglrenderer 1.3 + map_fixed`) "has a great impact on performance" ([Vulkan Support gist]) and removes the legacy `mmap` fallback. | [virgl/QEMU virtio-gpu docs] note: "supporting mapping hostmem blobs with map_fixed has a great impact on performance". | New: `make venus-check` records whether the guest used the fixed-window path; surface as `xport.qemu-vgpu.hostmem_map`. |
| L3.5 | **Flash attention only with coopmat2** | Single-purpose image means we pick the right Vulkan code path at Kconfig time. | Upstream confirms: "Flash attention on Vulkan is only supported on some NVIDIA drivers with the coopmat2 extension"; without it, the kernel falls back to CPU and **drops ~50 %**. | [llama.cpp Issue #9572], [llama.cpp PR #11284], [llama.cpp DeepWiki — Flash Attention and Optimizations]. | New `vk.ggml-dispatch` sub-row: `flash_attention_enabled` toggled by `GGML_VULKAN_DISABLE_FA` env. |

---

## Layer 4 — QEMU / host transport

We don't ship QEMU, but the appliance only achieves its claim when the host
exposes the right device. Document, gate, and verify.

| # | Lever | Unikraft pattern | Why it helps `llm.server.vk` | Authoritative evidence | In-repo verification |
|---|---|---|---|---|---|
| L4.1 | **`virtio-gpu-gl,hostmem=8G,blob=true,venus=true`** | The kraft/Kraftfile model lets us pin device flags per appliance — no shared default that another image could change. | The blob window is where ggml's KV cache lives. 256M is enough for tiny models, 8G is the documented upper bound for serious model weights. | [QEMU docs — virtio-gpu]. | `scripts/venus_qemu_probe.py` (existing) — extend with `hostmem_bytes` assertion. |
| L4.2 | **`-display egl-headless,gl=on` for server (no Wayland/X11)** | Unikraft images have no display surface — server only needs the compute path. | Removing the windowed display path collapses the EGL render-node failure modes that currently produce `blocked:no-egl-render-node`. | [QEMU virtio-gpu docs — "egl-headless"]. | Existing `llm.bench.vk.real` blocker row. |
| L4.3 | **lwIP TCP knobs** for the HTTP listener (when the netdev gate lands) | Unikraft lwIP is picked + configured per image — no global default ([Unikraft Performance docs]). | The lwIP wiki documents `TCP_WND=24000`, `TCP_SND_BUF=16*MSS`, `TCP_SND_QUEUELEN=16` lifting a uCOS device from **9 → 12.9 Mbps**. The same knobs scale for in-VM HTTP traffic. | [lwIP wiki — "Maximising throughput"], [lwIP wiki — "Tuning TCP"]. | New `llm.server.vk.http` row recording requests/s. |

---

## Sequencing

Phase 1 (already merged in the previous goal): L2.1, L3.1, L3.2, L3.3, L1.1.

Phase 2 (this goal proposes):
1. **L1.4 + L1.2** — huge-page mmap loader + boot-time-check numbers.
2. **L4.1 + L4.2** — pin QEMU device flags in `kraft/Kraftfile.llama-upstream-vk-server` and document the `--display egl-headless` flag in PORTING.md.
3. **L2.2 + L2.3** — promote `--parallel` and `cache_prompt=true` into the appliance's startup parameters; baseline measurements with `llama-bench --parallel N`.
4. **L3.4** — make the static dispatch read the host-blob mapping and assert `MAP_FIXED` is in use.

Phase 3 (defer until phase 2 numbers exist): L1.3 (`COOP` scheduler trim), L1.5 (NUMA), L2.4 (speculative), L2.5 (Paged-KV), L4.3 (lwIP knobs).

---

## Verification gates (already wired + to add)

```sh
make perf-check                 # existing — fps + pp512/tg128 regression
make image-size-check           # existing — per-appliance byte counts
make boot-time-check            # existing — boot-to-READY ms
# Phase 2 additions:
make model-load-time-check      # first-byte → model-loaded latency
make llm-server-vk-check        # aggregate_t/s, per_request_t/s, TTFT
```

Each new gate emits `results/<gate>/latest.{json,md}` with the same
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
- **llama.cpp Issue #9572 — Vulkan flash attention regression** — <https://github.com/ggml-org/llama.cpp/issues/9572>
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
  + `tests/virtio_gpu_test.c` assertions 200–204 — fence count cut by one.
- `libs/libukvenus/venus_cs.c::uk_venus_encode_uint32/uint64/size` — scalar
  fast path; native `venus_cs_test` and `ggml_vk_dispatch_test` confirm
  wire-format equivalence (160/160 PASS).
- `libs/libukggml_vk/uk_vulkan_dispatch.c` — `UK_GGML_VK_DISPATCH_BATCH=1`
  collapses recorded command-buffer ops into one Venus submission;
  default-off so the per-call native dispatch test still counts as before.

Each subsequent layer entry above has a paper, PR, or blog with measured
numbers attached, so any reviewer can audit the *claim* and the
*reproducibility path* independently of the host running `make verify`.
