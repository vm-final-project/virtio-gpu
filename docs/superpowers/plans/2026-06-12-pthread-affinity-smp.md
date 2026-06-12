# Pthread Affinity SMP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Linux-style per-thread CPU affinity in the patched Unikraft scheduler so musl pthreads, including ggml workers, can be pinned to distinct per-LCPU cooperative schedulers and verified end to end.

**Architecture:** Keep the existing one-`ukschedcoop`-instance-per-LCPU design, but replace global clone-order placement with persistent affinity state owned by each `uk_thread`. Implement scheduler registry lookup, self-migration, real `sched_getaffinity`/`sched_setaffinity`, strict ggml CPU masks, and an independent pthread placement probe before measuring llama.cpp.

**Tech Stack:** Unikraft `RELEASE-0.21.0`, `lib/uksched`, `lib/ukschedcoop`, `lib/posix-process`, musl pthreads, C/C++17, Kraft/Kconfig, QEMU/KVM SMP, Python `unittest`, llama.cpp `b9581`.

---

## Source Of Truth

Design:
`docs/superpowers/specs/2026-06-12-pthread-affinity-smp-design.md`

Official references:

- Linux scheduler locking and affinity:
  https://github.com/torvalds/linux/blob/master/kernel/sched/core.c
- Linux online CPU mask:
  https://docs.kernel.org/core-api/cpu_hotplug.html
- `sched_setaffinity(2)` semantics:
  https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html
- pthread creation affinity:
  https://man7.org/linux/man-pages/man3/pthread_attr_setaffinity_np.3.html
- musl `pthread_create`:
  https://git.musl-libc.org/cgit/musl/tree/src/thread/pthread_create.c
- ggml CPU threadpool:
  https://github.com/ggml-org/llama.cpp/blob/master/ggml/src/ggml-cpu/ggml-cpu.c

## File Map

### Unikraft source, exported into one patch

- Modify `.deps/src/unikraft/lib/uksched/Config.uk`
  - affinity and placement trace Kconfig.
- Modify `.deps/src/unikraft/lib/uksched/include/uk/thread.h`
  - persistent per-thread affinity mask.
- Modify `.deps/src/unikraft/lib/uksched/include/uk/sched.h`
  - affinity policy and migration interfaces.
- Modify `.deps/src/unikraft/lib/uksched/thread.c`
  - initialize and inherit affinity state.
- Modify `.deps/src/unikraft/lib/uksched/sched.c`
  - mask validation, target selection, real affinity syscalls.
- Modify `.deps/src/unikraft/lib/ukschedcoop/Config.uk`
  - update SMP help text; add trace dependency.
- Modify `.deps/src/unikraft/lib/ukschedcoop/include/uk/schedcoop.h`
  - per-LCPU registry and migration declarations.
- Modify `.deps/src/unikraft/lib/ukschedcoop/exportsyms.uk`
  - export registry/migration helpers.
- Modify `.deps/src/unikraft/lib/ukschedcoop/smp.c`
  - registry readiness, scheduler/LCPU lookup, target migration.
- Modify `.deps/src/unikraft/lib/ukschedcoop/schedcoop.c`
  - queue-safe migration and placement trace.
- Modify `.deps/src/unikraft/lib/posix-process/clone.c`
  - trace current path, inherit affinity, later remove clone round-robin.
- Modify `.deps/src/unikraft/lib/posix-process/include/uk/process.h`
  - expose TID-to-thread lookup used by musl affinity wrappers.

### VOGUE application and tests

- Replace `scripts/smp_topology.py`
  - model affinity mask intersection and deterministic target selection.
- Modify `scripts/tests/test_scripts.py`
  - host tests for the affinity policy.
- Create `apps/app-pthread-affinity/Config.uk`
  - placement probe options.
- Create `apps/app-pthread-affinity/Makefile.uk`
  - build the probe.
- Create `apps/app-pthread-affinity/main.c`
  - guest correctness and concurrency probe.
- Create `kraft/Kraftfile.pthread-affinity`
  - SMP4 test image.
- Create `scripts/app-pthread-affinity.py`
  - run QEMU and parse probe output.
- Modify `scripts/tests/test_scripts.py`
  - runner command/output parsing tests.
- Modify `Makefile`
  - probe build/run targets and verification gate.
- Modify `apps/app-llama-cpu/Config.uk`
  - strict affinity and polling controls.
- Modify `apps/app-llama-cpu/bench.cpp`
  - configure ggml threadpool cpumask.
- Modify `apps/app-llama-cpu/llama-server-entry.cpp`
  - pass equivalent server CPU-mask options.
- Modify `kraft/Kraftfile.llama-cpu-bench`
  - enable strict affinity and optional placement trace.
- Modify `kraft/Kraftfile.llama-cpu-server`
  - same policy for server mode.

### Delivery and documentation

- Create `patches/unikraft/0003-pthread-affinity-smp.patch`
  - complete affinity change against `RELEASE-0.21.0`.
- Modify `config/deps.json`
  - register the patch.
- Modify `docs/plan-smp-scheduler.md`
  - mark old clone-round-robin tasks superseded.
- Modify `docs/notes-smp-bringup-status.md`
  - record new evidence and final diagnosis.
- Create `docs/results-smp/pthread-affinity-*.log`
  - placement evidence.
- Create `docs/results-smp/pthread-affinity-llama-*.json`
  - repeated benchmark results.

## Constraints

- Do not modify `.deps/src/llama.cpp`.
- Do not implement general load balancing, preemption, NUMA placement, or CPU
  hot-unplug.
- Do not return success from an ignored affinity operation.
- Do not claim speedup until trace-free repeated measurements beat SMP1.
- Run SMP guests under KVM; TCG results are diagnostic only.

---

### Task 1: Capture The Current Placement Failure

**Files:**
- Modify: `.deps/src/unikraft/lib/uksched/Config.uk`
- Modify: `.deps/src/unikraft/lib/posix-process/clone.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/schedcoop.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/smp.c`
- Modify: `kraft/Kraftfile.llama-cpu-bench`
- Create: `docs/results-smp/pthread-affinity-before.log`

- [ ] **Step 1: Add the trace Kconfig**

Add under `if LIBUKSCHED`:

```kconfig
config LIBUKSCHED_PLACEMENT_TRACE
	bool "Trace thread placement and migration"
	default n
	help
	  Emit machine-parseable SMPPLACE records for clone, enqueue,
	  migration, and actual execution. Enable only for correctness runs.
```

- [ ] **Step 2: Add stable trace records**

Use this exact schema:

```c
uk_pr_info("SMPPLACE clone tid=%d caller=%u target=%d sched=%p\n",
	   child_tid, caller_lcpu, target_lcpu, s);

uk_pr_info("SMPPLACE enqueue thread=%p caller=%u target=%u sched=%p\n",
	   t, caller_lcpu, c->lcpu_idx, s);

uk_pr_info("SMPPLACE online lcpu=%u sched=%p\n", lcpu_idx, s);
```

Guard every record with `#if CONFIG_LIBUKSCHED_PLACEMENT_TRACE`.
Do not log inside the scheduling hot loop.

- [ ] **Step 3: Enable tracing only in the diagnostic Kraftfile**

Add:

```yaml
CONFIG_LIBUKSCHED_PLACEMENT_TRACE: 'y'
```

- [ ] **Step 4: Build and run the current image**

Run:

```bash
make llama-cpu-bench-build
VOGUE_SMP=4 make llama-cpu-bench-run 2>&1 |
  tee docs/results-smp/pthread-affinity-before.log
```

Expected:

- build exits 0;
- four `SMPPLACE online` records;
- clone/enqueue records reveal whether ggml worker creation reaches the
  current round-robin path;
- benchmark may remain slower than SMP1.

- [ ] **Step 5: Record a factual diagnosis**

Append a short dated subsection to `docs/notes-smp-bringup-status.md` listing:

```text
worker clone count:
selected target LCPUs:
enqueue target LCPUs:
actual execution LCPUs: not yet observable / observed:
```

Do not infer actual execution from enqueue alone.

- [ ] **Step 6: Commit the evidence instrumentation**

```bash
git add docs/results-smp/pthread-affinity-before.log \
  docs/notes-smp-bringup-status.md kraft/Kraftfile.llama-cpu-bench
git commit -m "evidence(smp): trace current pthread placement path"
```

The `.deps/src/unikraft` edits remain for inclusion in the final exported
patch.

---

### Task 2: Replace Round-Robin Model Tests With Affinity Policy Tests

**Files:**
- Modify: `scripts/smp_topology.py`
- Modify: `scripts/tests/test_scripts.py`

- [ ] **Step 1: Write failing host tests**

Replace `SmpTopologyTests` with:

```python
class SmpTopologyTests(unittest.TestCase):
    def test_effective_mask_intersects_requested_and_online(self) -> None:
        self.assertEqual(
            smp_topology.effective_mask(0b1110, 0b0011),
            0b0010,
        )

    def test_empty_effective_mask_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            smp_topology.effective_mask(0b1000, 0b0011)

    def test_keep_current_owner_when_allowed(self) -> None:
        self.assertEqual(
            smp_topology.choose_target(0b1010, current=3),
            3,
        )

    def test_choose_lowest_allowed_when_current_disallowed(self) -> None:
        self.assertEqual(
            smp_topology.choose_target(0b1010, current=0),
            1,
        )

    def test_worker_masks_are_one_hot(self) -> None:
        self.assertEqual(
            smp_topology.worker_masks(4, 4),
            [0b0001, 0b0010, 0b0100, 0b1000],
        )

    def test_reject_more_workers_than_lcpus(self) -> None:
        with self.assertRaises(ValueError):
            smp_topology.worker_masks(5, 4)
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts.SmpTopologyTests -v
```

Expected: FAIL because `effective_mask`, `choose_target`, and `worker_masks`
do not exist.

- [ ] **Step 3: Implement the policy model**

Replace `scripts/smp_topology.py` with:

```python
"""Reference model for Unikraft pthread affinity placement."""
from __future__ import annotations


def effective_mask(requested: int, online: int) -> int:
    effective = requested & online
    if effective == 0:
        raise ValueError("affinity has no online CPU")
    return effective


def choose_target(effective: int, current: int) -> int:
    if effective & (1 << current):
        return current
    return (effective & -effective).bit_length() - 1


def worker_masks(n_workers: int, n_lcpus: int) -> list[int]:
    if n_workers < 1 or n_workers > n_lcpus:
        raise ValueError("workers must fit available LCPUs")
    return [1 << i for i in range(n_workers)]
```

- [ ] **Step 4: Run the focused and full host suites**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts.SmpTopologyTests -v
make test-fast
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/smp_topology.py scripts/tests/test_scripts.py
git commit -m "test(smp): model pthread affinity placement policy"
```

---

### Task 3: Add Per-Thread Affinity State

**Files:**
- Modify: `.deps/src/unikraft/lib/uksched/include/uk/thread.h`
- Modify: `.deps/src/unikraft/lib/uksched/include/uk/sched.h`
- Modify: `.deps/src/unikraft/lib/uksched/thread.c`
- Modify: `.deps/src/unikraft/lib/uksched/sched.c`

- [ ] **Step 1: Add compile-time mask helpers**

In `uk/thread.h`, add:

```c
#define UK_SCHED_CPU_MASK_WORDS \
	((CONFIG_UKPLAT_CPU_MAXCOUNT + (8 * sizeof(unsigned long)) - 1) / \
	 (8 * sizeof(unsigned long)))

struct uk_sched_cpu_mask {
	unsigned long bits[UK_SCHED_CPU_MASK_WORDS];
};
```

Add to `struct uk_thread` next to `sched`:

```c
struct uk_sched_cpu_mask affinity;
```

- [ ] **Step 2: Declare policy helpers**

Add to `uk/sched.h`:

```c
void uk_sched_affinity_fill(struct uk_sched_cpu_mask *mask);
int uk_sched_affinity_intersect(struct uk_sched_cpu_mask *dst,
				const struct uk_sched_cpu_mask *requested);
bool uk_sched_affinity_test(const struct uk_sched_cpu_mask *mask,
			    unsigned int lcpu_idx);
int uk_sched_affinity_target(const struct uk_sched_cpu_mask *mask,
			     int current_lcpu);
```

- [ ] **Step 3: Initialize affinity in one place**

In `_uk_thread_struct_init()`:

```c
struct uk_thread *parent = uk_thread_current();

if (parent)
	t->affinity = parent->affinity;
else
	uk_sched_affinity_fill(&t->affinity);
```

The BSP/AP bootstrap threads receive all currently configured LCPUs. Child
pthreads inherit the parent mask, matching Linux behavior.

- [ ] **Step 4: Implement pure mask operations**

Implement:

- zero/fill;
- requested-and-online intersection;
- bit test;
- keep-current-else-lowest target selection.

For this project, the online mask comes from the ready entries in the
`ukschedcoop` registry. Do not use `CONFIG_UKPLAT_CPU_MAXCOUNT` alone as proof
that an AP scheduler is ready.

- [ ] **Step 5: Compile with SMP1 and SMP4 configs**

Run:

```bash
make llama-cpu-bench-build
rg -n "affinity" .unikraft/build/libuksched*.cmd
```

Expected: build exits 0 and new scheduler code is compiled.

- [ ] **Step 6: Commit the kernel checkpoint**

```bash
git -C .deps/src/unikraft add lib/uksched
git -C .deps/src/unikraft commit -m \
  "feat(uksched): add persistent per-thread CPU affinity"
```

---

### Task 4: Expose A Ready Per-LCPU Scheduler Registry

**Files:**
- Modify: `.deps/src/unikraft/lib/ukschedcoop/include/uk/schedcoop.h`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/exportsyms.uk`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/smp.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/Config.uk`

- [ ] **Step 1: Replace the picker API**

Declare:

```c
unsigned int uk_schedcoop_smp_online_count(void);
struct uk_sched *uk_schedcoop_smp_sched(unsigned int lcpu_idx);
int uk_schedcoop_smp_lcpu(const struct uk_sched *sched);
int uk_schedcoop_smp_migrate_current(unsigned int target_lcpu);
```

Keep `uk_schedcoop_smp_pick()` temporarily for Task 1 comparison, but mark it
deprecated in its comment.

- [ ] **Step 2: Publish readiness after AP scheduler start**

Do not set `coop_by_lcpu[i]` as ready before the AP has:

1. initialized its LCPU state;
2. started its scheduler;
3. reached a point where remote enqueue is safe.

Use a per-slot ready flag or publish the pointer with release ordering. Registry
readers use matching acquire ordering.

- [ ] **Step 3: Implement registry lookup**

Required behavior:

```c
uk_schedcoop_smp_sched(out_of_range) == NULL
uk_schedcoop_smp_sched(offline_slot) == NULL
uk_schedcoop_smp_lcpu(NULL) == -EINVAL
uk_schedcoop_smp_lcpu(unknown_sched) == -ENOENT
```

`online_count()` returns ready scheduler count, not array span.

- [ ] **Step 4: Export the helpers**

Add the four symbols to `exportsyms.uk`.

- [ ] **Step 5: Update Kconfig wording**

Change `LIBUKSCHEDCOOP_SMP` help text from “round-robin newly created threads”
to:

```text
Run one cooperative scheduler instance per online vCPU and provide the
registry/migration support used by per-thread CPU affinity.
```

- [ ] **Step 6: Build and inspect symbols**

Run:

```bash
make llama-cpu-bench-build
nm .unikraft/build/libukschedcoop.ld.o |
  rg "uk_schedcoop_smp_(online_count|sched|lcpu|migrate_current)"
```

Expected: all four symbols defined.

- [ ] **Step 7: Commit**

```bash
git -C .deps/src/unikraft add lib/ukschedcoop
git -C .deps/src/unikraft commit -m \
  "feat(ukschedcoop): expose ready per-LCPU scheduler registry"
```

---

### Task 5: Implement Safe Self-Migration

**Files:**
- Modify: `.deps/src/unikraft/lib/uksched/include/uk/sched.h`
- Modify: `.deps/src/unikraft/lib/uksched/sched.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/schedcoop.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/smp.c`

- [ ] **Step 1: Add a scheduler migration callback**

Extend `struct uk_sched` with:

```c
typedef int (*uk_sched_thread_migrate_func_t)
	(struct uk_sched *source, struct uk_thread *thread,
	 struct uk_sched *target);

uk_sched_thread_migrate_func_t thread_migrate;
```

Add wrapper:

```c
int uk_sched_thread_migrate_current(struct uk_sched *target);
```

- [ ] **Step 2: Enforce supported migration scope**

The wrapper returns:

- `0` when target equals current scheduler;
- `-EINVAL` for null target;
- `-ENOTSUP` when source has no migration callback;
- `-EBUSY` for migration of a different running thread.

Only self-migration is implemented in this plan.

- [ ] **Step 3: Implement cooperative migration handoff**

The `ukschedcoop` callback must:

1. lock source and target queue locks in ascending `lcpu_idx`;
2. update scheduler ownership exactly once;
3. insert the current thread into the target run queue;
4. remove it from the source scheduler's ownership list;
5. wake the target LCPU;
6. mark the current source context non-runnable locally;
7. switch to another source scheduler thread;
8. resume the migrated thread only from the target scheduler.

Do not call the existing public `uk_sched_thread_remove()` followed by
`uk_sched_thread_add()`: those wrappers independently manipulate IRQ state and
lists and leave an interval with ambiguous ownership. Implement one atomic
migration operation under the ordered locks.

- [ ] **Step 4: Add invariants**

Before and after migration assert:

```c
UK_ASSERT(thread == uk_thread_current());
UK_ASSERT(thread->sched == source);
UK_ASSERT(source != target);
UK_ASSERT(!uk_thread_is_exited(thread));
```

After target resume:

```c
UK_ASSERT(uk_thread_current()->sched == target);
```

- [ ] **Step 5: Add trace records**

Under the trace Kconfig:

```c
uk_pr_info("SMPPLACE migrate thread=%p source=%u target=%u\n",
	   thread, source_idx, target_idx);
uk_pr_info("SMPPLACE resume thread=%p actual=%u sched=%p\n",
	   thread, actual_idx, thread->sched);
```

- [ ] **Step 6: Build before adding syscalls**

Run:

```bash
make llama-cpu-bench-build
```

Expected: PASS. No behavior changes yet because affinity syscalls still use
the old stubs.

- [ ] **Step 7: Commit**

```bash
git -C .deps/src/unikraft add lib/uksched lib/ukschedcoop
git -C .deps/src/unikraft commit -m \
  "feat(ukschedcoop): migrate current thread across LCPU schedulers"
```

---

### Task 6: Implement Real Affinity Syscalls

**Files:**
- Modify: `.deps/src/unikraft/lib/uksched/sched.c`
- Modify: `.deps/src/unikraft/lib/uksched/Config.uk`
- Modify: `.deps/src/unikraft/lib/posix-process/include/uk/process.h`
- Modify: `.deps/src/unikraft/lib/posix-process/process.c`
- Modify: `.deps/src/unikraft/lib/posix-process/Config.uk`

- [ ] **Step 1: Record the musl wrapper ABI**

Run:

```bash
rg -n "pthread_(get|set)affinity_np|sched_(get|set)affinity" \
  .unikraft/libs/musl
nm -An .unikraft/build/libmusl.ld.o |
  rg "pthread_(get|set)affinity_np|sched_(get|set)affinity"
```

Record in `docs/notes-smp-source.md` whether musl passes:

- `pthread_setaffinity_np()` passes the pthread descriptor's kernel TID to
  `sched_setaffinity`;
- `pthread_getaffinity_np()` passes the same TID to `sched_getaffinity`.

This is the musl contract, so positive-TID lookup is required.

- [ ] **Step 2: Add the public lookup function**

Expose:

```c
struct uk_thread *uk_posix_thread_from_tid(int tid);
```

It returns `NULL` for a missing TID and must not expose `struct posix_thread`.
Declare it in `lib/posix-process/include/uk/process.h`, implement it by wrapping
the existing internal `tid2ukthread()`, and export it through the library's
existing public-symbol mechanism.

- [ ] **Step 3: Replace `sched_getaffinity()` stub**

Required flow:

```c
thread = resolve_affinity_thread(pid);
if (!thread)
	return -ESRCH;
if (cpusetsize < sizeof(thread->affinity))
	return -EINVAL;
memset(mask, 0, cpusetsize);
memcpy(mask, &thread->affinity, sizeof(thread->affinity));
return sizeof(thread->affinity);
```

Use syscall return conventions already used by `UK_SYSCALL_R_DEFINE`.

- [ ] **Step 4: Replace `sched_setaffinity()` stub**

Required flow:

```c
thread = resolve_affinity_thread(pid);
validate_cpusetsize_and_pointer();
copy_requested_mask();
rc = uk_sched_affinity_intersect(&effective, &requested);
if (rc)
	return rc;
target = uk_sched_affinity_target(&effective, current_owner);
if (target < 0)
	return target;
thread->affinity = effective;
if (thread != uk_thread_current())
	return -ENOTSUP;
return uk_schedcoop_smp_migrate_current((unsigned int) target);
```

If migration fails, restore the previous affinity mask before returning.

- [ ] **Step 5: Remove both `UK_WARN_STUBBED()` calls**

Run:

```bash
rg -n "UK_WARN_STUBBED" .deps/src/unikraft/lib/uksched/sched.c
```

Expected: no affinity stub remains.

- [ ] **Step 6: Build and inspect final symbols**

Run:

```bash
make llama-cpu-bench-build
nm -An .unikraft/build/vogue-llama-cpu_qemu-x86_64.dbg |
  rg "sched_(get|set)affinity|pthread_(get|set)affinity_np"
```

Expected: syscall and pthread wrapper symbols resolve.

- [ ] **Step 7: Commit**

```bash
git -C .deps/src/unikraft add lib/uksched lib/posix-process
git -C .deps/src/unikraft commit -m \
  "feat(uksched): implement per-thread CPU affinity syscalls"
```

---

### Task 7: Build A Dedicated Pthread Affinity Probe

**Files:**
- Create: `apps/app-pthread-affinity/Config.uk`
- Create: `apps/app-pthread-affinity/Makefile.uk`
- Create: `apps/app-pthread-affinity/main.c`
- Create: `kraft/Kraftfile.pthread-affinity`
- Create: `scripts/app-pthread-affinity.py`
- Modify: `scripts/tests/test_scripts.py`
- Modify: `Makefile`

- [ ] **Step 1: Write runner parsing tests**

Add tests for a parser accepting:

```text
pthread-affinity: worker=0 requested=0x1 actual=0 samples=100000
pthread-affinity: worker=1 requested=0x2 actual=1 samples=100000
pthread-affinity: PASS workers=4 distinct=4
```

and rejecting:

```text
pthread-affinity: worker=1 requested=0x2 actual=0 samples=1
pthread-affinity: FAIL reason=wrong-lcpu
```

Run:

```bash
python3 -m unittest scripts.tests.test_scripts -v
```

Expected: FAIL until the runner exists.

- [ ] **Step 2: Implement the guest probe**

The probe must:

```c
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
```

For worker `i`:

1. build a one-hot `cpu_set_t`;
2. call `pthread_setaffinity_np(pthread_self(), sizeof(set), &set)`;
3. call `pthread_getaffinity_np()` and verify the returned bit;
4. wait on an atomic start barrier;
5. loop at least 100,000 times reading
   `uk_pcpuvar_current_get(uk_pcpuvar_cpu_idx)`;
6. fail immediately if actual LCPU differs from `i`;
7. increment a per-worker progress counter.

The main thread also participates as worker 0 or is explicitly pinned to LCPU
0 before releasing the barrier.

- [ ] **Step 3: Add negative cases**

Before the concurrency case, verify:

```c
CPU_ZERO(&set);
assert(pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == EINVAL);

CPU_ZERO(&set);
CPU_SET(CONFIG_UKPLAT_CPU_MAXCOUNT, &set);
assert(pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == EINVAL);
```

Use error checks and PASS/FAIL markers, not libc `assert()` if assertions are
disabled in release builds.

- [ ] **Step 4: Add build/run integration**

Add Make targets:

```make
pthread-affinity-build:
	$(KRAFT) build --kraftfile kraft/Kraftfile.pthread-affinity

pthread-affinity-run: pthread-affinity-build
	python3 scripts/app-pthread-affinity.py --smp 4
```

Follow the existing llama runner structure for QEMU selection, JSON result,
timeout handling, and blocked statuses.

- [ ] **Step 5: Run host tests**

Run:

```bash
make test-fast
```

Expected: PASS.

- [ ] **Step 6: Run SMP1 and SMP4 probes**

Run:

```bash
python3 scripts/app-pthread-affinity.py --smp 1
python3 scripts/app-pthread-affinity.py --smp 4
```

Expected:

- SMP1: one worker on LCPU0;
- SMP4: four worker lines with actual LCPUs `0,1,2,3`;
- `PASS workers=4 distinct=4`;
- no timeout or queue assertion.

- [ ] **Step 7: Run repeated create/join stress**

Configure 100 create/join rounds and run:

```bash
PTHREAD_AFFINITY_ROUNDS=100 \
  python3 scripts/app-pthread-affinity.py --smp 4
```

Expected: PASS with no stale ownership or double-enqueue assertion.

- [ ] **Step 8: Commit**

```bash
git add apps/app-pthread-affinity kraft/Kraftfile.pthread-affinity \
  scripts/app-pthread-affinity.py scripts/tests/test_scripts.py Makefile
git commit -m "test(smp): add pthread affinity placement probe"
```

---

### Task 8: Configure ggml Strict Worker Affinity

**Files:**
- Modify: `apps/app-llama-cpu/Config.uk`
- Modify: `apps/app-llama-cpu/bench.cpp`
- Modify: `apps/app-llama-cpu/llama-server-entry.cpp`
- Modify: `kraft/Kraftfile.llama-cpu-bench`
- Modify: `kraft/Kraftfile.llama-cpu-server`

- [ ] **Step 1: Add explicit app options**

Add:

```kconfig
config APP_LLAMA_CPU_STRICT_AFFINITY
	bool "Pin each ggml worker to one vCPU"
	default y if LIBUKSCHEDCOOP_SMP

config APP_LLAMA_CPU_POLL
	int "ggml worker polling level"
	range 0 100
	default 100
```

- [ ] **Step 2: Add a local threadpool parameter helper**

In `llama-cpu-common.h` or a new focused `threadpool-config.h`, define:

```cpp
static inline ggml_threadpool_params uk_llama_threadpool_params(int n_threads) {
    ggml_threadpool_params p = ggml_threadpool_params_default(n_threads);
    p.poll = CONFIG_APP_LLAMA_CPU_POLL;
#if CONFIG_APP_LLAMA_CPU_STRICT_AFFINITY
    if (n_threads > CONFIG_UKPLAT_CPU_MAXCOUNT) {
        uk_crash("ggml threads exceed available vCPUs\n");
    }
    for (int i = 0; i < n_threads; ++i) {
        p.cpumask[i] = true;
    }
    p.strict_cpu = true;
#endif
    return p;
}
```

Confirm against pinned ggml's `ggml_thread_cpumask_next()` that a contiguous
global mask plus `strict_cpu=true` produces one-hot worker masks. The pinned
order is:

```text
n_threads=4, cpumask=0xf
secondary ith=1 -> LCPU0
secondary ith=2 -> LCPU1
secondary ith=3 -> LCPU2
main      ith=0 -> LCPU3
```

Tests assert distinct one-hot assignment in this order, not `ith == lcpu`.

- [ ] **Step 3: Use the helper in bench mode**

Replace:

```cpp
ggml_threadpool_params_default(CONFIG_APP_LLAMA_CPU_THREADS);
tpp.poll = 100;
```

with:

```cpp
struct ggml_threadpool_params tpp =
    uk_llama_threadpool_params(CONFIG_APP_LLAMA_CPU_THREADS);
```

- [ ] **Step 4: Give server mode the same policy**

In `llama-server-entry.cpp`, compute the hex mask
`(1UL << CONFIG_APP_LLAMA_CPU_THREADS) - 1` and append:

```text
--cpu-mask <hex-mask>
--cpu-strict 1
--poll CONFIG_APP_LLAMA_CPU_POLL
--cpu-mask-batch <hex-mask>
--cpu-strict-batch 1
--poll-batch CONFIG_APP_LLAMA_CPU_POLL
```

The exact option spellings are defined in the pinned
`.deps/src/llama.cpp/common/arg.cpp:1174-1258`. Add a focused helper test or
compile-time argv assertion in VOGUE code. Do not patch upstream code.

- [ ] **Step 5: Build both CPU images**

Run:

```bash
make llama-cpu-bench-build
make llama-cpu-server-build
```

Expected: both PASS.

- [ ] **Step 6: Run trace-enabled correctness**

Run:

```bash
VOGUE_SMP=4 make llama-cpu-bench-run 2>&1 |
  tee docs/results-smp/pthread-affinity-llama-placement.log
```

Expected:

- each secondary worker has a migration/resume record;
- workers resume on distinct LCPUs;
- benchmark prints PASS.

- [ ] **Step 7: Commit**

```bash
git add apps/app-llama-cpu kraft/Kraftfile.llama-cpu-bench \
  kraft/Kraftfile.llama-cpu-server \
  docs/results-smp/pthread-affinity-llama-placement.log
git commit -m "feat(llama-cpu): enable strict ggml worker affinity"
```

---

### Task 9: Remove Global Clone Round-Robin

**Files:**
- Modify: `.deps/src/unikraft/lib/posix-process/clone.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/smp.c`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/include/uk/schedcoop.h`
- Modify: `.deps/src/unikraft/lib/ukschedcoop/exportsyms.uk`
- Modify: `scripts/smp_topology.py`
- Modify: `scripts/tests/test_scripts.py`

- [ ] **Step 1: Write the regression expectation**

Add a host/source test that checks patched `clone.c` contains:

```c
s = uk_sched_current();
```

and does not call:

```c
uk_schedcoop_smp_pick()
```

This may be a focused test in `scripts/tests/test_scripts.py` reading the
pinned file.

- [ ] **Step 2: Run the test and verify failure**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts -v
```

Expected: FAIL while clone round-robin remains.

- [ ] **Step 3: Restore upstream-style clone placement**

In `uk_clone()`:

```c
s = uk_sched_current();
```

The child already inherits the parent affinity state from Task 3. Explicit
worker affinity moves it after startup.

- [ ] **Step 4: Remove deprecated picker**

Remove:

- `rr_cursor`;
- `uk_schedcoop_smp_pick()`;
- export and declaration;
- old Python `place()` behavior.

- [ ] **Step 5: Run probe and llama correctness again**

Run:

```bash
make test-fast
python3 scripts/app-pthread-affinity.py --smp 4
VOGUE_SMP=4 make llama-cpu-bench-run
```

Expected: all PASS. This proves correctness no longer depends on global thread
creation order.

- [ ] **Step 6: Commit**

```bash
git add scripts/smp_topology.py scripts/tests/test_scripts.py
git -C .deps/src/unikraft add lib/posix-process lib/ukschedcoop
git -C .deps/src/unikraft commit -m \
  "fix(smp): make explicit affinity own pthread placement"
git commit -m "test(smp): reject clone-order thread placement"
```

---

### Task 10: Export And Reapply The Unikraft Patch

**Files:**
- Create: `patches/unikraft/0003-pthread-affinity-smp.patch`
- Modify: `config/deps.json`

- [ ] **Step 1: Export the complete kernel diff**

Run:

```bash
git -C .deps/src/unikraft diff RELEASE-0.21.0 \
  > patches/unikraft/0003-pthread-affinity-smp.patch
test -s patches/unikraft/0003-pthread-affinity-smp.patch
git -C .deps/src/unikraft apply --reverse --check \
  "$PWD/patches/unikraft/0003-pthread-affinity-smp.patch"
```

Expected: non-empty patch and reverse check exits 0.

- [ ] **Step 2: Register the patch**

Append to `git_sources.unikraft.patches`:

```json
"patches/unikraft/0003-pthread-affinity-smp.patch"
```

- [ ] **Step 3: Verify forward application in a disposable checkout**

Use a temporary checkout, not destructive removal of the active `.deps` tree:

```bash
tmp=$(mktemp -d)
git clone --depth 1 --branch RELEASE-0.21.0 \
  https://github.com/unikraft/unikraft.git "$tmp/unikraft"
git -C "$tmp/unikraft" apply \
  "$PWD/patches/unikraft/0003-pthread-affinity-smp.patch"
rg -n "uk_sched_affinity|SMPPLACE|migrate_current" "$tmp/unikraft/lib"
```

Expected: patch applies cleanly and all expected symbols are present.

- [ ] **Step 4: Rebuild from configured dependency flow**

Run:

```bash
python3 scripts/deps.py fetch
make llama-cpu-bench-build
make pthread-affinity-build
```

Expected: both PASS using the registered patch.

- [ ] **Step 5: Commit**

```bash
git add patches/unikraft/0003-pthread-affinity-smp.patch config/deps.json
git commit -m "feat(smp): ship pthread affinity scheduler patch"
```

---

### Task 11: Measure Correctness And Performance

**Files:**
- Modify: `scripts/app-llama-cpu.py`
- Modify: `scripts/tests/test_scripts.py`
- Create: `docs/results-smp/pthread-affinity-llama-smp1.json`
- Create: `docs/results-smp/pthread-affinity-llama-smp4.json`
- Create: `docs/results-smp/pthread-affinity-llama-smp4-control.json`
- Modify: `docs/notes-smp-bringup-status.md`

- [ ] **Step 1: Add repeated-run aggregation**

Add runner support for:

```text
--runs 5
```

Output metrics:

```json
{
  "runs": 5,
  "pp512_samples": [],
  "tg128_samples": [],
  "pp512_median": 0.0,
  "tg128_median": 0.0
}
```

Write parser/aggregation tests before implementation.

- [ ] **Step 2: Disable placement trace**

Set:

```yaml
CONFIG_LIBUKSCHED_PLACEMENT_TRACE: 'n'
```

Rebuild before performance measurement.

- [ ] **Step 3: Measure SMP1**

Build with one thread and run five times:

```bash
VOGUE_SMP=1 python3 scripts/app-llama-cpu.py \
  --mode bench --model "$LLAMA_MODEL" --runs 5
cp results/llama/llama_cpu.json \
  docs/results-smp/pthread-affinity-llama-smp1.json
```

- [ ] **Step 4: Measure strict-affinity SMP4**

Build with four threads and run:

```bash
VOGUE_SMP=4 python3 scripts/app-llama-cpu.py \
  --mode bench --model "$LLAMA_MODEL" --runs 5
cp results/llama/llama_cpu.json \
  docs/results-smp/pthread-affinity-llama-smp4.json
```

- [ ] **Step 5: Measure SMP4 control**

Build the same four-thread image with
`CONFIG_APP_LLAMA_CPU_STRICT_AFFINITY=n`, run five times, and save:

```text
docs/results-smp/pthread-affinity-llama-smp4-control.json
```

- [ ] **Step 6: Evaluate without overclaiming**

Record:

```text
placement correctness:
SMP1 pp512/tg128 median:
SMP4 strict pp512/tg128 median:
SMP4 control pp512/tg128 median:
speedup ratios:
```

Success requires:

```text
SMP4 strict pp512 median > SMP1 pp512 median
```

If placement is correct but this fails, close this plan as “affinity correct,
performance follow-up required” and create a separate barrier/wakeup profiling
plan. Do not alter affinity semantics to chase a benchmark.

- [ ] **Step 7: Commit evidence**

```bash
git add docs/results-smp/pthread-affinity-llama-*.json \
  docs/notes-smp-bringup-status.md scripts/app-llama-cpu.py \
  scripts/tests/test_scripts.py
git commit -m "evidence(smp): measure strict pthread affinity"
```

---

### Task 12: Update Stale Plans And Run Release Gates

**Files:**
- Modify: `docs/plan-smp-scheduler.md`
- Modify: `docs/plan-smp-vcpu.md`
- Modify: `docs/notes-smp-source.md`
- Modify: `docs/notes-smp-bringup-status.md`

- [ ] **Step 1: Mark the old plan superseded**

At the top of `docs/plan-smp-scheduler.md`, add:

```markdown
> **Superseded for pthread placement (2026-06-12):**
> Tasks that rely on `CONFIG_LIBPTHREAD_EMBEDDED`, global clone round-robin,
> or claim A3 already pins ggml workers are stale. Per-thread affinity,
> migration, and validation are specified in
> `docs/superpowers/specs/2026-06-12-pthread-affinity-smp-design.md` and
> implemented by
> `docs/superpowers/plans/2026-06-12-pthread-affinity-smp.md`.
```

- [ ] **Step 2: Replace stale operational guidance**

Update all remaining statements that say:

- `libpthread_embedded` is the active provider;
- `-C`/CPU masks must not be used;
- global round-robin equals pinning;
- scheduler-online logs prove worker placement;
- a single enqueue observation proves actual execution CPU.

Replace them with the final measured behavior and links to evidence artifacts.

- [ ] **Step 3: Run stale-content scan**

Run:

```bash
rg -n \
  "LIBPTHREAD_EMBEDDED|libpthread_embedded|embedded-pthread|A3 already pins|do NOT use.*cpu-mask|round-robin.*pins" \
  docs
```

Expected: only historical statements explicitly marked stale/superseded.

- [ ] **Step 4: Run full verification**

Run:

```bash
make test-fast
make pthread-affinity-run
make llama-cpu-bench-build
make llama-cpu-server-build
make test-native
```

Then run the broader gate:

```bash
make verify
```

Expected:

- host and native tests PASS;
- pthread probe PASS;
- CPU images build;
- verify reports no new virtio-gpu/Vulkan regression;
- blocked environmental prerequisites are reported as blocked, not pass.

- [ ] **Step 5: Confirm llama.cpp remains unchanged**

Run:

```bash
git -C .deps/src/llama.cpp status --short
git diff -- .deps/src/llama.cpp
```

Expected: no changes.

- [ ] **Step 6: Final documentation commit**

```bash
git add docs/plan-smp-scheduler.md docs/plan-smp-vcpu.md \
  docs/notes-smp-source.md docs/notes-smp-bringup-status.md
git commit -m "docs(smp): replace clone round-robin with pthread affinity"
```

---

## Completion Checklist

- [ ] Current placement path captured before changes.
- [ ] Per-thread affinity persists and inherits across pthread creation.
- [ ] Effective masks intersect requested and ready online schedulers.
- [ ] Empty masks and unavailable CPUs fail explicitly.
- [ ] Self-migration returns only after execution resumes on an allowed LCPU.
- [ ] No thread appears on two scheduler queues.
- [ ] SMP4 probe reports four distinct workers on LCPUs 0-3.
- [ ] ggml strict CPU masks use the existing upstream affinity path.
- [ ] Default clone placement no longer uses global round-robin.
- [ ] Kernel changes reapply from `RELEASE-0.21.0`.
- [ ] Five-run trace-free llama results are recorded.
- [ ] Stale SMP plan claims are removed or explicitly superseded.
- [ ] `make test-fast`, probe, CPU builds, native tests, and `make verify`
      complete with results recorded.

## Stop Conditions

Stop implementation and return to root-cause analysis if:

1. migration resumes a thread on the source LCPU after successful affinity;
2. a worker appears on both source and target queues;
3. three distinct migration fixes fail;
4. the pinned musl wrapper requires unsupported remote-thread migration;
5. AP scheduler readiness cannot be published without racing remote enqueue.

These indicate an architectural issue, not permission to stack another patch.
