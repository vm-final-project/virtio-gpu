# A3 — Per-LCPU Cooperative Scheduler for Unikraft (SMP) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: this plan is executed with superpowers:subagent-driven-development — a fresh implementer subagent per task, followed by spec-compliance then code-quality review, in this same session. Subagents follow superpowers:test-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make Unikraft `RELEASE-0.21.0` run a cooperative scheduler instance on each online vCPU so that the llama.cpp appliances' threads (ggml workers, HTTP, lwIP) execute on multiple vCPUs — with one ggml worker pinned per vCPU — and beat the single-vCPU baseline, with no changes to llama.cpp.

**Architecture:** This is variant **A3** from `docs/plan-smp-vcpu.md` Phase 3. The SMP bring-up plumbing already exists in 0.21.0 (`uklcpu`, `ukpcpuvar`, IPIs, APIC/ACPI); only the *scheduler* is single-LCPU. A3 keeps the existing `ukschedcoop` (each instance is already self-contained: its own run queue + idle thread) and instantiates **one instance per online LCPU**, starts a scheduling loop on each secondary vCPU via the lcpu secondary-entry mechanism, makes the "current scheduler" a per-CPU variable, and adds a thread-placement policy. It is delivered as a **patch under `patches/unikraft/`** (VOGUE patches Unikraft rather than forking — see `config/deps.json` + `scripts/deps.py`), plus VOGUE-side Kconfig/Kraftfile wiring.

**Tech Stack:** Unikraft `RELEASE-0.21.0` (`lib/ukschedcoop`, `lib/uksched`, `lib/uklcpu`, `lib/ukpcpuvar`, `lib/ukboot`); C; VOGUE patch+deps machinery (`patches/unikraft/`, `scripts/deps.py`); QEMU `-smp`/KVM; llama.cpp `b9581` (unmodified); Python `unittest` host-native gate (`make test-fast`).

---

## Prerequisite

This plan assumes **Phase 0 of `docs/plan-smp-vcpu.md` is already merged** (the `smp_args()` helper + `--smp`/`VOGUE_SMP` wiring in `scripts/common.py`, `scripts/app-llama-vk.py`, `scripts/app-llama-cpu.py`, with passing `scripts/tests/test_scripts.py`). If it is not, execute that phase first — this plan launches guests with `VOGUE_SMP=N` and relies on that flag existing. Confirm with:

```bash
cd /mydata/JerryT/vm-final-project/virtio-gpu
python3 -c "import sys; sys.path.insert(0,'scripts'); import common; print(common.smp_args(4))"   # -> ['-smp', '4']
```

## Verified design foundation (source of truth)

All file:line references verified against the pinned sources (`config/deps.json`: Unikraft `RELEASE-0.21.0`, llama.cpp `b9581`). Subagents must re-confirm in Task 1 against the locally fetched tree before writing kernel code.

1. **`ukschedcoop` is per-instance self-contained.** `uk_schedcoop_create(a, sa, auxsa, tls_a)` allocates a `struct schedcoop` with its own `run_queue`, `sleep_queue`, and `idle` thread (`lib/ukschedcoop/schedcoop.c:281-332`). `schedcoop_schedule()` explicitly assumes "scheduler `s` is only responsible for the current logical CPU" (`schedcoop.c:60-64`). → Running one instance per LCPU is the design it already expects.
2. **The single-LCPU block is one guard.** `schedcoop_idle_thread()` returns `NULL` for `proc_id > 0` (`schedcoop.c:269-279`). A3 does not need to remove this (that's A1); A3 gives each LCPU its *own* instance, so each only ever sees its local idle.
3. **Boot starts only the BSP scheduler.** `lib/ukboot/boot.c:358-378`: `uk_schedcoop_create()` → `uk_sched_start(s)` → `uk_sched_thread_create_fn2(s, main_thread, …)`. **No `uk_lcpu_start` / AP bring-up happens.** Secondary LCPUs are never driven into a scheduler.
4. **`uk_sched_start()` is the per-scheduler bootstrap.** `lib/uksched/sched.c:203-260`: allocates a bare "init" `uk_thread` for the current context, sets the per-CPU current-thread pointer `uk_pcpuvar_current_set(__uk_sched_thread_current, main_thread)`, then calls `s->sched_start(s, main_thread)`. → The per-LCPU AP entry must run the equivalent of this for its local scheduler.
5. **Per-CPU current-thread pointer already exists.** `lib/uksched/include/uk/thread.h`: `extern __uk_pcpuvar struct uk_thread *__uk_sched_thread_current;` (per-CPU via `ukpcpuvar`). A3 adds a sibling per-CPU `__uk_sched_current` (the local scheduler pointer).
6. **Secondary CPUs take an entry function.** `uk_lcpu_entry_default()` (`lib/uklcpu/lcpu.c:253-273`) runs `uk_lcpu_init()` then jumps to a per-CPU supplied entry (`UK_LCPU_SENTRY_SYM` / stack / arg) if set, else handles `uk_lcpu_run()` IPIs. → A3 supplies a per-LCPU entry that bootstraps + runs the local scheduler.
7. **Remote-run + wait API (array/IPI based).** `uk_lcpu_run(const __u64 lcpuidx[], unsigned int *num, const struct uk_lcpu_func *fn, unsigned long flags)`, `uk_lcpu_wait(lcpuidx[], num, timeout)`, `uk_lcpu_wakeup(lcpuidx[], num)`, `uk_lcpu_get_current()`, array `uk_lcpus` (`lib/uklcpu/include/uk/lcpu.h:252-293`, `lib/uklcpu/exportsyms.uk`). `uk_lcpu_mp_init(run_irq, wakeup_irq)` does MP discovery on the BSP (`lcpu.c:224`).
8. **llama.cpp is unmodified.** ggml opens exactly `n_threads` pthread workers and synchronises with a spin barrier (`ggml/src/ggml-cpu/ggml-cpu.c:3286`, `:566-599`). Pinning one worker per LCPU makes the spin barrier correct (see `docs/plan-smp-vcpu.md` Phase 3.0, Fact 1).

## Delivery mechanism & file structure

VOGUE re-applies patches after every Unikraft checkout (`scripts/deps.py`, `config/deps.json` `patches` list). A3 ships as:

- **Create** `patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch` — the kernel change: per-CPU `__uk_sched_current`, `uk_sched_start_secondary()` bootstrap, an AP bring-up driver that starts a coop instance on each online LCPU, and a placement hook in `uk_sched_thread_add`. (Built/edited inside the fetched tree, then exported as a patch — see tasks.)
- **Modify** `config/deps.json` — register the new patch in the `unikraft.patches` array.
- **Modify** `scripts/deps.py` if needed so the new patch is applied (it iterates the `patches` list; confirm in Task 1).
- **Create** `scripts/smp_topology.py` + tests in `scripts/tests/test_scripts.py` — the host-native-testable thread→LCPU placement policy (pure logic, TDD).
- **Modify** `kraft/Kraftfile.llama-cpu-bench`, `kraft/Kraftfile.llama-vk-server` — enable the new `CONFIG_LIBUKSCHEDCOOP_SMP` (or chosen symbol) + `CONFIG_UKPLAT_CPU_MAXCOUNT`.
- **Create** `docs/results-smp/` artifacts — boot logs + before/after JSON backing each claim.
- **Create** `docs/notes-smp-source.md` — the Task 1 source-confirmation design note.

Design rules (AGENTS.md): `uk_*` prefix for new kernel symbols; tabs to match Unikraft style; keep VOGUE apps one-image-one-purpose; prefer existing Makefile wrappers.

---

## Task 0: Isolated workspace + pinned source + green baseline

**Files:** none modified (environment only)

- [ ] **Step 1: Create the working branch (do NOT work on the default branch)**

```bash
cd /mydata/JerryT/vm-final-project/virtio-gpu
git status --short && git rev-parse --abbrev-ref HEAD
git checkout -b smp-a3-per-lcpu-scheduler
```
Expected: on branch `smp-a3-per-lcpu-scheduler`, clean tree.

- [ ] **Step 2: Fetch & pin the Unikraft source locally**

```bash
python3 -m scripts.deps fetch 2>/dev/null || python3 scripts/deps.py fetch
ls .deps/src/unikraft/lib/ukschedcoop/schedcoop.c
git -C .deps/src/unikraft describe --tags
```
Expected: source present at `.deps/src/unikraft`, describing as `RELEASE-0.21.0`. (If the deps command name differs, read `scripts/deps.py --help` / the Makefile `deps` target and use that; record the exact command in `docs/notes-smp-source.md`.)

- [ ] **Step 3: Establish the green host-native baseline**

Run: `make test-fast`
Expected: PASS. This must stay green through every task that touches `scripts/`.

- [ ] **Step 4: Commit the branch point (no code yet)**

```bash
git commit --allow-empty -m "chore(smp-a3): start per-LCPU scheduler branch"
```

## Task 1: Source-confirmation design note (de-risk the kernel code)

This task produces a written artifact answering the exact integration questions the kernel patch depends on. It is not a placeholder — it has a fixed checklist and a concrete deliverable. The later kernel tasks reference its answers.

**Files:**
- Create: `docs/notes-smp-source.md`

- [ ] **Step 1: Answer each question with a `file:line` citation from `.deps/src/unikraft`**

Read the listed files and record answers in `docs/notes-smp-source.md`:

1. **AP start call.** Is there a public `uk_lcpu_start(...)` (it is in `lib/uklcpu/exportsyms.uk`)? Find its signature and how an entry function + stack + arg are passed (grep `uk_lcpu_start`, `UK_LCPU_SENTRY_SYM`, `sstackp`, `sarg` in `lib/uklcpu/lcpu.c` and `lib/uklcpu/include/uk/lcpu.h`). Record the exact signature.
2. **MP discovery.** Where/when is `uk_lcpu_mp_init()` called today (grep the tree)? If nowhere, A3's driver must call it on the BSP before starting APs. Record the call site or its absence.
3. **Per-CPU current scheduler.** How does code get "the scheduler for this CPU" today? Find `uk_sched_current()` (grep `lib/uksched/`), and whether it reads a global or a per-CPU var. Record the symbol A3 must make per-CPU.
4. **pthread → scheduler binding.** Which lib backs `CONFIG_LIBPTHREAD_EMBEDDED`? Find where a new pthread chooses its scheduler (grep `uk_sched_current\|uk_sched_thread_add\|uk_sched_thread_create` under the pthread lib). Record whether new threads join "current" scheduler (→ they'll land on the creating LCPU's scheduler) or a fixed default.
5. **Patch application.** Confirm `scripts/deps.py` applies every entry in `config/deps.json` `unikraft.patches` after checkout, and the patch format it expects (`git apply` vs `patch -p1`). Record it.
6. **`struct uk_lcpu` fields.** Confirm the index field name used by `uk_lcpu_get_current()` (the smoke spike in `plan-smp-vcpu.md` assumed `->idx`). Record the real field.

- [ ] **Step 2: Record a go/no-go**

In the note, state whether all six are answered and whether the A3 design below is buildable as written, or which steps need adjustment. If a finding contradicts the design foundation above, flag it explicitly (do not silently proceed).

- [ ] **Step 3: Commit**

```bash
git add docs/notes-smp-source.md
git commit -m "docs(smp-a3): source-confirmation note for per-LCPU scheduler"
```

## Task 2: Thread→LCPU placement policy (pure logic, TDD)

The placement decision (which LCPU a new thread is pinned to) is pure arithmetic and is the one piece testable host-native. Build it first so the kernel side has a verified policy to call.

**Files:**
- Create: `scripts/smp_topology.py`
- Test: `scripts/tests/test_scripts.py`

> Note: this Python module documents and tests the *policy*; the kernel patch (Task 5) implements the identical rule in C. Keeping the rule here lets `make test-fast` guard it and gives the C side a reference oracle.

- [ ] **Step 1: Write the failing test**

Add to `scripts/tests/test_scripts.py`:

```python
import smp_topology

class SmpTopologyTests(unittest.TestCase):
    def test_round_robin_pins_one_worker_per_lcpu(self) -> None:
        # 4 vCPUs, 4 ggml workers -> one each, distinct LCPUs
        self.assertEqual([smp_topology.place(i, 4) for i in range(4)], [0, 1, 2, 3])

    def test_wraps_when_more_threads_than_lcpus(self) -> None:
        self.assertEqual([smp_topology.place(i, 4) for i in range(6)], [0, 1, 2, 3, 0, 1])

    def test_single_lcpu_pins_all_to_zero(self) -> None:
        self.assertEqual([smp_topology.place(i, 1) for i in range(3)], [0, 0, 0])
```

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -m pytest scripts/tests/test_scripts.py -k SmpTopology -v`
Expected: FAIL (`ModuleNotFoundError: No module named 'smp_topology'`).

- [ ] **Step 3: Implement**

`scripts/smp_topology.py`:

```python
"""Thread -> LCPU placement policy for the A3 per-LCPU scheduler.

Mirrors the C rule in patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch.
Round-robin so each successive thread lands on the next vCPU; with one ggml
worker per vCPU this pins exactly one worker per LCPU, which makes ggml's spin
barrier correct (see docs/plan-smp-vcpu.md Phase 3.0).
"""
from __future__ import annotations


def place(thread_seq: int, lcpu_count: int) -> int:
    """Return the LCPU index a thread with sequence number `thread_seq` pins to."""
    if lcpu_count <= 1:
        return 0
    return thread_seq % lcpu_count
```

- [ ] **Step 4: Run to verify it passes + full gate**

Run: `python3 -m pytest scripts/tests/test_scripts.py -k SmpTopology -v && make test-fast`
Expected: PASS; `make test-fast` green.

- [ ] **Step 5: Commit**

```bash
git add scripts/smp_topology.py scripts/tests/test_scripts.py
git commit -m "feat(smp-a3): round-robin thread->LCPU placement policy + tests"
```

## Design amendments (confirmed in Task 1 — `docs/notes-smp-source.md`)

These findings from the source-confirmation note **override** the skeletons below where they conflict. They are verified against `.deps/src/unikraft` (RELEASE-0.21.0).

- **A. No new per-CPU scheduler pointer needed (simplifies Task 3).** `uk_sched_current()` (`lib/uksched/include/uk/sched.h:53`) already derives the scheduler from `uk_thread_current()->sched`, and the current-thread pointer `__uk_sched_thread_current` is already per-CPU (`__uk_pcpuvar`, `lib/uksched/include/uk/thread.h:97`). So once each AP runs its own bootstrap that sets the per-CPU current thread (whose `->sched` points to that AP's local scheduler), `uk_sched_current()` is automatically correct per-CPU. **Do NOT add `__uk_sched_current`.** Task 3 = add `uk_sched_start_secondary()` only.
- **B. `uk_lcpu_start` is array-based and the AP arg is fixed (changes Task 4).** Prototype: `int uk_lcpu_start(const __u64 lcpuidx[], unsigned int *num, __u64 sp[], __u64 entry[], unsigned long flags)` (`lib/uklcpu/include/uk/lcpu/pm.h:118`, def `lib/uklcpu/lcpu.c:297`). The per-AP argument is **hard-coded** to `(struct uk_lcpu *)lcpu` (`lcpu.c:~350`), so the entry function signature is fixed to `void entry(struct uk_lcpu *)` and **cannot** receive the scheduler pointer. → The bring-up driver must store the per-LCPU scheduler instances in a **global array** `static struct uk_sched *coop_sched_by_lcpu[CONFIG_UKPLAT_CPU_MAXCOUNT];`, and the AP entry recovers its instance via its own index: `coop_sched_by_lcpu[uk_pcpuvar_current_get(uk_pcpuvar_cpu_idx)]`.
- **C. `uk_lcpu_mp_init()` is already called at boot (removes a step from Task 4).** Called on the BSP under `CONFIG_HAVE_SMP` at `plat/kvm/x86/setup.c:150` and `plat/kvm/arm/setup.c:110`. **Do NOT call it again** in the driver. The driver just allocates a stack per AP and calls `uk_lcpu_start` for indices `1..CONFIG_UKPLAT_CPU_MAXCOUNT-1`, using the `num` out-parameter to learn how many actually started.
- **D. No `struct uk_lcpu.idx` field (fixes the smoke spike + all index uses).** `struct uk_lcpu` (`lib/uklcpu/include/uk/lcpu.h:95`) has only `state`, `error_code`, `fn`. Get the current LCPU index with `uk_pcpuvar_current_get(uk_pcpuvar_cpu_idx)` (`lib/ukpcpuvar/include/uk/pcpuvar.h:36`), NOT `uk_lcpu_get_current()->idx`.
- **E. Thread pinning is mandatory, not optional (confirms Task 5).** New pthreads are created via `lib/posix-process/clone.c` (`s = uk_sched_current()` at `:143`, `uk_sched_thread_add(s, th)` at `:400`) — they bind to the *creating* CPU's scheduler. llama.cpp spawns all workers from `main` on the BSP, so without an explicit round-robin placement hook they ALL pile on the BSP scheduler and the A3 benefit is lost. Task 5 must intercept thread-add and distribute across `coop_sched_by_lcpu[]` (waking the target LCPU via `uk_lcpu_wakeup`).
- **F. Patch format (confirms Task 6).** `scripts/deps.py:106` applies patches with `git apply` (default `-p1`), idempotency-checked via `git apply --reverse --check` (`:100`). Export a plain reversible `git diff` (with `a/`…`b/` prefixes).

## Task 3: Kernel — secondary scheduler bootstrap (`uk_sched_start_secondary`)

Work inside `.deps/src/unikraft`; the patch is exported in Task 6. Use the exact signatures recorded in `docs/notes-smp-source.md` (Task 1).

**Files (in `.deps/src/unikraft`):**
- Modify: `lib/uksched/include/uk/sched.h` (declare per-CPU current scheduler)
- Modify: `lib/uksched/sched.c` (define it; add `uk_sched_start_secondary()`)

- [ ] **Step 1: Add a per-CPU "current scheduler" pointer**

In `lib/uksched/include/uk/sched.h`, alongside the existing per-CPU current-thread pointer, declare:

```c
extern __uk_pcpuvar struct uk_sched *__uk_sched_current;

static inline struct uk_sched *uk_sched_current(void)
{
	return uk_pcpuvar_current_get(__uk_sched_current);
}
```
(If `uk_sched_current()` already exists per Task 1, adapt it to read the per-CPU var instead of a global — do not duplicate.)

In `lib/uksched/sched.c`, define it: `__uk_pcpuvar struct uk_sched *__uk_sched_current;` and set it in `uk_sched_start()` right where `__uk_sched_thread_current` is set (`sched.c:~240`): `uk_pcpuvar_current_set(__uk_sched_current, s);`.

- [ ] **Step 2: Add `uk_sched_start_secondary()` — the AP-side bootstrap**

In `lib/uksched/sched.c`, add a variant of `uk_sched_start()` for secondary LCPUs. It mirrors `uk_sched_start()` (`sched.c:203-260`) — create a bare "init" thread for the AP's current context, set both per-CPU pointers, call `s->sched_start(s, main_thread)` — **without** assuming it is the BSP:

```c
int uk_sched_start_secondary(struct uk_sched *s)
{
	struct uk_thread *main_thread;
	uintptr_t tlsp, auxsp;
	int ret;

	UK_ASSERT(s && s->sched_start && !s->is_started);

	tlsp  = uk_lcpu_tlsp_get();
	auxsp = uk_pcpuvar_current_get(UK_LCPU_AUXSP_SYM);
	main_thread = uk_thread_create_bare(s->a, 0x0, 0x0, auxsp,
					    tlsp, !(!tlsp), false,
					    "init-ap", NULL, NULL);
	if (!main_thread)
		return -ENOMEM;
	main_thread->sched = s;
	uk_thread_set_runnable(main_thread);

	uk_pcpuvar_current_set(__uk_sched_thread_current, main_thread);
	uk_pcpuvar_current_set(__uk_sched_current, s);
	UK_TAILQ_INSERT_TAIL(&s->thread_list, main_thread, thread_list);

	ret = s->sched_start(s, main_thread);
	if (ret < 0)
		return ret;
	s->is_started = true;
	return 0;
}
```
Export it in `lib/uksched/exportsyms.uk` and declare it in `sched.h`. (Confirm `uk_thread_create_bare` arg order against `sched.c:218` — copy it verbatim from `uk_sched_start`.)

- [ ] **Step 3: Build to verify it compiles (via a guest build)**

Build the smoke or CPU-bench appliance (`make llama-cpu-bench`) so the modified Unikraft compiles. Expected: clean build (no unresolved symbols). Fix any signature mismatch against the real source now.

- [ ] **Step 4: Commit (in-tree, exported later)**

```bash
git add docs/notes-smp-source.md   # update with any signature corrections
git commit -m "feat(smp-a3): per-CPU current scheduler + uk_sched_start_secondary (kernel)"
```

## Task 4: Kernel — AP bring-up driver (one coop scheduler per LCPU)

**Files (in `.deps/src/unikraft`):**
- Create: `lib/ukschedcoop/smp.c` (the per-LCPU bring-up)
- Modify: `lib/ukschedcoop/Config.uk` (new `LIBUKSCHEDCOOP_SMP` option), `lib/ukschedcoop/Makefile.uk` (compile `smp.c` under SMP)
- Modify: `lib/ukboot/boot.c` (invoke the driver after the BSP scheduler starts)

- [ ] **Step 1: Add the bring-up driver**

`lib/ukschedcoop/smp.c` — for each online LCPU > 0: create a coop instance, then start the AP with an entry that calls `uk_sched_start_secondary()` and parks in its scheduler. The AP entry uses the secondary-entry mechanism confirmed in Task 1 (`uk_lcpu_start` with SENTRY/stack/arg). Skeleton (finalise the start call from Task 1's signature):

```c
#include <uk/sched.h>
#include <uk/schedcoop.h>
#include <uk/lcpu.h>
#include <uk/print.h>

static void __noreturn ap_sched_entry(struct uk_lcpu *this_lcpu __unused)
{
	struct uk_sched *s = uk_sched_current(); /* set just below before start */
	int rc = uk_sched_start_secondary(s);
	if (rc) UK_CRASH("AP %u: sched start failed (%d)\n",
			 uk_lcpu_get_current()->idx, rc);
	/* Hand control to the local scheduler; idle thread halts when no work. */
	uk_sched_yield();
	for (;;) uk_lcpu_halt_irq();
}

int uk_schedcoop_smp_bringup(struct uk_alloc *a, struct uk_alloc *sa,
			     struct uk_alloc *auxsa)
{
	for (unsigned int i = 1; i < CONFIG_UKPLAT_CPU_MAXCOUNT; i++) {
		struct uk_sched *s = uk_schedcoop_create(a, sa, auxsa, a);
		if (!s) return -ENOMEM;
		/* Pass `s` to the AP via its startup arg, then start it on
		 * uk_lcpus[i] with entry ap_sched_entry. EXACT start call per
		 * docs/notes-smp-source.md Q1 (uk_lcpu_start signature). */
		/* ... uk_lcpu_start(&uk_lcpus[i], ap_sched_entry, stack, s); ... */
	}
	return 0;
}
```
Resolve the two marked spots using Task 1's confirmed `uk_lcpu_start` signature and how the startup arg is delivered to the AP (so `ap_sched_entry` can recover its `s` — e.g. via the SARG per-CPU symbol rather than `uk_sched_current()`; adjust accordingly and record the choice in the note).

- [ ] **Step 2: Kconfig + Makefile**

`lib/ukschedcoop/Config.uk` — add under `if HAVE_SMP`:
```
config LIBUKSCHEDCOOP_SMP
	bool "Run a cooperative scheduler instance on each online LCPU"
	depends on HAVE_SMP
	default n
```
`lib/ukschedcoop/Makefile.uk` — `libukschedcoop_srcs-$(CONFIG_LIBUKSCHEDCOOP_SMP) += $(LIBUKSCHEDCOOP_BASE)/smp.c`.

- [ ] **Step 3: Invoke from boot**

In `lib/ukboot/boot.c`, after `uk_sched_start(s)` (`boot.c:364`) and before/around `main_thread` creation, guard with the new symbol:
```c
#if CONFIG_LIBUKSCHEDCOOP_SMP
	uk_lcpu_mp_init(uk_lcpu_run_irqv ? *uk_lcpu_run_irqv : 0,
			uk_lcpu_wakeup_irqv ? *uk_lcpu_wakeup_irqv : 0);
	uk_schedcoop_smp_bringup(a, sa, auxsa);
#endif
```
(Use the exact `uk_lcpu_mp_init` argument convention from Task 1 Q2; if MP init is already done elsewhere, drop that line.)

- [ ] **Step 4: Build**

Run: `make llama-cpu-bench`
Expected: clean build with `CONFIG_LIBUKSCHEDCOOP_SMP=y` reachable. Resolve link/compile errors against the real source.

- [ ] **Step 5: Commit**

```bash
git -C .deps/src/unikraft add -A   # staged in the source tree for export
git commit --allow-empty -m "feat(smp-a3): per-LCPU coop bring-up driver + boot hook (kernel)"
```

## Task 5: Kernel — pin threads via the placement policy

**Files (in `.deps/src/unikraft`):**
- Modify: thread-creation path so a new thread is added to the scheduler of its target LCPU (per `smp_topology.place`), and pinned there.

- [ ] **Step 1: Implement the placement at thread-add time**

Using Task 1 Q4's finding (where pthreads pick a scheduler): make new threads round-robin across LCPUs with the same rule as `scripts/smp_topology.py` — `target = seq % CONFIG_UKPLAT_CPU_MAXCOUNT` — and add the thread to that LCPU's scheduler instance (waking it via `uk_lcpu_wakeup` if halted). Maintain an atomic sequence counter for `seq`. Keep the BSP (LCPU0) able to take threads too.

If the cleanest hook is in `ukschedcoop` (a `thread_add` that, when `CONFIG_LIBUKSCHEDCOOP_SMP`, forwards to the per-LCPU instance), implement it there so llama.cpp/libpthread stay unchanged. Record the exact insertion point chosen.

- [ ] **Step 2: Build**

Run: `make llama-cpu-bench`
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git -C .deps/src/unikraft commit -am "feat(smp-a3): round-robin thread pinning across LCPU schedulers (kernel)"
```

## Task 6: Export the kernel change as a VOGUE patch + register it

**Files:**
- Create: `patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch`
- Modify: `config/deps.json`

- [ ] **Step 1: Export the diff against the pinned tag**

```bash
cd .deps/src/unikraft
git diff RELEASE-0.21.0 > /mydata/JerryT/vm-final-project/virtio-gpu/patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch
cd /mydata/JerryT/vm-final-project/virtio-gpu
wc -l patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch   # non-empty
```
(Use the patch format Task 1 Q5 confirmed `scripts/deps.py` consumes; if it expects `git format-patch`, produce that instead.)

- [ ] **Step 2: Register the patch**

Add `"patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch"` to the `unikraft.patches` array in `config/deps.json`.

- [ ] **Step 3: Verify clean re-apply from scratch**

```bash
rm -rf .deps/src/unikraft
python3 -m scripts.deps fetch 2>/dev/null || python3 scripts/deps.py fetch
grep -rl "uk_schedcoop_smp_bringup" .deps/src/unikraft/lib/ukschedcoop/   # patch landed
make llama-cpu-bench   # builds from a fresh, patched checkout
```
Expected: patch applies cleanly on a fresh checkout and the appliance builds.

- [ ] **Step 4: Commit**

```bash
git add patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch config/deps.json
git commit -m "feat(smp-a3): ship per-LCPU coop scheduler as a unikraft patch"
```

## Task 7: Enable A3 in the appliances + prove threads run on distinct LCPUs

**Files:**
- Modify: `kraft/Kraftfile.llama-cpu-bench`, `kraft/Kraftfile.llama-vk-server`
- Create: `docs/results-smp/a3-cpu-bench-smp4-boot.log`

- [ ] **Step 1: Enable the scheduler + CPU count in the Kraftfiles**

In `kraft/Kraftfile.llama-cpu-bench` add under `kconfig:`:
```yaml
    CONFIG_UKPLAT_CPU_MAXCOUNT: '4'
    CONFIG_LIBUKSCHEDCOOP_SMP: 'y'
    CONFIG_APP_LLAMA_CPU_THREADS: '4'
```
(`Kraftfile.llama-vk-server` already sets `CONFIG_UKPLAT_CPU_MAXCOUNT: '4'`; add `CONFIG_LIBUKSCHEDCOOP_SMP: 'y'`.)

- [ ] **Step 2: Add a per-LCPU log line to confirm distribution**

Temporarily, in `lib/ukschedcoop/smp.c` `ap_sched_entry`, keep a `uk_pr_info("smp-a3: scheduler online on LCPU %u\n", uk_lcpu_get_current()->idx);`. Re-export the patch (Task 6 Step 1) so the log lands.

- [ ] **Step 3: Boot the CPU bench on 4 vCPUs and capture**

```bash
make llama-cpu-bench
VOGUE_SMP=4 make llama-cpu-bench-run 2>&1 | tee docs/results-smp/a3-cpu-bench-smp4-boot.log
grep -E "smp-a3: scheduler online on LCPU [123]" docs/results-smp/a3-cpu-bench-smp4-boot.log
```
Expected: three "scheduler online on LCPU 1/2/3" lines + the BSP — proving four cooperative schedulers, one per vCPU. If a guest hangs, that is a real failure (debug with superpowers:systematic-debugging), not evidence to ignore.

- [ ] **Step 4: Commit**

```bash
git add kraft/Kraftfile.llama-cpu-bench kraft/Kraftfile.llama-vk-server patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch docs/results-smp/a3-cpu-bench-smp4-boot.log
git commit -m "feat(smp-a3): enable per-LCPU scheduler in appliances; prove 4 schedulers online"
```

## Task 8: Measure the win (CPU bench, then VK server)

**Files:**
- Create: `docs/results-smp/a3_llama_cpu_smp1.json`, `a3_llama_cpu_smp4.json`, `a3_server_vk_smp1.json`, `a3_server_vk_smp4.json`

- [ ] **Step 1: CPU bench — before/after**

```bash
VOGUE_SMP=1 make llama-cpu-bench-run && cp results/llama/llama_cpu.json docs/results-smp/a3_llama_cpu_smp1.json
VOGUE_SMP=4 make llama-cpu-bench-run && cp results/llama/llama_cpu.json docs/results-smp/a3_llama_cpu_smp4.json
python3 - <<'PY'
import json
a=json.load(open("docs/results-smp/a3_llama_cpu_smp1.json"))["metrics"]
b=json.load(open("docs/results-smp/a3_llama_cpu_smp4.json"))["metrics"]
for k in ("pp512","tg128"):
    print(k,"smp1=",a.get(k),"smp4=",b.get(k),
          "speedup=",round(b.get(k,0)/a.get(k,1),2) if a.get(k) else "n/a")
PY
```
Expected (success): `pp512` (the most thread-parallel ggml stage) speedup > 1.0 — ideally trending toward the vCPU count. Record the actual number; claim a win only if smp4 beats smp1.

- [ ] **Step 2: VK server — before/after**

```bash
VOGUE_SMP=1 make llama-vk-server-run && cp results/llama/server_vk_throughput.json docs/results-smp/a3_server_vk_smp1.json 2>/dev/null || cp results/llama/llama_server_vk.json docs/results-smp/a3_server_vk_smp1.json
VOGUE_SMP=4 make llama-vk-server-run && cp results/llama/server_vk_throughput.json docs/results-smp/a3_server_vk_smp4.json 2>/dev/null || cp results/llama/llama_server_vk.json docs/results-smp/a3_server_vk_smp4.json
```
Compare `requests_per_s` / `decode_tps_mean`. Baseline to beat (`plan-optimize.md`): `decode_tps_mean=140.6`, `prompt_tps_mean=865.66`, `ttft_s=1.2376`. Note the VK decode is GPU-bound (`-ngl 99`), so the expected gain is in CPU-side overlap, not raw decode.

- [ ] **Step 3: Remove the temporary debug log line**

Delete the `uk_pr_info("smp-a3: scheduler online …")` from `lib/ukschedcoop/smp.c`, re-export the patch (Task 6 Step 1), rebuild once to confirm it still boots and the bench result is unchanged.

- [ ] **Step 4: Commit the evidence**

```bash
git add docs/results-smp/a3_*.json patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch
git commit -m "evidence(smp-a3): CPU bench + VK server before/after on 4 vCPUs"
```

## Task 9: Full verification gate

- [ ] **Step 1: Host-native + release gates**

Run: `make test-fast` then `make verify`
Expected: `test-fast` green; `make verify` runs the Venus/Vulkan/llama captures without regressions (no `blocked:*` row treated as pass, per AGENTS.md).

- [ ] **Step 2: Confirm llama.cpp is untouched**

Run: `git diff --stat RELEASE-0.21.0 -- .deps/src/llama.cpp 2>/dev/null; ls patches/ | grep -i llama || echo "no llama patches added"`
Expected: no llama.cpp changes — the win came purely from the scheduler.

---

## Success criteria

1. A fresh, patched Unikraft checkout builds the appliances (`make llama-cpu-bench` from clean `.deps`).
2. Boot log shows one cooperative scheduler online per vCPU (`docs/results-smp/a3-cpu-bench-smp4-boot.log`).
3. CPU bench `pp512` on 4 vCPUs beats the 1-vCPU baseline (`a3_llama_cpu_smp4.json` > `a3_llama_cpu_smp1.json`) — the headline win.
4. `make test-fast` and `make verify` green; llama.cpp unmodified.

## Risks & honest caveats

- **This is real kernel work.** AP bring-up, per-CPU scheduler bootstrap, and cross-LCPU thread placement are subtle (stacks, TLS, IRQ state, wakeups). Budget for debugging with superpowers:systematic-debugging; a hang is a bug to fix, never evidence to wave away.
- **Task 1 may change the code.** The skeletons in Tasks 3–5 use verified APIs but two spots (exact `uk_lcpu_start` signature; how the AP recovers its scheduler pointer) are confirmed in Task 1 — adjust the code to the real signatures; do not force the skeleton if the source differs.
- **pthread binding.** If `CONFIG_LIBPTHREAD_EMBEDDED` creates all threads against a fixed default scheduler rather than "current", Task 5 must hook that path (recorded in Task 1 Q4) — otherwise threads won't spread even with per-LCPU schedulers running.
- **Cooperative within an LCPU still doesn't preempt.** A3 spreads threads across vCPUs but each LCPU is still cooperative. With one ggml worker pinned per vCPU this is fine; if a workload puts >1 CPU-bound non-yielding thread on one LCPU, that LCPU serialises (escalate to A1/A2 only if a real workload needs it).
- **VK upside is bounded.** `-ngl 99` keeps decode on the GPU; don't overclaim a decode-rate win on the VK path.
- **Upstream alignment.** Track any upstream SMP-scheduler work before investing further; keep the change isolated in one patch for easy rebase.

## References

Same sources as `docs/plan-smp-vcpu.md` (see its **References** section), with the A3-critical files:

- `lib/ukschedcoop/schedcoop.c:60-64, 269-279, 281-332` — per-instance structure; single-LCPU guard; `uk_schedcoop_create` signature (Unikraft `RELEASE-0.21.0`)
- `lib/uksched/sched.c:203-260` — `uk_sched_start()` bootstrap pattern (the template for `uk_sched_start_secondary`)
- `lib/uksched/include/uk/thread.h` — `__uk_pcpuvar struct uk_thread *__uk_sched_thread_current`
- `lib/ukboot/boot.c:358-378` — BSP-only scheduler start (the hook point)
- `lib/uklcpu/lcpu.c:224, 253-273` — `uk_lcpu_mp_init`, `uk_lcpu_entry_default` (secondary entry)
- `lib/uklcpu/include/uk/lcpu.h:204, 252-293` — `uk_lcpu_run/wait/wakeup/mp_init` signatures
- `lib/uklcpu/exportsyms.uk` — `uk_lcpu_start`, `uk_lcpu_run`, `uk_lcpu_wait`, `uk_lcpu_wakeup`, `uk_lcpu_get_current`, `uk_lcpus`
- `ggml/src/ggml-cpu/ggml-cpu.c:566-599, 3286` (llama.cpp `b9581`) — spin barrier + `n_threads-1` workers (why pin-one-per-LCPU works)
- `config/deps.json`, `scripts/deps.py` — patch-on-checkout delivery mechanism
- Parent plan & decision: `docs/plan-smp-vcpu.md` (Phase 3, variant A3)
- Upstream SMP PRs: https://github.com/unikraft/unikraft/pull/469 , /244 , /373
- Unikraft v0.21.0 notes: https://unikraft.org/blog/2026-04-20-unikraft-releases-v0.21.0

## Self-review (performed against this plan)

- **Coverage:** A3 decision → kernel per-LCPU scheduler (Tasks 3–5) + delivery patch (Task 6) + appliance enable (Task 7) + measurement (Task 8) + gates (Task 9); "no llama.cpp change" enforced (Task 9 Step 2). ✓
- **Placeholders:** code skeletons use verified APIs; the two source-dependent spots are gated behind Task 1's concrete source-confirmation note (with a fixed 6-question checklist + go/no-go), not hand-waved. ✓
- **Type/name consistency:** `place(thread_seq, lcpu_count)` matches between `scripts/smp_topology.py` and its tests and the C rule (Task 5); `uk_sched_start_secondary`, `__uk_sched_current`, `uk_schedcoop_smp_bringup`, `CONFIG_LIBUKSCHEDCOOP_SMP` used consistently across tasks; `uk_schedcoop_create(a, sa, auxsa, tls_a)` matches the verified `schedcoop.c:281` signature. ✓
