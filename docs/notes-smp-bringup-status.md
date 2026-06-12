# A3 SMP bring-up — status, findings, and plan (evidence-based)

Branch: `smp-a3-per-lcpu-scheduler`. Unikraft WIP commits in `.deps/src/unikraft` (latest `af34014`).
This note is the authoritative status for the A3 kernel work. Written per the "plan → work → plan → work" discipline.

## What the research established (so we don't reinvent or miss an upstream mechanism)

Checked official docs, the **staging** branch, GSoC, and SMP literature (Linux/Zephyr):

1. **The correct/latest lib IS `uklcpu`** (`uk_lcpu_start/run/wait/wakeup/get_current`, `uk_lcpus`) + `ukpcpuvar` + `uksched`/`ukschedcoop`. That is exactly what this work uses. (Renamed from the old `ukplat_lcpu_*`.)
2. **No SMP scheduler exists on any branch.** `staging/lib/ukschedcoop/schedcoop.c` still has the verbatim guard `"We only support one processing LCPU (for now)"` (line 274). No `ukschedpreem`, no per-LCPU run-queue scheduler in 0.21.0 **or** staging.
3. **The allocator is not SMP-safe on any branch.** `staging/lib/ukallocbbuddy/bbuddy.c` has no spinlock; `lib/ukalloc/alloc.c:141` admits "…locking support yet. Eventually, this should probably be replaced…".
4. **x86_64 SMP is "on-going work"** upstream (ARM64 is further along). GSoC'22 added SMP-safe **sync primitives** (mutex/semaphore/rwlock/condvar) as the *foundation* for full SMP — full SMP of core components is not done.
5. **There is no in-tree caller of `uk_lcpu_start`** — the "AP runs a full OS context" path is essentially unexercised in 0.21.0/staging.

**Conclusion:** running llama threads concurrently on multiple x86 vCPUs requires making the scheduler per-LCPU **and** making every shared subsystem SMP-safe. Upstream has not done this; it is genuinely research-grade, multi-subsystem work — not a missing flag or lib.

## What now works (verified milestone)

x86 AP bring-up under our per-LCPU design **boots without faulting**, after fixing three real platform issues (each diagnosed from the actual triple-fault traces, mirroring how Linux brings up secondary CPUs):

- **Low bootstrap stack** for each AP (static `.bss`, identity-mapped in both the boot and runtime page tables). The heap/stack allocators are vmem-backed at high virtual addresses the boot page table can't reach. (cf. Linux's low trampoline stack.)
- **AP adopts the runtime page table** (`CR3`) in `ap_entry`, after first matching **`EFER.NXE`** — the trampoline sets only `EFER.LME`, but the runtime page table marks data pages NX, so an AP with NXE clear takes a reserved-bit #PF. (Found from `e=0x8` page-fault error code.)
- **SMP-safe bbuddy allocator** (added a `__spinlock` around `palloc`/`pfree`).

Result: `-smp 4` now boots with **0 triple faults**; the BSP prints its banner; the per-LCPU bring-up driver runs.

Files (in `.deps/src/unikraft`, to be exported as `patches/unikraft/0002-*.patch`):
- `lib/ukschedcoop/smp.c` (new) — per-LCPU bring-up, AP entry, round-robin picker
- `lib/ukschedcoop/schedcoop.{c,h}` — `__spinlock` per-instance run queue
- `lib/ukallocbbuddy/bbuddy.c` — SMP-safe palloc/pfree
- `lib/ukboot/boot.c`, `lib/posix-process/clone.c` — bring-up hook + placement hook
- `lib/ukschedcoop/{Config.uk,Makefile.uk,exportsyms.uk,include/uk/schedcoop.h}`

## The remaining blocker (precisely characterised)

With APs running concurrently, the **C++ app init on the BSP crashes in `std::unordered_set::emplace`** (heap-corruption-shaped: `rdx=0x1fff…`), at ~0.12 s, **even with thread placement disabled and the allocator locked**. So at least one *more* shared subsystem is corrupted once secondary vCPUs execute concurrently. Candidates (each must be audited/locked): the timer/IRQ path, `ukvmem`/`ukallocstack` VMA structures, `ukrandom`, and the libc/libcxx runtime's global state. This matches the upstream reality that *core components are not yet SMP-safe*.

## Plan / options (decision needed)

**Option 1 — Constrained, demonstrable A3 (recommended, tractable).**
Prove the per-LCPU scheduler executes work on every vCPU with a *minimal in-kernel SMP smoke appliance* (no C++ app), avoiding the un-hardened subsystems. Deliverable: boot log showing a function running on LCPU 0..N with correct per-CPU identity. This demonstrates the A3 mechanism works on x86 (a real result beyond what upstream exercises) without requiring full SMP-safety of the whole stack.

**Option 2 — Full A3 for llama (research-grade, long).**
Audit and make SMP-safe, one subsystem at a time (timer/IRQ, vmem, random, libc runtime, …), re-testing each, until the llama CPU appliance runs threads concurrently. This is effectively completing what upstream calls "on-going work"; expect many build/boot/debug cycles and possibly upstreamable patches.

**Option 3 — Pivot to Option B (app-side `uk_lcpu_run` offload) for llama specifically.**
Keep APs parked in the default IPI handler and dispatch ggml compute via `uk_lcpu_run` run-to-completion, avoiding the per-LCPU scheduler + concurrent-allocation model. Smaller surface for the *llama* goal, but also needs concurrency audit of the ggml worker path.

## Verification done so far
- `-smp 1`: boots to the app (reaches 9pfs mount) — baseline OK.
- `-smp 4` pre-fix: triple-faulted in `ap_entry` (high-vaddr stack).
- `-smp 4` post-fix: 0 triple faults, BSP banner prints, APs start; crashes later in BSP C++ app init (shared-state corruption).

---

## FINAL STATUS (2026-06-12) — A3 functional; perf win not yet achieved

**The "crash" saga was a TCG emulation artifact.** The `unordered_set` crash only happened under `qemu accel=tcg`; under **KVM** everything boots. Always test SMP under KVM.

**Achieved (KVM `-smp 4`, CPU appliance):**
- 4 per-LCPU cooperative `ukschedcoop` instances come online ("SMP: scheduler online on LCPU 1/2/3", "brought up 3 secondary scheduler(s)"); app boots and the bench PASSES — correctness preserved. Evidence: `docs/results-smp/a3-cpu-smp4-schedulers-online-kvm.log`.
- Real x86 AP bring-up working (beyond what upstream exercises): low `.bss` bootstrap stack; AP adopts runtime CR3 after matching `EFER.NXE`; `uk_lcpu_init()` in the custom AP entry; SMP-safe bbuddy allocator; per-instance schedcoop run-queue spinlock.

**Measured (real 806 MB model):** smp4 is **slower**, not faster: pp512 31.0→21.3 (0.69x), tg128 10.7→7.8 (0.73x). Host has 56 cores (not oversubscribed).

**Why no speedup (root-caused):** the ggml worker threads are **not actually distributed** to the AP schedulers. Instrumenting `schedcoop_thread_add` showed **0 placement calls** during the bench — `libpthread_embedded` (a fetched KraftKit package) creates threads via a path that bypasses `clone.c`/`uk_sched_thread_add`/`schedcoop_thread_add`, so all workers stay on the BSP (4 threads on 1 vCPU) while the 3 APs idle, and the extra SMP machinery just adds overhead.

**Path to an actual speedup (remaining work):**
1. Intercept thread placement on the embedded-pthread creation path (need that package's source / the right `uk_sched_thread_create` hook), or apply CPU affinity there, so the N ggml workers land one-per-vCPU.
2. Keep workers co-scheduled (polling threadpool — already added to `bench.cpp`).
3. Address cooperative cross-LCPU co-scheduling/wakeup overhead (tight ggml barriers) — may need a gang/affinity-aware policy.

This is genuinely research-grade work that Unikraft upstream itself has not completed (x86 SMP "on-going work"; no SMP scheduler or SMP-safe allocator on any branch).
