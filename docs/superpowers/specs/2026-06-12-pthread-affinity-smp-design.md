# Pthread Affinity for Per-LCPU SMP Design

**Status:** Approved in conversation on 2026-06-12

## Goal

Make the existing musl-backed pthread path place ggml CPU workers on distinct
Unikraft vCPUs through a real per-thread affinity contract, while preserving
the current per-LCPU cooperative schedulers and leaving upstream llama.cpp
unchanged.

The implementation must first establish runtime evidence for the current
placement failure. It must not assume that the existing global clone
round-robin is working or that placement alone explains the measured slowdown.

## Current State

The current build resolves:

```text
ggml_thread_create
  -> musl pthread_create
  -> libmuslglue __clone
  -> uk_syscall_e_clone / uk_syscall_r_clone
  -> uk_clone
  -> uk_sched_thread_add
```

The project already:

- boots one `ukschedcoop` instance per vCPU;
- keeps a scheduler registry in `lib/ukschedcoop/smp.c`;
- selects a scheduler with `uk_schedcoop_smp_pick()` from patched
  `uk_clone()`;
- supports remote run-queue insertion and `uk_lcpu_wakeup()`.

However, Unikraft `RELEASE-0.21.0` implements `sched_getaffinity()` and
`sched_setaffinity()` as stubs. `sched_getaffinity()` reports only CPU 0, while
`sched_setaffinity()` returns success without changing placement.

ggml already computes a CPU mask for each worker and calls its platform
affinity helper from `ggml_graph_compute_secondary_thread()`. Therefore the
missing reusable mechanism is below pthread, not a new ggml thread API.

## Official Reference Model

The design follows these properties of Linux affinity:

1. CPU affinity is a persistent per-thread property.
2. A thread may execute only on CPUs in the intersection of its requested mask
   and the currently online CPU set.
3. An empty effective mask is rejected.
4. Thread placement is scheduler state, not a global `clone()` sequence.
5. Remote enqueue and migration serialize access to the thread and involved
   run queues; the target CPU is notified when runnable work arrives.
6. Affinity can be selected at thread creation or changed by the thread after
   creation. ggml currently uses the latter.

References:

- Linux scheduler core:
  https://github.com/torvalds/linux/blob/master/kernel/sched/core.c
- Linux CPU masks and online state:
  https://docs.kernel.org/core-api/cpu_hotplug.html
- Linux affinity semantics:
  https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html
- Pthread creation affinity:
  https://man7.org/linux/man-pages/man3/pthread_attr_setaffinity_np.3.html
- musl pthread creation:
  https://git.musl-libc.org/cgit/musl/tree/src/thread/pthread_create.c
- Unikraft musl integration:
  https://github.com/unikraft/lib-musl
- ggml CPU threadpool:
  https://github.com/ggml-org/llama.cpp/blob/master/ggml/src/ggml-cpu/ggml-cpu.c

## Chosen Architecture

### 1. Evidence Before Migration

Add a debug-only placement trace at three boundaries:

- `uk_clone()`: parent LCPU, selected scheduler, child TID;
- `schedcoop_thread_add()`: caller LCPU, target scheduler LCPU, child TID;
- worker entry or a dedicated test worker: actual LCPU and assigned scheduler.

Each event uses a stable machine-parseable prefix. Logging is controlled by a
Kconfig debug option and is disabled in performance measurements.

This distinguishes:

- incorrect scheduler selection;
- correct selection but incorrect queue ownership;
- correct queue ownership but execution on the wrong LCPU;
- correct placement with remaining synchronization overhead.

### 2. Scheduler Registry API

`lib/ukschedcoop/smp.c` remains the owner of the per-LCPU scheduler registry.
It exposes narrow lookup helpers:

```c
unsigned int uk_schedcoop_smp_online_count(void);
struct uk_sched *uk_schedcoop_smp_sched(unsigned int lcpu_idx);
int uk_schedcoop_smp_lcpu(const struct uk_sched *sched);
```

Lookup rejects an out-of-range or offline LCPU. The registry must publish a
scheduler only after its AP scheduler is ready to accept runnable threads.

### 3. Per-Thread Affinity State

Each `uk_thread` receives an affinity mask sized for
`CONFIG_UKPLAT_CPU_MAXCOUNT`. The default mask contains every online scheduler,
matching Linux's default of allowing execution on all available CPUs.

The state is initialized before the thread becomes runnable and remains valid
for the thread lifetime. Affinity is separate from the current scheduler:

- affinity says where the thread is allowed to execute;
- `thread->sched` says which per-LCPU scheduler currently owns it.

For the first implementation, each `ukschedcoop` thread has one owner
scheduler and does not migrate automatically for load balancing.

### 4. Affinity Syscalls

Replace the success-returning stubs with real behavior:

- `pid == 0` addresses the calling thread;
- positive TIDs are resolved through `lib/posix-process`;
- unsupported process-wide PID semantics return an explicit error;
- short or invalid masks return `-EINVAL`;
- missing TIDs return `-ESRCH`;
- effective mask is `requested & online`;
- an empty effective mask returns `-EINVAL`;
- `sched_getaffinity()` returns the stored effective mask.

This requires a small scheduler-facing thread lookup hook rather than making
`lib/uksched` depend directly on private `lib/posix-process` internals. The
exact dependency direction must follow the existing syscall/library pattern
confirmed during implementation.

### 5. Target Selection

When an affinity mask contains multiple online CPUs:

1. keep the current owner if it is allowed;
2. otherwise choose the lowest-numbered allowed online LCPU.

This deterministic policy avoids introducing an untested load balancer.
ggml's strict per-worker masks contain one CPU, so its target is unambiguous.

### 6. Migration Protocol

Migration supports the path ggml uses: a running worker calls
`pthread_setaffinity_np(pthread_self(), ...)` before entering its work loop.

The operation must:

1. validate and store the new effective mask;
2. identify the target scheduler;
3. serialize the source and target queue state in a fixed LCPU-index order;
4. move scheduler ownership without leaving the thread on two queues;
5. wake the target LCPU;
6. force the current thread to stop executing on a now-disallowed LCPU.

The first implementation may restrict migration to the calling thread and to a
thread that has not entered a runnable queue on another CPU. Remote migration
of an independently running thread is out of scope unless required by musl's
`pthread_setaffinity_np` wrapper in the pinned build.

For self-migration, the syscall returns only after the thread has resumed on
an allowed LCPU. Returning to user code on the old LCPU is a correctness bug.

### 7. ggml Configuration

The CPU appliance configures its existing ggml threadpool parameters:

- `n_threads = CONFIG_APP_LLAMA_CPU_THREADS`;
- cpumask enables LCPUs `0..n_threads-1`;
- `strict_cpu = true`;
- `poll` remains independently configurable.

ggml already maps the global mask into per-worker masks and invokes its
affinity helper from each worker. No source change is made under
`.deps/src/llama.cpp`.

In pinned llama.cpp `b9581`, secondary workers are created and assigned masks
before the main worker receives its mask. For four workers and mask `0xf`, the
deterministic assignment is secondary workers `ith=1,2,3` to LCPUs `0,1,2`
and main worker `ith=0` to LCPU3. The required invariant is one distinct
allowed LCPU per active worker, not `ith == lcpu`.

Server and bench entry points must use the same threadpool policy. If the
server path constructs threadpool parameters through upstream common code,
the VOGUE entry point supplies equivalent command-line CPU-mask settings rather
than patching llama.cpp.

### 8. Clone Fallback

The unconditional global round-robin in `uk_clone()` is retained only during
the evidence phase.

After affinity migration works:

- default clone placement returns to `uk_sched_current()`, matching upstream;
- a child inherits the parent's allowed affinity mask;
- explicit affinity moves or constrains the worker;
- no correctness property depends on unrelated HTTP or lwIP thread creation
  order.

This prevents non-ggml pthreads from perturbing worker placement.

## Testing Strategy

### Host Tests

Add pure tests for:

- mask validation and online-mask intersection;
- deterministic target choice;
- current-owner retention when allowed;
- empty-mask rejection;
- offline scheduler rejection;
- one-hot ggml worker masks for `N` workers and `N` vCPUs.

### Guest Microbenchmark

Create a small CPU-only pthread placement probe independent of llama.cpp. It:

1. starts `N` pthreads;
2. assigns worker `i` to LCPU `i`;
3. records actual LCPU repeatedly during a timed compute loop;
4. uses an atomic start barrier so all workers overlap;
5. fails if a worker executes on a disallowed LCPU;
6. reports a per-LCPU progress counter.

Required cases:

- SMP1, one worker;
- SMP4, four one-hot workers;
- invalid empty mask;
- mask containing only an offline/out-of-range CPU;
- mask with multiple CPUs;
- repeated create/join cycles to expose stale queue ownership.

### Llama Verification

Run the same model and benchmark parameters for:

- SMP1, one ggml thread;
- SMP4, four ggml threads with strict one-worker-per-vCPU affinity;
- SMP4, affinity disabled as a control.

Correctness gate:

- all worker traces show the intended LCPU;
- no hangs, assertions, duplicate queue membership, or allocator corruption;
- repeated runs produce the same placement.

Performance gate:

- debug tracing disabled;
- at least five runs per configuration;
- report median `pp512` and `tg128`;
- do not claim success unless SMP4 `pp512` exceeds the SMP1 baseline;
- if placement is correct but performance is not, profile barrier, wakeup, and
  memory-bandwidth costs as a separate follow-up.

### Regression Gates

- `make test-fast`;
- CPU bench build from the current dependency tree;
- clean patch reapplication from pinned Unikraft `RELEASE-0.21.0`;
- CPU server build;
- existing Vulkan/virtio-gpu tests;
- `git diff` confirms no llama.cpp source patch.

## Failure Handling

- Never return success for an affinity operation that was ignored.
- Never enqueue one thread on two scheduler run queues.
- Never allow a running thread to continue on a CPU excluded by its effective
  mask after a successful self-affinity call.
- If target scheduler readiness changes during placement, return an error or
  retry against the current online registry; do not silently fall back to BSP
  when strict affinity was requested.
- Instrumentation remains available behind Kconfig until placement and
  migration are stable.

## Scope Exclusions

- General Linux-compatible load balancing.
- Preemptive scheduling.
- NUMA memory placement.
- CPU hot-unplug after application start.
- Vulkan inference speedups.
- Modifying upstream llama.cpp.
- Supporting more CPU-bound ggml workers than available vCPUs.

## Success Criteria

1. The pthread placement probe proves four concurrent workers remain on four
   distinct LCPUs under `-smp 4`.
2. `sched_setaffinity()` and `sched_getaffinity()` no longer lie about success
   or CPU availability.
3. ggml uses its existing affinity path with strict one-worker-per-vCPU masks.
4. Default non-affined pthread creation no longer depends on global clone
   round-robin.
5. The CPU benchmark is correct and reproducible; performance claims are made
   only from trace-free repeated measurements.
6. Existing host, server, and virtio-gpu/Vulkan regression gates pass.
