# VOGUE profiling plan

This file is the authoritative profiling plan for explaining the measured gap
between the Unikraft Venus guest and a stock Linux guest over the **same** QEMU
Venus device. It follows the same evidence discipline as `plan-optimize.md`:
every claim must cite a branch-local artifact, and any direction without
same-branch proof is labeled **Candidate only** or **Not yet validated on this
branch**.

## Why profile, and what question to answer

From `docs/plan-optimize.md` (measured baseline):

- Unikraft in-guest Vulkan bench: `pp512=2232.1 / tg128=160.2`
  (`results/llama/llama_vk.json`, dated V100 capture)
- Stock-Linux-guest-over-Venus baseline on the *same* QEMU device:
  `pp512=4948 / tg128=323` (`results/llama/vulkan_qemu_linux_baseline.json`)

The transport (QEMU `virtio-gpu-gl` + virglrenderer Venus + host driver) is
identical in both runs, so the ~2× gap lives in the guest stack:
`libukvulkan_venus` + `libvulkan` dispatch, plus the single-vCPU runtime.
Profiling must therefore be **differential**: run the same workload in both
guests and attribute the delta, not profile VOGUE in isolation.

### The three questions

1. **Per-token guest CPU budget** — where does guest CPU time go per generated
   token: Venus command-stream encode, virtqueue kick, fence wait, memcpy?
2. **Round-trips per token** — how many virtio round-trips / vmexits per token
   does VOGUE issue vs. Mesa's Venus driver (which submits asynchronously)?
3. **GPU-op parity** — is per-operation GPU execution time identical between
   the two guests? If yes, the gap is 100 % CPU-side dispatch.

### Canonical workload

- VOGUE side: `make llama-vk-bench-run` (fixed model, pp512 + tg128, PASS
  marker parsed by `scripts/app-llama-vk.py`).
- Linux-guest side: the `vulkan_qemu_linux_baseline.json` capture path on the
  same QEMU/Venus host stack.

## Phase 1 — outside-in, zero guest changes (host side)

**Status: implemented (tooling) — `scripts/profile-llama-vk.py` + `mk/profile.mk`.
Runtime evidence requires the Venus-capable evaluation host; runs on a
non-KVM/non-perf host record an honest `blocked:*` status.**

All four tools wrap the *identical* bench QEMU invocation that
`scripts/app-llama-vk.py` builds (single source of truth — the profiler imports
its `qemu_command()`), so profiled runs are the same workload as the evidence
runs. Each emits `results/profile/<tool>.json` in the standard result schema
plus raw artifacts under `results/profile/raw/`.

| Target | Tool | What it answers |
|---|---|---|
| `make profile-vk-kvm-stat` | `perf kvm stat record` | vmexit profile: exit reasons, HLT residency (fence stalls), MMIO/PIO kick rate → **exits per token** |
| `make profile-vk-host-record` | `perf record -g` on QEMU | host-side split: vCPU thread vs virglrenderer render thread; host flamegraph input |
| `make profile-vk-guest-record` | `perf kvm --guest record` + unikernel ELF symbolization | **guest flamegraph** without an in-guest profiler (unikernel = single static binary, no ASLR) |
| `make profile-vk-qemu-trace` | QEMU `--trace "virtio_gpu_*"` | SUBMIT_3D / fence command rates per second and per token |

Differential requirement: each tool must also be run against the Linux-guest
baseline on the same host before any attribution claim is made.

Host prerequisites: `perf` with KVM
support, `/dev/kvm` access, and the §7 Venus stack from the README. The runner
degrades to `blocked:perf-missing` / `blocked:kvm-missing` / etc. instead of
fabricating numbers.

## Phase 2 — GPU-op-level timing (ggml perf logger)

**Status: implemented on this branch — driver query-pool wiring + appliance
toggle. Not yet runtime-validated on the Venus host (this dev box has no GPU
stack); validation = first `profile-vk-bench-perf-logger` capture.**

Upstream ggml-vulkan at the pinned tag (`b9581`,
`ggml/src/ggml-vulkan/ggml-vulkan.cpp:6576`) supports
`GGML_VK_PERF_LOGGER=1`: it brackets every graph node with
`vkCmdWriteTimestamp` and prints per-op GPU times. If per-op GPU times match
the Linux guest, question 3 is answered with hard evidence.

### Branch-local blockers found and fixed (2026-06-12)

The perf logger needs five Vulkan entry points and one device limit that were
previously fabricated or stubbed in `libs/libvulkan/uk_vulkan_dispatch.c`:

| Requirement (ggml b9581 call site) | Previous state | Now |
|---|---|---|
| `vkCreateQueryPool` (l.15693) | guest-local handle only, never sent to host | real Venus encode |
| `vkResetQueryPool` (l.15701, device-level, Vulkan 1.2) | **absent from proc table** → null-pointer call under Vulkan-Hpp | implemented + `EXT` alias |
| `vkCmdWriteTimestamp` (l.14030/15710) | no-op | real Venus encode into the recording stream |
| `vkGetQueryPoolResults` (l.16017, `e64\|eWait`) | always `VK_NOT_READY`, zeroed data | real reply round-trip (chunked through the 4 KB reply blob) |
| `limits.timestampPeriod` (l.16023) | 0.0 → all deltas multiply to zero | read from the host's real `VkPhysicalDeviceProperties` over the Venus reply path; 1.0 fallback |

Wire format source of truth: generated
`libs/libukvulkan_venus/generated/vn_protocol_driver_query_pool.h` (encoders
and reply layout `[u32 cmd][i32 VkResult][u64 array_size][blob]`), decoded with
the generated `vn_decode_*` over the `vn_cs_decoder` shim where structs are
involved (no hand-computed offsets for the properties struct).

### Toggle plumbing

- The bench appliance accepts an app arg: `ggml-vk-perf-logger[=freq]`
  (scanned from argv so it works with or without a `--` separator in the
  cmdline). It maps to `GGML_VK_PERF_LOGGER=1` (+ `_FREQUENCY`).
- Runner: `scripts/app-llama-vk.py --mode bench --perf-logger` appends the app
  arg, captures the `Vulkan Timings:` blocks from the console, and writes
  `results/profile/ggml_vk_perf.json` alongside the normal bench result.
- Make target: `make profile-vk-bench-perf-logger`.

Linux-guest comparison run: plain `GGML_VK_PERF_LOGGER=1 llama-bench` inside
the guest (Mesa Venus implements query pools natively) — no tooling needed.

## Phase 3 — guest-side instrumentation (VOGUE stack counters)

**Status: Candidate only — implement only what Phase 1 cannot answer.**

Lightweight cycle counters (`rdtsc` / `ukplat_monotonic_clock`) at the choke
points, dumped as one aggregate line at bench exit and parsed into
`results/profile/*.json`:

- `libukvulkan_venus`: ring submit count, encode time + bytes, fence-wait time
  per `vkQueueSubmit`.
- `libukvirtio_gpu`: kick→IRQ completion latency, SUBMIT_3D count, blob
  map/unmap counts.

Fallback for event ordering (vs aggregates): `CONFIG_LIBUKDEBUG_TRACEPOINTS` +
gdb extraction.

## Phase 4 — host-native encoder microprofiling

**Status: Candidate only — runnable on the dev box (no QEMU/GPU needed).**

The fake-backend test suite (`tests/`, `make test-fast`) already runs the real
driver encode path host-native. Add an encode-heavy microbench binary and
profile with `perf` / callgrind for instruction-level hotspots in the Venus
encoder (memcpy, per-command overhead, allocation churn).

## Phase 5 — synthesis and evidence gates

Produce a per-token time budget table — encode / kick / wait / GPU exec /
completion — Unikraft vs Linux guest, rank deltas, and map each to an
optimization item (async submission, command batching, fence strategy, P0 SMP).

Stop condition (same as `plan-optimize.md`): no attribution claim without a
checked-in `results/profile/*.json` artifact from the evaluation host, and
`make test-fast` must stay green after any guest-side change.

## Execution order

1. Phases 1 + 2 on the evaluation host (one session, both guests) — expected
   to answer most of questions 1–3.
2. Phase 3 only for whatever Phase 1 leaves unattributed.
3. Phase 4 opportunistically on the dev box.
