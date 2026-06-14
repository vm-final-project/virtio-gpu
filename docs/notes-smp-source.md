# SMP A3 — Source-Confirmation Design Note (per-LCPU scheduler)

Task 1 deliverable. All line citations are **relative to `.deps/src/unikraft`**
at tag **RELEASE-0.21.0** (`git -C .deps/src/unikraft describe --tags` →
`RELEASE-0.21.0`, confirmed). No source was modified by this task.

Goal recap (A3): run one `ukschedcoop` instance on **each** online vCPU so
llama.cpp pthreads execute across multiple vCPUs.

## 2026-06-13 Scope Reset

This note started as the source-confirmation note for the broader A3 pthread
affinity effort. The active requirement has since been narrowed:

- only the single llama bench/server VM must spread worker threads across
  available vCPUs;
- the per-LCPU scheduler bring-up findings below still matter and remain valid;
- the generalized current-thread `sched_setaffinity()` migration path is no
  longer the primary implementation target.

The practical consequence is:

- keep the evidence here that proves `pthread_create -> __clone -> uk_clone`
  and that new threads otherwise bind to the creator's scheduler;
- use that evidence to justify a minimal create-time placement mechanism for
  llama workers;
- do not read this note as a requirement to finish full Linux-style affinity
  migration before claiming success for the narrowed llama-only goal.

---

## Q1 — AP start call (entry function + stack + argument)

**`uk_lcpu_start` prototype** — declared in `lib/uklcpu/include/uk/lcpu/pm.h:118`,
defined in `lib/uklcpu/lcpu.c:297`, exported in `lib/uklcpu/exportsyms.uk:18`:

```c
int uk_lcpu_start(const __u64 lcpuidx[],
		  unsigned int *num,
		  __u64 sp[], __u64 entry[],
		  unsigned long flags);          /* pm.h:118 */
```

(The .c definition uses `__uptr sp[]`/`__uptr entry[]`, same width.)

It takes **arrays** indexed in parallel: `lcpuidx[i]` is the logical CPU index
to start, `sp[i]` its stack pointer, `entry[i]` its entry function (NULL ⇒ the
CPU enters a low-power wait state). `num` is in/out (count requested / count
started).

**Mechanism that makes a secondary LCPU run a chosen entry function.**
Inside `uk_lcpu_start`, for each target CPU the startup arguments are written
into that CPU's per-CPU slots (`lib/uklcpu/lcpu.c:350-355`):

```c
uk_pcpuvar_lval(lcpuidx[i], UK_LCPU_SENTRY_SYM) =
        (entry && entry[i]) ? entry[i] : (__uptr)uk_lcpu_entry_default;
uk_pcpuvar_lval(lcpuidx[i], UK_LCPU_SSTACKP_SYM) = sp[i];
uk_pcpuvar_lval(lcpuidx[i], UK_LCPU_SARG_SYM)    = (__uptr)lcpu;   /* <-- NOT user-chosen */
```

`UK_LCPU_SENTRY_SYM` / `UK_LCPU_SSTACKP_SYM` / `UK_LCPU_SARG_SYM` are defined in
`lib/uklcpu/include/uk/lcpu/start.h:14-18`, resolving (native plat) to the
per-CPU symbols `uk_plat_native_sentry` / `uk_plat_native_sstackp` /
`uk_plat_native_sarg` (`plat/native/arch/x86_64/start.c:9-11`,
`plat/native/arch/arm64/start.c:9-11`).

The AP, after low-level bring-up, runs `uk_lcpu_entry_default`
(`lib/uklcpu/lcpu.c:253`). That function calls `uk_lcpu_init(this_lcpu)` then
reads the per-CPU sentry/sstackp/sarg and, if a custom entry was supplied,
jumps to it (`lib/uklcpu/lcpu.c:268-272`):

```c
sentry  = uk_pcpuvar_current_get(UK_LCPU_SENTRY_SYM);
sstackp = uk_pcpuvar_current_get(UK_LCPU_SSTACKP_SYM);
sarg    = uk_pcpuvar_current_get(UK_LCPU_SARG_SYM);
if (sentry && sentry != (__uptr)uk_lcpu_entry_default)
        uk_arch_jump_to_with_arg(sstackp, sentry, sarg);   /* does not return */
```

**CRITICAL CONSTRAINT:** the argument delivered to the custom entry is
`sarg = (__uptr)lcpu`, i.e. a `struct uk_lcpu *` — it is hard-coded by
`uk_lcpu_start` and **cannot** be set to an arbitrary caller value. So the A3
secondary entry must have the shape:

```c
__noreturn void uk_sched_ap_entry(struct uk_lcpu *this_lcpu);
```

and derive everything else (which scheduler to start, this CPU's index) from
per-CPU state — see Q3/Q6. The entry **must not return**.

---

## Q2 — MP discovery: is `uk_lcpu_mp_init()` called during boot?

**YES — it is already called during normal platform boot.** Signature
(`lib/uklcpu/include/uk/lcpu.h:204`, def `lib/uklcpu/lcpu.c:224`):

```c
int uk_lcpu_mp_init(unsigned long run_irq, unsigned long wakeup_irq);
```

Call sites (whole-tree grep):
- `plat/kvm/x86/setup.c:150` — `uk_lcpu_mp_init(CONFIG_LIBUKLCPU_RUN_IRQ, CONFIG_LIBUKLCPU_WAKEUP_IRQ)`
- `plat/kvm/arm/setup.c:110` — same.

Both are inside `#if CONFIG_HAVE_SMP`, run on the BSP after the interrupt
controller is probed and before the boot stack / `uk_boot_entry`. It asserts
`uk_lcpu_current_is_bsp()` (`lib/uklcpu/lcpu.c:228`) and registers the
RUN/WAKEUP IPI handlers.

**Implication:** A3's AP-bring-up driver does **NOT** need to call
`uk_lcpu_mp_init` itself (the plan's "must call it on the BSP" caveat does not
apply for kvm x86/arm — it is already done). It only needs `CONFIG_HAVE_SMP=y`.

Note on CPU **count/discovery**: there is no public `uk_lcpu_count()` getter in
`lib/uklcpu`. Per-CPU slots are addressed by linear index via
`uk_pcpuvar_lval(idx, sym)` (`lib/ukpcpuvar/include/uk/pcpuvar.h:65`), and CPU
discovery (MADT/ACPI etc.) is platform-internal. The A3 driver therefore needs
a platform-provided online-CPU count / valid `lcpuidx` set to build the
`lcpuidx[]`/`sp[]`/`entry[]` arrays — flagged as a small open dependency for
the bring-up driver (Task 4).

---

## Q3 — Per-CPU "current scheduler"

`uk_sched_current()` is an inline that derives the scheduler from the **current
thread** (`lib/uksched/include/uk/sched.h:53`):

```c
static inline struct uk_sched *uk_sched_current(void)
{
        struct uk_thread *th = uk_thread_current();
        if (th)
                return th->sched;
        return NULL;
}
```

The current thread pointer **IS per-CPU**:
`extern __uk_pcpuvar struct uk_thread *__uk_sched_thread_current;`
(`lib/uksched/include/uk/thread.h:97`, defined `lib/uksched/sched.c:50`).
`uk_thread_current()` reads this per-CPU symbol.

The only **global** scheduler state is the registration list head
`struct uk_sched *uk_sched_head;` (`lib/uksched/sched.c:48`) used by
`uk_sched_register` — this is just a linked list of all created schedulers, not
a "current" pointer.

**Conclusion:** there is **no dedicated per-CPU current-scheduler variable**,
but one is **not needed** — "current scheduler" already resolves per-CPU
through `__uk_sched_thread_current → th->sched`. As long as each CPU's running
thread belongs to that CPU's scheduler, `uk_sched_current()` returns the right
instance automatically. `uk_sched_start()` sets `__uk_sched_thread_current`
for the calling CPU (`lib/uksched/sched.c:238`), so running it on each AP makes
that AP's current scheduler correct with no new per-CPU field.

---

## Q4 — pthread → scheduler binding

**`CONFIG_LIBPTHREAD_EMBEDDED` does NOT exist in this pinned core tree** —
`grep -rn LIBPTHREAD_EMBEDDED .deps/src/unikraft` returns nothing, and there is
no in-tree `lib/pthread*`. The current application build uses the external
Unikraft `lib-musl` port as both libc and pthread provider. This distinction is
important: the official `lib-musl` README says musl provides thread support
natively and “must not be built with” `pthread-embedded`:
https://github.com/unikraft/lib-musl#readme.

The current build's symbol graph confirms the provider and call path:

```text
appllama_cpu.ld.o:  U pthread_create
libmusl.ld.o:       T __pthread_create
libmusl.ld.o:       W pthread_create
libmusl.ld.o:       U __clone
libmuslglue.ld.o:   T __clone
final image:        T uk_syscall_r_clone
final image:        t uk_clone
```

The x86-64 `__clone` wrapper calls `uk_syscall_e_clone` at
`.unikraft/libs/musl/arch/x86_64/__clone.S:52`. This disproves the later,
stale claim that the current pthread implementation bypasses the
`lib/posix-process/clone.c` path.

What core Unikraft provides, and what musl's `pthread_create` ultimately hits,
is the `clone()` syscall in **`lib/posix-process`**. In upstream
`RELEASE-0.21.0`, new threads are bound to the scheduler as follows
(`lib/posix-process/clone.c:143` and `:400`):

```c
s = uk_sched_current();              /* clone.c:143  */
...
ret = uk_sched_thread_add(s, child); /* clone.c:400  */
```

(`fork`/`process.c:406,458` use the same `uk_sched_current()` + `uk_sched_thread_add`.)

**Conclusion:** a newly created thread joins **the current CPU's scheduler**
(whichever LCPU executed `clone`), via `uk_sched_thread_add`. It does **NOT**
go to a single fixed/default scheduler. BUT in llama.cpp all worker pthreads are
spawned from the **main thread, which runs on the BSP** — so with naive
per-LCPU schedulers every worker would pile onto the BSP scheduler and never
spread. For the narrowed 2026-06-13 goal, this is the key kernel fact: some
explicit placement hook is required, but it does **not** need to be a
generalized Linux-style affinity/migration implementation if a bounded
create-time placement hook is enough for llama.

That hook now exists in the project-patched tree:

```c
#if CONFIG_LIBUKSCHEDCOOP_SMP
	s = uk_schedcoop_smp_pick();
#else
	s = uk_sched_current();
#endif
...
ret = uk_sched_thread_add(s, child);
```

References:
- Project patch:
  `.deps/src/unikraft/lib/posix-process/clone.c:145-154,410-411`.
- Round-robin implementation:
  `.deps/src/unikraft/lib/ukschedcoop/smp.c:254-272`.
- Upstream comparison:
  https://github.com/unikraft/unikraft/blob/RELEASE-0.21.0/lib/posix-process/clone.c
- ggml pthread mapping and worker creation:
  `.deps/src/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c:435,465,3286`.

This source graph proves that the placement hook is reachable in the current
image. It does **not** prove that each worker executes on the selected LCPU.
That requires runtime instrumentation of the selected scheduler in
`uk_clone()`, the target scheduler/LCPU in `schedcoop_thread_add()`, and the
actual LCPU at `ggml_graph_compute_secondary_thread` entry.

---

## Q5 — Patch application method (`scripts/deps.py`)

`scripts/deps.py::apply_patches` (`scripts/deps.py:91-106`) iterates every entry
and applies each with **`git apply`** (no `-p` flag → default `-p1`), run with
`cwd=ROOT` against the checkout via `-C <path>`:

```python
already = subprocess.run(
    ["git", "-C", str(path), "apply", "--reverse", "--check", str(patch_abs)], ...)  # idempotency probe
if already.returncode == 0:           # already applied → skip
    continue
run(["git", "-C", str(path), "apply", str(patch_abs)])                                # deps.py:106
```

- Method: **`git apply`** (NOT `patch -p1`, NOT `git am`).
- Flags on the real apply call: **none** (default strip level `-p1`, no
  `--3way`, no `--index`). Idempotency is achieved by a pre-check
`git apply --reverse --check`, so a patch that already applies in reverse is
skipped.
- It is driven from `config/deps.json` → `unikraft.patches` array

---

## Q6 — musl pthread affinity ABI (2026-06-12)

The built musl tree confirms that pthread affinity wrappers pass the pthread's
kernel TID through the affinity syscalls, so Unikraft must resolve **positive
TIDs**, not only `pid == 0`.

Observed in `.unikraft/build/libmusl/origin/musl-1.2.3/src/sched/affinity.c`:

```c
int pthread_setaffinity_np(pthread_t td, size_t size, const cpu_set_t *set)
{
	return -__syscall(SYS_sched_setaffinity, td->tid, size, set);
}

int pthread_getaffinity_np(pthread_t td, size_t size, cpu_set_t *set)
{
	return -do_getaffinity(td->tid, size, set);
}
```

And the linked musl object exports the wrappers while leaving the syscall
resolvers to Unikraft:

```text
$ nm -An .unikraft/build/libmusl.ld.o | rg 'affinity'
libmusl.ld.o: T pthread_getaffinity_np
libmusl.ld.o: T pthread_setaffinity_np
libmusl.ld.o: T sched_getaffinity
libmusl.ld.o: T sched_setaffinity
libmusl.ld.o: U uk_syscall_r_sched_getaffinity
libmusl.ld.o: U uk_syscall_r_sched_setaffinity
```

Therefore the correct Unikraft-side contract is:

1. `sched_{get,set}affinity(pid=0, ...)` operates on `uk_thread_current()`.
2. `sched_{get,set}affinity(pid>0, ...)` resolves the live pthread by TID.
3. Missing TIDs must fail rather than silently succeed.

References:
- musl source tree:
  https://git.musl-libc.org/cgit/musl/tree/src/sched/affinity.c
- pthread affinity man page:
  https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html
- sched affinity man page:
  https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html
  (`config/deps.json:20-24`), applied after checkout in
  `apply_patches(path, entry["patches"])` (`scripts/deps.py:123-124`).
- Existing patches are plain `git diff` format with `a/`…`b/` prefixes
  (e.g. `patches/unikraft/0001-pal-ectx-extern-c-linkage.patch`).

**Implication for Task 6:** export the A3 patch as a **`git diff` / `git
format-patch`-style unified diff with `a/` `b/` prefixes** (so default `-p1`
applies it), add it to `config/deps.json`'s `unikraft.patches`. Must apply
cleanly with a plain `git apply` and also cleanly reverse (for the idempotency
check). Do **not** rely on `git am` mailbox semantics or 3-way merge.

---

## Q6 — `struct uk_lcpu` index field

`struct uk_lcpu` is defined at **`lib/uklcpu/include/uk/lcpu.h:95`**:

```c
struct __align(UK_ARCH_CACHE_LINE_SIZE) uk_lcpu {
        volatile int state __align(8);   /* lcpu.h:99  */
        int error_code;                  /* lcpu.h:104 */
#if CONFIG_HAVE_SMP
        struct uk_lcpu_func fn;          /* lcpu.h:111 */
#endif
};
```

**There is NO `->idx` field (nor `->id`).** The plan's smoke-spike assumption of
`lcpu->idx` is **WRONG** — the struct holds only `state`, `error_code`, and
(under SMP) `fn`. NOT FOUND after searching the struct, all `lib/uklcpu`
headers, and `exportsyms.uk`.

**How to get the current LCPU's index instead** — use the per-CPU index symbol
in `lib/ukpcpuvar`:

```c
extern __uk_pcpuvar __u64 uk_pcpuvar_cpu_idx;   /* pcpuvar.h:36  "Linear index of the current CPU in the per-CPU array" */
extern __uk_pcpuvar __u64 uk_pcpuvar_cpu_id;    /* pcpuvar.h:33  hardware CPU id */

__u64 my_idx = uk_pcpuvar_current_get(uk_pcpuvar_cpu_idx);
```

(`uk_pcpuvar_current_get` — `lib/ukpcpuvar/include/uk/pcpuvar.h:78`.) This is
exactly how `uk_lcpu_start` itself reads the executing CPU:
`uk_pcpuvar_current_get(uk_pcpuvar_cpu_id)` (`lib/uklcpu/lcpu.c:301`).

To get the current `struct uk_lcpu *` use `uk_lcpu_get_current()`
(`lib/uklcpu/lcpu.c:49`, exported `exportsyms.uk:1`), which returns
`uk_pcpuvar_current_ptr_get(uk_lcpus)`. There is **no** `uk_lcpu_idx()` /
`uk_lcpu_id()` helper in `exportsyms.uk` (the only idx-flavoured export is
`uk_lcpu_get_current_idx_in_except`, which is an exception-stack offset hack,
NOT a logical CPU index — do not use it for placement).

---

## Design impact (per question, for Tasks 3–5)

- **Q1:** The A3 secondary entry must be `__noreturn void f(struct uk_lcpu *)`;
  it receives the lcpu pointer (sarg is fixed). It must derive its scheduler and
  index from per-CPU state, never return, and the bring-up driver must build
  parallel `lcpuidx[]/sp[]/entry[]` arrays with per-CPU stacks.
- **Q2:** No need to call `uk_lcpu_mp_init` from A3 code on kvm x86/arm — it is
  already invoked in `plat/*/setup.c` under `CONFIG_HAVE_SMP`. Just ensure
  `CONFIG_HAVE_SMP=y`. Driver still needs a CPU-count source to size arrays.
- **Q3:** No new per-CPU current-scheduler field required. Run
  `uk_sched_start()` on each AP; `uk_sched_current()` then resolves correctly
  per-CPU via `__uk_sched_thread_current → th->sched`.
- **Q4:** clone-created (pthread) threads attach to `uk_sched_current()`. Since
  all llama.cpp workers are spawned on the BSP, Task 5 MUST add an explicit
  round-robin/placement hook to spread threads across the per-LCPU schedulers;
  the default binding will NOT spread them.
- **Q5:** Task 6 must ship the patch as a plain `git apply -p1` unified diff
  with `a/`/`b/` prefixes (reversible), registered in `config/deps.json`
  `unikraft.patches`.
- **Q6:** Replace any `lcpu->idx` usage with
  `uk_pcpuvar_current_get(uk_pcpuvar_cpu_idx)` (and `uk_lcpu_get_current()` for
  the struct pointer). The `->idx` field does not exist.

---

## Go / No-Go

All 6 questions are answered against real source. **Verdict: GO**, with two
mandatory plan amendments (neither is a blocker):

1. **GO** for the core shape: per-LCPU `uk_schedcoop_create` + a secondary
   bootstrap that calls `uk_sched_start` (which is already per-CPU-safe — it
   sets the per-CPU `__uk_sched_thread_current`) + an AP bring-up driver using
   `uk_lcpu_start`. `uk_sched_current()` is inherently per-CPU (Q3), and
   `uk_lcpu_mp_init` is already wired in boot (Q2).

2. **AMENDMENT A (Q6) — loud flag:** the smoke-spike's `lcpu->idx` is INVALID;
   `struct uk_lcpu` has no idx field. Use
   `uk_pcpuvar_current_get(uk_pcpuvar_cpu_idx)`. Any code already written
   against `->idx` will not compile and must be changed.

3. **AMENDMENT B (Q4) — loud flag:** round-robin thread pinning is NOT optional.
   Because workers are created on the BSP and `clone` binds to the *creating*
   CPU's scheduler, Task 5's explicit placement hook is load-bearing — without
   it threads never leave the BSP and the whole A3 benefit is lost.

4. **Minor (Q1):** the secondary entry signature is fixed to
   `void f(struct uk_lcpu *)`; design the bootstrap around the lcpu pointer, not
   a custom argument. (Q2) the bring-up driver needs an online-CPU count source.

No finding fundamentally blocks the A3 design.
