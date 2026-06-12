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

**Why no speedup (current evidence, corrected 2026-06-12):** the earlier
conclusion that a fetched `libpthread_embedded` implementation bypassed
`clone.c` is stale and contradicted by the current build. The llama CPU object
has an undefined `pthread_create`; `libmusl.ld.o` provides
`__pthread_create`/`pthread_create` and references `__clone`;
`libmuslglue.ld.o` provides `__clone`. The x86-64 wrapper calls
`uk_syscall_e_clone`, and the final image contains `uk_syscall_r_clone` and
`uk_clone`. Therefore the current call path is:

```text
ggml_thread_create
  -> musl pthread_create
  -> libmuslglue __clone
  -> uk_syscall_e_clone / uk_syscall_r_clone
  -> uk_clone
  -> uk_sched_thread_add
```

The old observation that instrumentation reported zero
`schedcoop_thread_add` placement calls must be re-measured against the current
musl-only image; it cannot be used as proof that pthread bypasses clone.
The measured slowdown remains valid, but its precise runtime cause is now
**open**. The next run must trace the scheduler selected in `uk_clone`, the
target LCPU in `schedcoop_thread_add`, and the LCPU on which each ggml worker
actually starts. Likely remaining causes include a placement/registry defect,
remote wakeup or cooperative barrier overhead, or another SMP-unsafe shared
subsystem.

**Path to an actual speedup (remaining work):**
1. Instrument the current musl path at `uk_clone`,
   `schedcoop_thread_add`, and the ggml worker entry to prove whether workers
   land one-per-vCPU.
2. If placement is wrong, fix `uk_schedcoop_smp_pick()` or the per-LCPU
   scheduler registry; do not add another pthread-specific creation path.
3. Keep workers co-scheduled (polling threadpool — already added to `bench.cpp`).
4. Address cooperative cross-LCPU co-scheduling/wakeup overhead (tight ggml barriers) — may need a gang/affinity-aware policy.

**References and reproducible checks:**
- Unikraft `lib-musl` states that musl is the recommended libc, provides
  native thread support, and must not be built with `pthread-embedded`:
  https://github.com/unikraft/lib-musl#readme
- ggml maps `ggml_thread_create` to `pthread_create` and creates secondary
  graph workers with it:
  `.deps/src/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c:435,465,3286`.
- The musl x86-64 clone wrapper calls `uk_syscall_e_clone`:
  `.unikraft/libs/musl/arch/x86_64/__clone.S:12-67`.
- The project patch selects `uk_schedcoop_smp_pick()` in `uk_clone` and then
  calls `uk_sched_thread_add(s, child)`:
  `.deps/src/unikraft/lib/posix-process/clone.c:145-154,410-411`.
- Upstream Unikraft `RELEASE-0.21.0` instead selects
  `uk_sched_current()` before the same `uk_sched_thread_add` call:
  https://github.com/unikraft/unikraft/blob/RELEASE-0.21.0/lib/posix-process/clone.c
- Reproduce symbol resolution with:

  ```sh
  nm -An .unikraft/build/appllama_cpu.ld.o \
    .unikraft/build/libmusl.ld.o \
    .unikraft/build/libmuslglue.ld.o \
    .unikraft/build/vogue-llama-cpu_qemu-x86_64.dbg |
    grep -E 'pthread_create|__clone|uk_syscall_r_clone|uk_clone'
  ```

This is genuinely research-grade work that Unikraft upstream itself has not completed (x86 SMP "on-going work"; no SMP scheduler or SMP-safe allocator on any branch).

## pthread placement trace baseline (2026-06-12)

Evidence: `docs/results-smp/pthread-affinity-before.log`.

- build result: `make llama-cpu-bench-build` exited 0
- worker clone count: 0
- selected target LCPUs: not observed; no worker clone reached `uk_clone`
- enqueue target LCPUs: not observed; no worker enqueue reached `schedcoop_thread_add`
- actual execution LCPUs: not yet observable

The run observed four `SMPPLACE online` records for LCPUs 0, 1, 2, and 3.
The repository model fixture is not a valid runnable GGUF model, so model
loading failed before ggml worker creation. This run therefore proves scheduler
bring-up tracing only; it does not prove worker placement or execution.
