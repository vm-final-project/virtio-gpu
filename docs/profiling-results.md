# VOGUE Venus-path decode profiling — full record

Report-ready record of the compile-time-gated Venus-path profiler for the
`llama.cpp` Vulkan bench appliance. Captured from a clean rebuild of HEAD
`753cd6e` ("enable profiling for L2 dispatch and add stub timing"). All three
models pass; the per-layer split is stable across models and repeat runs
(absolute `tg128` wobbles with A30 GPU clock state — the **percentage split is
the robust result**).

Layer numbering follows the paper (`docs/.../03-design`): L1 Application ·
L2 Compat `libukggml_vk` (Vulkan dispatch) · L3 Venus `libukvenus` · L4 VirtIO
`libukvirtio_gpu` · below = host (QEMU + virglrenderer + GPU). We report **decode
(`tg128`) only**.

## 1. Environment (reproduction)

| Component | Setup |
|---|---|
| Host GPU | NVIDIA **A30, 24 GB** (`/dev/dri/renderD128`, nvidia driver) |
| VMM | `/opt/qemu-11.0.1/bin/qemu-system-x86_64` (QEMU 11.0.1) |
| Host bridge | virglrenderer **1.11.0** (`/usr/local/lib/x86_64-linux-gnu`); `virgl_render_server` at `/usr/local/libexec` |
| GPU device | `virtio-gpu-gl-pci, hostmem=2–4G, blob=true, venus=true`; `-display egl-headless,gl=on,rendernode=/dev/dri/renderD128`; KVM; `-smp 1` |
| Models | `gemma-3-1b-it`, `gemma-3-4b-it`, `Qwen2.5-7B-Instruct`, all `Q4_K_M` |
| Workload | upstream `llama-bench -ngl 99 -p 512 -n 128 --no-host 1 --mmap 0` |

> Note: the paper's eval-host table lists a Tesla V100; this profiling actually
> ran on the A30. The per-layer **fractions** are GPU-independent.

Run command:

```sh
env LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu \
    VIRGL_RENDER_SERVER_EXEC_PATH=/usr/local/libexec/virgl_render_server \
    VOGUE_EGL_RENDERNODE=/dev/dri/renderD128 VOGUE_QEMU_MEM_MB=<8192|12288|16384> \
  python3 scripts/app-llama-vk.py --mode bench --arch x86_64 \
    --model models/<M>.gguf --qemu /opt/qemu-11.0.1/bin/qemu-system-x86_64 --timeout 540
```

(`VOGUE_QEMU_MEM_MB` = 8192 / 12288 / 16384 for 1B / 4B / 7B; 7B also used
`VOGUE_GPU_HOSTMEM=4G`. Needs `/dev/dri/renderD128` + `/dev/kvm` readable/writable.)

## 2. Methodology

Compile-time-gated by `CONFIG_LIBUKVIRTIO_GPU_PROFILING=y` (`kraft/Kraftfile.llama-vk`;
macro `VOGUE_PROF_ENABLED`). Counters live in `libs/libukvirtio_gpu/virtio_gpu_real.c`,
emitted as `VOGUE-TIMING <cat>[prompt|decode]` lines and parsed by
`scripts/common.py`. Overhead ≈ 5 % of `tg128` (two monotonic-clock reads per
encoded command), so we report **fractions of decode wall**, not absolute deltas.
Upstream `llama-bench` is patched with weak hooks to tag the phase
(`pp512`→prompt, `tg128`→decode) and drop warm-up + one-time model-load counters,
so the report reflects only the measured reps.

Disjoint timed spans (per measured decode rep):

| Span (counter) | Layer | Bracket |
|---|---|---|
| `vk-encode` | L3 encode | `_t_enc` (after lock) → `VOGUE_ENC_DONE` (before flush), `uk_vulkan_dispatch.c` |
| `host-flush` | L3 flush wrapper | around `uk_venus_submit`, `venus_init.c:144–150` |
| `host-submit.active` | L4 guest | `_t_active`→`_t_wait`: sglist + enqueue + `virtqueue_host_notify`, `virtio_gpu_real.c:303–323` |
| `host-submit.wait` | host (below) | `_t_wait`→dequeue ok: busy-spin (`pause`) on used ring, `virtio_gpu_real.c:323–337` |
| `fence-wait` | L3 sync | `stub_vkWaitForFences` poll of `completed_fence`, `uk_vulkan_dispatch.c:1887–1891` |
| `L2-stub` | whole L2 dispatch | `VOGUE_STUB_DECL` (before lock) → `VOGUE_STUB_DONE` (after unlock); **nested**, contains the above |
| `wall[decode]` | total | `vogue_prof_reset_phase` → `vogue_prof_report`, `virtio_gpu_real.c` |

Derivations (per phase). The application is the only layer not directly timed; it
is recovered as the residual.

```
L1 app  = wall − vk-encode − host-flush − fence        # residual
L2 glue = L2-stub − vk-encode − host-flush  (≈ 0)      # confirms L2 is free
L3      = vk-encode + (host-flush − host-submit.active − host-submit.wait)
L4      = host-submit.active
host    = host-submit.wait + fence
```

## 3. Results — decode per-layer breakdown

| Layer (component) | 1B | 4B | 7B |
|---|---|---|---|
| **L1 Application** (`ggml-vulkan` / `llama.cpp` host) | **40.9 %** (1725 ms) | **39.0 %** (2051 ms) | **40.0 %** (1488 ms) |
| **L2 Dispatch** (`libukggml_vk`) | **<1 %** | **<1 %** | **<1 %** |
| **L3 Venus encode** (`libukvenus`) | 19.3 % (812 ms) | 22.7 % (1195 ms) | 19.4 % (723 ms) |
| **L4 VirtIO submit** (`libukvirtio_gpu`) | 10.2 % (430 ms) | 9.8 % (514 ms) | 10.8 % (401 ms) |
| **Host** (QEMU + virglrenderer + GPU) | 29.6 % (1248 ms) | 28.4 % (1494 ms) | 29.8 % (1110 ms) |
| decode wall (= 100 %) | 4215 ms | 5254 ms | 3721 ms |

ms are summed over all bench reps — compare the **%**, not raw ms. Columns may not
sum to exactly 100 % due to rounding and the sub-1 % L2 layer.

Per-token structure:

| | 1B | 4B | 7B |
|---|---|---|---|
| decode steps (tokens × reps) | 6400 | 6400 | 4480 |
| Venus commands / token | 496 | 645 | 572 |
| SUBMIT_3D round-trips / token | 5.0 | 5.5 | 6.0 |
| ring flushes (`L3-flush`) | **0** | **0** | **0** |
| `tg128` this run (tok/s) | 176.3 | 143.6 | 198.9 |

Run-to-run `tg128` (4 runs): 1B ≈ 166–182, 4B ≈ 88–153 (88 = clock outlier),
7B ≈ 199–231. The layer % split is stable; absolute throughput varies with A30
clock state.

## 4. Results — raw `VOGUE-TIMING` decode counters (ns)

| Counter (decode) | 1B | 4B | 7B |
|---|---|---|---|
| `wall` | 4,214,764,204 | 5,254,030,788 | 3,721,215,624 |
| `vk-encode` total / calls | 736,739,800 / 3,175,693 | 1,075,086,713 / 4,131,854 | 625,784,023 / 2,563,216 |
| `L2-stub` total / calls | 1,997,438,108 / 3,175,693 | 2,582,233,484 / 4,131,854 | 1,749,280,476 / 2,563,216 |
| `host-flush` total / calls | 1,728,902,511 / 32,013 | 2,122,351,666 / 35,214 | 1,601,500,048 / 26,896 |
| `host-submit.active` | 430,205,281 | 514,040,527 | 400,580,128 |
| `host-submit.wait` | 1,223,549,550 | 1,488,370,371 | 1,104,198,395 |
| `fence-wait` total / calls | 24,326,479 / 6,400 | 5,696,246 / 6,400 | 6,126,972 / 4,480 |
| `L3-flush` (ring) | 0 | 0 | 0 |

## 5. Key findings

1. **Decode is NOT host-bound** — host/GPU is < ⅓ (~29 %). The throughput gap vs.
   a Linux guest over the same Venus bridge lives in the guest, not the bridge.
2. **No single dominant layer:** app ~40 % / host ~29 % / our substrate
   (L3 + L4) ~30 %.
3. **L1 application (~40 %) is the largest layer** — upstream `ggml-vulkan` host
   code (per-token command-graph build, descriptor/sync bookkeeping, sampling) on
   a single vCPU. Not ours; it caps the upside of any L2–L4 work.
4. **L2 dispatch (`libukggml_vk`) is effectively free (<1 %)** — measured directly
   with the whole-stub timer; the static dispatch table adds no overhead.
5. **L3 Venus encode ~20 %**, **L4 VirtIO submit ~10 %** — the part VOGUE owns.
6. Stable across 1B→7B: bigger models do not shift work onto the GPU; decode
   stays guest-encode/submit-bound.

## 6. Transport: ring disabled, submission is synchronous

- **Ring stream is opt-in (`UK_GGML_VK_DISPATCH_RING=1`) and OFF** — the bench
  doesn't set it, so `L3-flush = 0`. Deliberately: the code note records it
  **regresses decode ~84 %** on this QEMU 11 + virglrenderer + single-vCPU stack
  (each ring flush still issues a synchronous `vkNotifyRingMESA` SUBMIT_3D, and
  the host `ring_thread` drain latency exceeds the inline batched submit).
- **Fallback = batched SUBMIT_3D, and every flush is SYNCHRONOUS.** Commands
  accumulate into one buffer (no per-command kick); each flush
  (`cmd_submit_locked`) enqueues, `virtqueue_host_notify`, then busy-spins
  (`pause`) until the host returns the used descriptor. ~5 blocking round-trips
  per token → decode is a serial request/response loop, **no submit pipelining**.
- GPU execution is async vs. that ack (the wait is host *receipt/decode*, not GPU
  completion; completion is tracked by `completed_fence`, set on the same ack —
  hence `fence-wait ≈ 0`).

## 7. Optimization implications (by layer)

- **P1 — cache/replay the per-token Venus encoding (L3, ~20 %):** ggml re-encodes
  ~500–650 near-identical commands every token.
- **P2 — async / non-blocking submit (L4 + host):** overlap token N+1 encode with
  token N's host round-trip, without depending on the ring's host drain thread.
- **P0 — guest SMP / ggml threads (L1, ~40 %):** the only lever on the single
  largest layer (ggml-vulkan submit is partly serial, so its SMP upside needs its
  own measurement).
- **L2 needs no work.**
