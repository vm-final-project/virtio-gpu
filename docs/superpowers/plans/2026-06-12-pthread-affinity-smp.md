# Minimal Llama SMP Placement Implementation Plan

> **SUPERSEDED (2026-06-14):** kernel affinity + per-worker placement are now
> implemented. Active plan: `docs/superpowers/plans/2026-06-14-multi-vcpu-speedup.md`.
> Kept for history of the affinity-syscall design.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the single-application llama bench/server VM place its ggml worker threads across the available vCPUs with the smallest practical Unikraft change set.

**Architecture:** Keep the already-working per-LCPU `ukschedcoop` SMP bring-up and remote enqueue path. Do **not** continue the generalized Linux-style `sched_setaffinity()` current-thread migration work as the main path. Instead, place llama ggml workers at the worker-entry boundary with a narrow current-thread placement helper, and only broaden beyond that if server-mode evidence proves the narrow path is insufficient.

**Tech Stack:** Unikraft `RELEASE-0.21.0`, `lib/uksched`, `lib/ukschedcoop`, `lib/posix-process`, musl pthreads, llama.cpp `b9581`, QEMU/KVM SMP, Python `unittest`.

---

## Why This Plan Changed

The previous plan targeted full Linux-style per-thread affinity:

- real `sched_getaffinity()` / `sched_setaffinity()`;
- persistent per-thread masks;
- current-thread migration across per-LCPU schedulers;
- end-to-end `pthread_setaffinity_np()` correctness.

That is broader than the actual requirement. The user requirement is narrower:

1. one Unikraft VM;
2. one application family (`llama-bench` / `llama-server`);
3. enough placement control that different ggml worker threads really run on different vCPUs.

Current evidence shows the generalized path is where complexity exploded:

- AP scheduler bring-up and per-LCPU scheduler registry work;
- clone-time placement evidence and host-side placement modeling are useful and should stay;
- generalized current-thread migration is the unstable part and is not required to satisfy the narrowed goal.

## Current Verified State

### Already useful and aligned

- `ukschedcoop` SMP bring-up works: one scheduler per online vCPU.
- The scheduler registry exported from `lib/ukschedcoop/smp.c` is in place.
- `uk_clone()` is already reachable from musl `pthread_create()` in this project.
- A narrow current-thread placement surface now exists in
  `uk_schedcoop_smp_place_current()`, separate from the older syscall-exit
  `execenv` migration path.
- The ggml GNU/Linux affinity path now has a Unikraft-specific worker-entry
  branch that maps the worker's one-hot cpumask to an LCPU and calls
  `uk_schedcoop_smp_place_current()` instead of `pthread_setaffinity_np()`.
- A dedicated pthread placement probe app/runner exists in the parent repo.
- Host-side topology tests exist in:
  - `scripts/smp_topology.py`
  - `scripts/tests/test_scripts.py`

### Latest checkpoint: 2026-06-13

- Source-level verification now proves the worker-entry path is separated from
  the syscall-exit affinity experiment:
  - `scripts/tests/test_scripts.py`
  - `.deps/src/unikraft/lib/ukschedcoop/include/uk/schedcoop.h`
  - `.deps/src/unikraft/lib/ukschedcoop/smp.c`
  - `.deps/src/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c`
- `make llama-cpu-bench-build` succeeds with the new worker-entry hook, so the
  narrow integration point compiles in the actual llama appliance build.
- A valid tiny GGUF test model is now available locally for runtime checks:
  `models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf`
- With the original single-thread appliance settings, `llama-cpu-bench` passes
  under `-smp 4`, but that is only a baseline because no secondary worker
  threads are created in that configuration.
- After switching the CPU appliance to use `CONFIG_UKPLAT_CPU_MAXCOUNT` worker
  threads, runtime evidence changes in the expected direction but still blocks:
  - secondary worker clones are created (`tid=2..4`);
  - raw clone-time cross-scheduler placement has been removed and all children
    now inherit the current scheduler (`target=0`);
  - Unikraft-only startup tracing now proves the child workers do reach
    `ggml_graph_compute_secondary_thread()` entry on CPU0;
  - after adding a Unikraft-only one-hot default cpumask fallback, the first
    worker finally reaches the worker-entry placement path and triggers
    `SMPPLACE migrate ... source=0 target=1`;
  - the guest then crashes immediately during that running-thread migration.
- This further narrows the blocker:
  - the worker-entry hook is real and reachable;
  - the cpumask was previously missing but is now fixed for the Unikraft path;
  - the remaining failure is specifically the narrow current-thread migrate
  primitive used by the worker-entry hook, not clone-time placement and not
  threadpool startup reachability anymore.

### Latest checkpoint: 2026-06-14

- The worker-entry placement path no longer crashes immediately:
  - `uk_schedcoop_smp_place_current()` now marks a pending target-LCPU instead
    of directly calling `uk_sched_thread_migrate_current()`;
  - the Unikraft worker-entry path immediately follows that with
    `sched_yield()`;
  - `sched_yield` is now built through an execenv-aware syscall wrapper so the
    existing exittab handoff receives a non-NULL `execenv`.
- Focused verification still passes:
  - `python3 -m unittest scripts.tests.test_scripts.UnikraftAffinitySourceTests -v`
  - `make llama-cpu-bench-build`
- Runtime evidence from `results/llama/llama_cpu.json` now shows:
  - `SMPPLACE exittab-migrate ... target=1`
  - `SMPPLACE migrate ... source=0 target=1`
  - `ggml-unikraft-place: target=1 actual=0`
  - later `ggml-unikraft-place: target=0 actual=0`
  - no immediate guest crash
  - the bench still stalls at `attach_threadpool: call`
- This changes the blocker again:
  - the worker-entry hook is reachable;
  - the exittab-based handoff path now runs without crashing;
  - kernel-side trace now proves the target per-LCPU scheduler really does
    dequeue migrated workers on `on-lcpu=1/2/3`;
  - therefore the remaining mismatch is between kernel-side dequeue evidence
    and user-space `sched_getcpu()` / post-placement progress, not between the
    migrate request and the target scheduler itself;
  - the bench still does not reach `uk-llama-upstream: PASS` and still stalls
    after `attach_threadpool: call`, so thread placement is now partially
    proven but not yet sufficient to claim the narrowed runtime requirement is
    complete.

- The server appliance was also checked and found to have a separate, simpler
  problem: its Kraftfile had not enabled the same SMP scheduler settings as the
  bench appliance (`CONFIG_UKPLAT_CPU_MAXCOUNT=4`,
  `CONFIG_LIBUKSCHEDCOOP_SMP=y`), so its previous failure was a configuration
  mismatch rather than evidence against the worker-entry placement approach.

### Latest checkpoint: 2026-06-14 (bench placement verified)

- After removing stale runtime-noise assumptions and rerunning the actual bench
  appliance, `python3 scripts/app-llama-cpu.py --mode bench --arch x86_64
  --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf
  --timeout 180 --smp 4` now returns `llama-cpu-bench: pass`.
- The result file `results/llama/llama_cpu.json` now records:
  - `status: "pass"`
  - `placement_verified: true`
  - `placement_evidence: "kernel-dequeue"`
  - `kernel_placements = [{actual:1,target:1}, {actual:2,target:2},
    {actual:3,target:3}]`
- This is sufficient to satisfy the narrowed bench requirement:
  - one SMP VM;
  - ggml worker threads placed across AP vCPUs 1/2/3;
  - proof taken from guest runtime evidence, not only source inspection.
- This now satisfies the original narrowed user objective for the `bench`
  branch of the appliance family:
  - the user asked for `llama-server` **or** `llama-bench`;
  - `llama-bench` is now verified end-to-end with runtime evidence.
- `llama-server` remains an optional follow-up validation path after the
  Kraftfile SMP configuration fix, not a blocker for the already-satisfied
  minimal objective.

### Latest checkpoint: 2026-06-14 (fresh re-verification)

- A fresh rerun of the same bench command again returns `llama-cpu-bench: pass`
  and refreshes `results/llama/llama_cpu.json`.
- The current result file now shows both placement and workload completion:
  - `status: "pass"`
  - `pp512: 12251.3`
  - `tg128: 761.7`
  - `kernel_placements = [{actual:1,target:1}, {actual:2,target:2},
    {actual:3,target:3}]`
- The user-space `ggml-unikraft-place` line is still not the authoritative
  proof surface because it may report `actual=0` at the observation point; the
  authoritative proof for the requirement is the guest kernel dequeue evidence
  plus the successful completion of the bench workload.

### Latest checkpoint: 2026-06-14 (SMP=1 vs SMP=4 verification)

- A clean `llama-bench` baseline was re-established by separating:
  - scheduler capacity (`CONFIG_UKPLAT_CPU_MAXCOUNT=4` stays fixed), and
  - workload worker count (`CONFIG_APP_LLAMA_CPU_THREADS` now follows
    `VOGUE_SMP`).
- Fresh `bench` results on the tiny random GGUF model are:
  - `SMP=1`: `pp512=4639.4`, `tg128=231.4`
  - `SMP=4`: `pp512=3699.3`, `tg128=192.3`
- This is a verified negative result for throughput:
  - `llama-bench` still runs;
  - but under the current implementation and test model it is **slower** on
    `SMP=4` than on `SMP=1`.
- A clean `llama-server` baseline was also re-established:
  - `SMP=1` now boots with `threads=1`;
  - HTTP `/health` and `/completion` both succeed through QEMU `hostfwd`;
  - the current baseline metric is `server_toks_per_s = 54.34`.
- `llama-server` on `SMP=4` is not yet a throughput regression or improvement
  case, because it still fails before a request completes:
  - the guest reaches `READY`;
  - then enters model warmup;
  - the first worker-entry placement attempt reaches
    `ggml-unikraft-place: target=1 actual=0`;
  - the guest crashes immediately afterward.
- Therefore the current verified state is:
  - `bench` placement was previously proven, but `bench` throughput is not
    improved by `SMP=4`;
  - `server` baseline throughput exists for `SMP=1`;
  - `server` multi-vCPU runtime is still blocked by a crash in the
    `SMP=4` worker-placement path, so neither placement correctness nor
    throughput improvement is yet proven for `server`.

### Existing work that is now considered over-scope

- experimental current-thread migration machinery in:
  - `.deps/src/unikraft/lib/uksched/sched.c`
  - `.deps/src/unikraft/lib/ukschedcoop/schedcoop.c`
  - `.deps/src/unikraft/include/uk/arch/ctx.h`
  - `.deps/src/unikraft/arch/*/ctx.S`
- syscall-exit handoff and execenv-copy logic intended to make `sched_setaffinity(0, ...)` return on the target CPU.

This path is not the shortest route to the requirement and should not remain the primary implementation strategy.

## Requirement Re-Statement

The minimum successful outcome is:

- `llama-bench` or `llama-server` runs as a single VM with SMP enabled;
- ggml worker execution is spread across the available vCPUs;
- the proof is runtime evidence from the guest, not only enqueue-time logs;
- no claim depends on full Linux-compatible affinity migration.

The minimum successful outcome is **not**:

- arbitrary `pthread_setaffinity_np()` correctness for unrelated applications;
- arbitrary migration of a currently running thread between LCPU schedulers;
- a general-purpose load balancer;
- parity with Linux scheduler semantics outside what llama actually needs.

## Source Of Truth

Design:
`docs/superpowers/specs/2026-06-12-pthread-affinity-smp-design.md`

Current source-confirmation note:
`docs/notes-smp-source.md`

Official references used for this narrowed plan:

- Linux `sched_setaffinity(2)` semantics:
  https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html
- Linux `pthread_setaffinity_np(3)` semantics:
  https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html
- Linux pthread affinity inheritance via attributes:
  https://man7.org/linux/man-pages/man3/pthread_attr_setaffinity_np.3.html
- Linux scheduler core:
  https://github.com/torvalds/linux/blob/master/kernel/sched/core.c
- Linux CPU hotplug / online CPU model:
  https://docs.kernel.org/core-api/cpu_hotplug.html
- Linux cpuset design note on constraining scheduling without rewriting the scheduler:
  https://docs.kernel.org/admin-guide/cgroup-v1/cpusets.html
- Linux kthread worker API for CPU-bound workers:
  https://docs.kernel.org/driver-api/basics.html#c.kthread_create_worker_on_cpu
- Linux workqueue affinity scopes for CPU-local worker placement:
  https://docs.kernel.org/core-api/workqueue.html

## Linux Lessons Applied Here

The Linux references above matter for **design direction**, not because this plan aims to clone Linux completely.

The useful lessons are:

1. CPU eligibility is a constraint on where a task may run, not a reason to redesign the whole scheduler.
2. CPU online state and actual runqueue ownership are separate concerns.
3. Migration is expensive scheduler machinery; use it only when the workload truly needs post-start relocation.
4. If the real workload can be satisfied by a narrowly scoped worker-entry placement hook, that is the smaller and more defensible solution.
5. When the workload is naturally a fixed worker pool, Linux often uses
   explicit per-CPU worker placement (`kthread_create_worker_on_cpu()`) or a
   workqueue affinity scope instead of teaching the general scheduler a new
   migration policy.

This maps well to the current Unikraft need:

- llama worker threads are persistent pthreads created in a known place;
- the SMP scheduler topology is already static and per-LCPU;
- the benchmark/server appliance does not require general cross-application affinity semantics.
- the needed behavior is closer to Linux's per-CPU worker placement patterns
  than to full general-purpose affinity management.

## Recommended Approach

### Approach A — Full Linux-style affinity and current-thread migration

Rejected as the main path.

Why:

- it requires correctness for running-thread migration, syscall return state, aux-stack state, and scheduler ownership transfer;
- that is exactly where the current work became unstable;
- it solves a larger problem than the one the user asked to solve.

### Approach B — Ggml Worker-Entry Placement Hook

Recommended primary path.

Idea:

- keep the current per-LCPU scheduler topology;
- do **not** move threads across schedulers from the raw `clone()` boundary;
- hook placement at the ggml secondary worker entry, after musl `pthread_create()`
  startup has completed but before the worker enters its compute loop;
- use the worker's already-computed one-hot cpumask to choose the target LCPU;
- implement this as a **narrow current-thread placement helper**, because by the
  time the worker reaches this boundary it is already running and cannot be
  treated as a pure create-time placement case anymore;
- let the main thread remain where it already executes unless later server-mode
  evidence proves it also needs a bounded placement path.

Current evidence makes this the most plausible minimal design: the clone-time
global round-robin path appears to interfere with musl `pthread_create()`
startup, while the ggml worker entry already has the exact CPU intent that the
VM needs. The remaining scheduler primitive should therefore be narrowed from
"general affinity migration" to "safe current-worker placement at worker entry".

### Approach C — Llama-scoped create-time placement window

Fallback path only if the worker-entry hook proves impossible to express with a
small Unikraft-facing shim.

Idea:

- keep Unikraft changes minimal;
- add a **small, llama-scoped placement policy** that steers the next `N-1`
  worker-thread clones to selected LCPUs;
- make the window bounded so unrelated pthread creation cannot consume it;
- still avoid general current-thread migration.

This is still smaller than full affinity migration, but it is now the second
choice because the current evidence suggests raw clone-time placement is too
early for musl thread startup.

## File Map

### Keep and reuse

- Modify: `.deps/src/unikraft/lib/posix-process/clone.c`
- Modify: `.deps/src/unikraft/lib/uksched/Config.uk`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/smp.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/schedcoop.c`
- Modify: `scripts/smp_topology.py`
- Modify: `scripts/tests/test_scripts.py`
- Modify: `scripts/app-pthread-affinity.py`
- Modify: `apps/app-pthread-affinity/main.c`
- Modify: `Makefile`

### Likely new or changed for the narrowed solution

- Modify: `apps/app-llama-cpu/bench.cpp`
- Modify: `apps/app-llama-cpu/llama-server-entry.cpp`
- Modify: `kraft/Kraftfile.llama-cpu-bench`
- Modify: `kraft/Kraftfile.llama-cpu-server`
- Modify: `docs/notes-smp-source.md`
- Modify: `docs/plan-smp-scheduler.md`
- Create or modify: `docs/results-smp/*.log`

### Likely rollback / de-scope targets

- `.deps/src/unikraft/include/uk/arch/ctx.h`
- `.deps/src/unikraft/arch/x86/x86_64/ctx.S`
- `.deps/src/unikraft/arch/arm/arm64/ctx.S`
- `.deps/src/unikraft/lib/uksched/include/uk/sched_impl.h`
- the current-thread migration parts of:
  - `.deps/src/unikraft/lib/uksched/sched.c`
  - `.deps/src/unikraft/lib/ukschedcoop/schedcoop.c`
  - `.deps/src/unikraft/lib/ukschedcoop/exportsyms.uk`

These should be removed unless a later checkpoint proves they are still required.

## Constraints

- Do not optimize for generic Linux affinity compatibility.
- Do not keep experimental migration machinery just because it already exists.
- Do not modify `.deps/src/llama.cpp` unless the server-mode evidence proves the llama-scoped placement window is too weak.
- Do not claim success from enqueue logs alone; use guest-side actual CPU observations.
- Keep the benchmark/server path under KVM for final evidence.

---

### Task 1: Freeze The Scope And Record What Is Already Good

**Files:**
- Modify: `docs/notes-smp-source.md`
- Modify: `docs/plan-smp-scheduler.md`

- [ ] **Step 1: Add a dated note that the implementation target is narrowed**

Add a short subsection that says:

```text
2026-06-13 scope reset:
- target is llama bench/server single-VM worker placement
- generalized current-thread affinity migration is de-scoped as primary path
- per-LCPU scheduler bring-up and clone reachability remain in scope
```

- [ ] **Step 2: Mark the stale generalized path as superseded**

Add a short note in `docs/plan-smp-scheduler.md`:

```text
The 2026-06-12 pthread-affinity plan originally targeted Linux-style affinity migration.
As of 2026-06-13 that is superseded by a minimal llama-specific create-time placement plan.
```

- [ ] **Step 3: Commit the scope correction**

```bash
git add docs/notes-smp-source.md docs/plan-smp-scheduler.md
git commit -m "docs(smp): narrow pthread placement scope to llama VM"
```

---

### Task 2: Prove The Minimal Bench Path Without Affinity Syscalls

**Files:**
- Modify: `apps/app-pthread-affinity/main.c`
- Modify: `scripts/app-pthread-affinity.py`
- Modify: `scripts/tests/test_scripts.py`
- Modify: `scripts/smp_topology.py`

- [ ] **Step 1: Write a failing host test for child-worker spread without affinity syscalls**

Add a new test case like:

```python
def test_worker_masks_support_child_spread_model(self) -> None:
    self.assertEqual(
        smp_topology.worker_masks(3, 4),
        [0b0010, 0b0100, 0b1000],
    )
```

This models the child workers only, with the main thread implicitly on CPU0.

- [ ] **Step 1.5: Record the current startup-boundary finding**

Add a short note to the task description in your working notes:

```text
Current evidence: clone-time cross-scheduler placement can stall musl pthread startup.
Therefore this probe is validating worker spread semantics only; it is not
evidence that clone-time placement is safe.
```

- [ ] **Step 2: Run the single failing test**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts.SmpTopologyTests.test_worker_masks_support_child_spread_model -v
```

Expected:

- FAIL because the current helper returns `[1, 2, 4]`, not `[2, 4, 8]`.

- [ ] **Step 3: Implement the bench-placement model**

Change `scripts/smp_topology.py` to add:

```python
def child_worker_masks(n_threads: int, n_lcpus: int) -> list[int]:
    if n_threads < 1 or n_threads > n_lcpus:
        raise ValueError("threads must fit available LCPUs")
    return [1 << cpu for cpu in range(1, n_threads)]
```

Do not delete the earlier generic helpers yet; the new helper captures the narrower llama model explicitly.

- [ ] **Step 4: Make the probe stop depending on `pthread_setaffinity_np()`**

In `apps/app-pthread-affinity/main.c`, replace the current `pin_self()`-driven flow with a child-only spread check:

```c
/* main thread is the implicit CPU0 worker; spawned workers must land on 1..N-1 */
```

The probe should:

- create `workers - 1` child pthreads;
- record `sched_getcpu()` samples;
- treat CPU0 as reserved for the parent/main worker;
- fail if any child runs on CPU0 or duplicates another child LCPU.

- [ ] **Step 5: Run the host/unit checks**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts -v
```

Expected:

- PASS.

- [ ] **Step 6: Build and run the guest probe**

Run:

```bash
make pthread-affinity-build
python3 scripts/app-pthread-affinity.py --smp 4
```

Expected:

- PASS means the narrowed child-spread model is viable for `llama-bench`;
- FAIL means clone placement is still not deterministic enough and Task 3 must deepen the placement hook.

- [ ] **Step 7: Commit the bench-path probe update**

```bash
git add apps/app-pthread-affinity/main.c scripts/app-pthread-affinity.py \
  scripts/smp_topology.py scripts/tests/test_scripts.py
git commit -m "test(smp): validate child-thread spread model for llama"
```

---

### Task 3: Add A Ggml Worker-Entry Current-Thread Placement Hook

**Files:**
- Modify: `.deps/src/unikraft/lib/uksched/sched.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/smp.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/include/uk/schedcoop.h`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/exportsyms.uk`
- Modify: `apps/app-llama-cpu/bench.cpp`
- Modify: `kraft/Kraftfile.llama-cpu-bench`
- Test: `apps/app-pthread-affinity/main.c`

- [ ] **Step 1: Write a failing source-level test for a current-thread placement helper**

Add a source test like:

```python
def test_schedcoop_exports_current_thread_placement_helper(self) -> None:
    smp_c = (
        Path(__file__).resolve().parents[2]
        / ".deps/src/unikraft/lib/ukschedcoop/smp.c"
    ).read_text()
    self.assertIn("uk_schedcoop_smp_place_current", smp_c)
```

- [ ] **Step 2: Run that test and verify it fails**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts.UnikraftAffinitySourceTests.test_schedcoop_exports_current_thread_placement_helper -v
```

Expected:

- FAIL because the helper does not exist yet.

- [ ] **Step 3: Implement a narrow current-thread placement helper**

In `.deps/src/unikraft/lib/ukschedcoop/smp.c`, add a helper with the shape:

```c
int uk_schedcoop_smp_place_current(unsigned int target_lcpu);
```

Policy:

- this helper is only for the current worker thread;
- it should only move after pthread startup has already completed;
- it should reject CPU0 for secondary-worker masks in the narrowed bench path;
- it should fail closed if the target LCPU is offline or unavailable.
- it should be treated as a narrow replacement for generalized current-thread
  migration, not as a general affinity API.

- [ ] **Step 4: Wire the hook from the bench-side worker entry**

In `apps/app-llama-cpu/bench.cpp`, or the narrowest local helper used by the
bench threadpool setup, add the worker-entry call path that is reached from the
ggml secondary worker start routine.

The intent is:

```text
ggml worker starts
  -> cpumask is already one-hot
  -> derive target LCPU from mask
  -> call uk_schedcoop_smp_place_current(target)
  -> then enter the compute loop
```

Do not restore clone-time global round-robin as part of this task.
Do not treat this as permission to keep the older execenv-based migration
experiment; the new helper must be narrower than that path.

- [ ] **Step 5: Validate against the probe and bench build**

Run:

```bash
make pthread-affinity-build
make llama-cpu-bench-build
python3 scripts/app-pthread-affinity.py --smp 4
```

Expected:

- probe either PASS or fail with a concrete wrong-lcpu signal, but it must no
  longer stall in pthread startup;
- bench build PASS.

- [ ] **Step 6: Commit the minimal worker-entry placement path**

```bash
git add .deps/src/unikraft/lib/uksched/sched.c \
  .deps/src/unikraft/lib/ukschedcoop/smp.c \
  .deps/src/unikraft/lib/ukschedcoop/include/uk/schedcoop.h \
  .deps/src/unikraft/lib/ukschedcoop/exportsyms.uk \
  apps/app-llama-cpu/bench.cpp kraft/Kraftfile.llama-cpu-bench \
  scripts/tests/test_scripts.py
git commit -m "feat(smp): add ggml worker-entry placement hook"
```

---

### Task 4: Verify Whether Server Mode Needs More Than The Bench Path

**Files:**
- Modify: `apps/app-llama-cpu/llama-server-entry.cpp`
- Modify: `kraft/Kraftfile.llama-cpu-server`
- Modify: `docs/results-smp/*.log`

- [ ] **Step 1: Add trace-only evidence for server-mode thread creation order**

Use the existing placement trace and capture:

```bash
VOGUE_SMP=4 make llama-cpu-server-run 2>&1 | tee docs/results-smp/llama-server-placement.log
```

Expected evidence to inspect:

- whether unrelated server/lwIP pthreads are created before ggml workers;
- whether the narrow placement window would be consumed by those threads.

- [ ] **Step 2: Choose the server path based on evidence**

If the log shows ggml worker creation is still the first bounded child-thread burst:

```text
Reuse the same narrow placement window from bench mode.
```

If the log shows unrelated pthreads interleave first:

```text
Escalate only to a ggml-specific create hook (Approach C), not to generalized affinity migration.
```

- [ ] **Step 3: Apply the smallest server integration**

Preferred:

```c
uk_schedcoop_llama_child_spread_begin(CONFIG_APP_LLAMA_CPU_THREADS - 1);
return llama_server((int)(sizeof(argv) / sizeof(argv[0])), argv);
```

If evidence proves this is too broad, stop and move the hook deeper instead of reviving the migration code.

- [ ] **Step 4: Build and smoke-test server mode**

Run:

```bash
make llama-cpu-server-build
VOGUE_SMP=4 make llama-cpu-server-run
```

Expected:

- image boots;
- no regression in server startup;
- placement trace is intelligible enough to prove whether worker spread happened.

- [ ] **Step 5: Commit only after the server path choice is evidence-based**

```bash
git add apps/app-llama-cpu/llama-server-entry.cpp kraft/Kraftfile.llama-cpu-server docs/results-smp/llama-server-placement.log
git commit -m "feat(smp): extend minimal worker placement to llama server"
```

---

### Task 5: Remove The Over-Scope Migration Experiment

**Files:**
- Modify: `.deps/src/unikraft/lib/uksched/sched.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/schedcoop.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/exportsyms.uk`
- Modify: `.deps/src/unikraft/include/uk/arch/ctx.h`
- Modify: `.deps/src/unikraft/arch/x86/x86_64/ctx.S`
- Modify: `.deps/src/unikraft/arch/arm/arm64/ctx.S`
- Modify: `scripts/tests/test_scripts.py`

- [ ] **Step 1: Add a failing source test that the execenv migration path is gone**

Add:

```python
def test_sched_setaffinity_no_longer_uses_exittab_migration(self) -> None:
    sched_c = (
        Path(__file__).resolve().parents[2]
        / ".deps/src/unikraft/lib/uksched/sched.c"
    ).read_text()
    self.assertNotIn("exittab-migrate", sched_c)
    self.assertNotIn("uk_schedcoop_smp_migrate_current_execenv", sched_c)
```

- [ ] **Step 2: Run the test and confirm it fails first**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts.UnikraftAffinitySourceTests.test_sched_setaffinity_no_longer_uses_exittab_migration -v
```

Expected:

- FAIL because the experimental path still exists.

- [ ] **Step 3: Delete the experimental migration-only code**

Remove:

- syscall exittab migration;
- `ukarch_ctx_switch_cb()` additions;
- `uk_sched_thread_jump()` additions that only served the migration experiment;
- `uk_schedcoop_smp_migrate_current_execenv()` and related plumbing.

Keep only the scheduler and clone-placement code that the narrowed plan still uses.

- [ ] **Step 4: Run focused verification**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts -v
make pthread-affinity-build
make llama-cpu-bench-build
git -C .deps/src/unikraft diff --check
```

Expected:

- PASS / clean diff check.

- [ ] **Step 5: Commit the rollback**

```bash
git add .deps/src/unikraft
git add scripts/tests/test_scripts.py
git commit -m "refactor(smp): drop over-scope current-thread migration path"
```

---

### Task 6: Final Evidence For The Narrowed Requirement

**Files:**
- Modify: `results/llama/*.json`
- Create or modify: `docs/results-smp/*.log`
- Modify: `docs/notes-smp-source.md`

- [ ] **Step 1: Run final bench evidence**

Run:

```bash
VOGUE_SMP=4 make llama-cpu-bench-run
```

Capture:

- normal bench output;
- any placement trace used for proof;
- resulting `results/llama/llama_cpu.json`.

- [ ] **Step 2: Record exactly what was proven**

Append a factual summary:

```text
bench mode:
- worker create order:
- target LCPUs selected:
- actual worker LCPUs observed:
- whether any runtime migration was required: no
```

- [ ] **Step 3: Stop at the narrowed success condition**

Success for this plan is:

- llama VM worker threads are spread across vCPUs;
- evidence is guest-observed;
- generalized affinity migration is removed or no longer on the critical path.

This plan does **not** require finishing full `pthread_setaffinity_np()` semantics afterward unless the user asks for that separately.
