# Multi-vCPU Speedup for llama.cpp bench + server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `llama-bench` and `llama-server` run their ggml intra-op workers across multiple guest vCPUs under Unikraft and beat the single-vCPU baseline — bench first, then server, intra-op parallelism only.

**Architecture:** The per-LCPU cooperative scheduler, affinity syscalls, and per-worker placement (`uk_schedcoop_smp_place_current`) are already implemented and working. The remaining gap is that the worker count is fixed at build time (`@@VOGUE_SMP@@`) and does not match the launch-time vCPU count, which both prevents the speedup and **deadlocks** the cooperative scheduler when polling workers > online vCPUs. We make the worker count a runtime decision equal to the online vCPU count, verify one-worker-per-vCPU placement at runtime, then measure.

**Tech Stack:** Unikraft `RELEASE-0.21.0` (`uklcpu`, `ukschedcoop`, `uksched` affinity syscalls), llama.cpp `b9581` ggml CPU backend (patched `ggml-cpu.c` Unikraft affinity path), QEMU/KVM `-smp`, VOGUE Python harness (`scripts/app-llama-cpu.py`), Python `unittest`.

---

## Conditions for a multi-vCPU speedup (the design contract)

These are the conditions every task below serves. A task is only worth doing if it advances one of these.

**Correctness — workers actually run on distinct vCPUs:**
- **C1.** N vCPUs online with N per-LCPU schedulers. ✅ done (`-smp N`, `CONFIG_UKPLAT_CPU_MAXCOUNT≥N`).
- **C2.** `n_threads` and the affinity mask **equal the online vCPU count** — one worker per vCPU, mask `{0..N-1}`. ❌ currently fixed at build-time `@@VOGUE_SMP@@`, mismatches launch `--smp`.
- **C3.** Placement takes effect before compute and is stable: every worker's `sched_getcpu()` == its target.

**Speedup — distribution alone is not enough:**
- **S1.** True simultaneous execution (each vCPU = a KVM host thread; host has the cores). ✅
- **S2.** No blocking handoff in the parallel region — workers poll/spin, never sleep, between graph nodes. ✅ polling threadpool (`poll=100`).
- **S3.** Per-worker work ≫ cross-LCPU barrier cost (tiny models lose; report pp and tg separately).
- **S4.** **No oversubscription — a hard correctness rule here.** Polling workers > online vCPUs on a cooperative scheduler = guaranteed deadlock (spinning workers never yield). ⟹ `n_threads` MUST equal online vCPUs. This is the current `blocked:no-pass-marker` hang.
- **S5.** Memory-bandwidth ceiling: `pp512` (compute-bound matmul) is where the win is real; `tg128` (bandwidth-bound) may not scale.

**The core fix is C2+S4: make `n_threads` and the cpumask track the *online* vCPU count at runtime.** Everything else is verification and applying the same path to the server.

---

## File Map

- Modify: `apps/app-llama-cpu/llama-cpu-common.h` — add a runtime `uk_llama_cpu_online_vcpus()` helper; stop hardwiring `UK_LLAMA_CPU_WORKER_THREADS` to the build-time macro.
- Modify: `apps/app-llama-cpu/bench.cpp` — set `n_threads` / threadpool size / cpumask from the runtime vCPU count.
- Modify: `apps/app-llama-cpu/llama-server-entry.cpp` — same runtime thread/mask policy as bench (Task 5).
- Modify: `scripts/app-llama-cpu.py` — already passes `--smp`; ensure the parser records `pp512`/`tg128`/placement and `server_toks_per_s` (per the 2026-06-14 verification plan).
- Modify: `scripts/tests/test_scripts.py` — lock parser behavior.
- Result artifacts: `results/llama/llama_cpu_smp{1,4}.json`, `results/llama/llama_server_cpu_smp{1,4}.json`.
- Update (stale, trimmed by this turn): `docs/plan-smp-vcpu.md`, `docs/plan-smp-scheduler.md`.

---

## Task 1: Runtime online-vCPU count helper

**Files:**
- Modify: `apps/app-llama-cpu/llama-cpu-common.h`

- [ ] **Step 1: Add the runtime helper**

Replace the build-time `UK_LLAMA_CPU_WORKER_THREADS` macro usage with a runtime query. ggml/musl exposes `sched_getaffinity`, which now returns the real online mask. Add to `llama-cpu-common.h`:

```c
#include <sched.h>
#include <uk/schedcoop.h>   /* uk_schedcoop_smp_online_count() */

/* Number of vCPUs actually online at runtime. One ggml worker per vCPU is the
 * hard ceiling: polling workers > online vCPUs deadlock the cooperative
 * scheduler (spinning workers never yield). See plan condition S4. */
static inline unsigned int uk_llama_cpu_online_vcpus(void)
{
    unsigned int n = uk_schedcoop_smp_online_count();
    return n ? n : 1;
}
```

- [ ] **Step 2: Build to confirm the symbol resolves**

Run: `make llama-cpu-bench-build`
Expected: build exits 0 (links `uk_schedcoop_smp_online_count`, already in `exportsyms.uk`).

- [ ] **Step 3: Commit**

```bash
git add apps/app-llama-cpu/llama-cpu-common.h
git commit -m "feat(llama-cpu): runtime online-vCPU count helper"
```

## Task 2: Bench uses runtime worker count + one-hot cpumask

**Files:**
- Modify: `apps/app-llama-cpu/bench.cpp`

- [ ] **Step 1: Derive worker count and mask at runtime**

In `main()`, before building `cparams`, replace the fixed `UK_LLAMA_CPU_WORKER_THREADS` uses:

```c
    const unsigned int nvcpu = uk_llama_cpu_online_vcpus();
    cparams.n_ctx            = 512 + 128;
    cparams.n_batch          = 512;
    cparams.n_threads        = (int)nvcpu;
    cparams.n_threads_batch  = (int)nvcpu;
```

- [ ] **Step 2: Build a strict one-hot threadpool mask**

Replace the default threadpool params so each worker is pinned to a distinct vCPU `{0..nvcpu-1}`:

```c
    struct ggml_threadpool_params tpp =
        ggml_threadpool_params_default((int)nvcpu);
    for (uint32_t i = 0; i < GGML_MAX_N_THREADS; i++)
        tpp.cpumask[i] = (i < nvcpu);
    tpp.strict_cpu = true;   /* honor the mask; one worker per vCPU */
    tpp.poll       = 100;    /* never sleep (S2) */
    struct ggml_threadpool *tp = ggml_threadpool_new(&tpp);
    if (tp)
        llama_attach_threadpool(ctx, tp, tp);
```

- [ ] **Step 3: Build**

Run: `make llama-cpu-bench-build`
Expected: exits 0; `.unikraft/build/vogue-llama-cpu_qemu-x86_64` exists.

- [ ] **Step 4: Smoke test SMP=1 does NOT deadlock**

Run: `python3 scripts/app-llama-cpu.py --mode bench --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 1`
Expected: `status: pass`; exactly one worker; no `failed to place ggml worker on cpu 1` warnings (mask is `{0}` only).

- [ ] **Step 5: Commit**

```bash
git add apps/app-llama-cpu/bench.cpp
git commit -m "feat(llama-cpu): bench pins one ggml worker per online vCPU"
```

## Task 3: Verify SMP=4 placement is correct (C3) and beats baseline (S5)

**Files:**
- Modify: `results/llama/llama_cpu_smp1.json`, `results/llama/llama_cpu_smp4.json`

- [ ] **Step 1: Capture SMP=1 baseline**

Run:
```bash
python3 scripts/app-llama-cpu.py --mode bench --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 1
cp results/llama/llama_cpu.json results/llama/llama_cpu_smp1.json
```
Expected: `status: pass`; `metrics.pp512` and `metrics.tg128` present.

- [ ] **Step 2: Capture SMP=4**

Run:
```bash
python3 scripts/app-llama-cpu.py --mode bench --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 4
cp results/llama/llama_cpu.json results/llama/llama_cpu_smp4.json
```
Expected: `status: pass`; **no `blocked:no-pass-marker`**.

- [ ] **Step 3: Assert placement correctness (C3)**

Inspect the SMP=4 guest log embedded in the result. Gate:
- four `ggml-unikraft-place: target=k actual=k` lines for `k=0,1,2,3` (target == actual);
- zero `failed to place ggml worker` warnings.
If `actual != target` for any worker, STOP — the `place_current`+`sched_yield` migration is not resuming on the target LCPU; that is the real defect to fix before measuring (see Task 4).

- [ ] **Step 4: Compare throughput (S5)**

Run:
```bash
python3 - <<'PY'
import json; from pathlib import Path
s1=json.loads(Path("results/llama/llama_cpu_smp1.json").read_text())["metrics"]
s4=json.loads(Path("results/llama/llama_cpu_smp4.json").read_text())["metrics"]
print("pp512 smp1=%s smp4=%s  ratio=%.2f"%(s1["pp512"],s4["pp512"],s4["pp512"]/s1["pp512"]))
print("tg128 smp1=%s smp4=%s  ratio=%.2f"%(s1["tg128"],s4["tg128"],s4["tg128"]/s1["tg128"]))
PY
```
Expected: report both ratios. Success = `pp512` ratio > 1.0. Record `tg128` honestly even if ≤ 1.0 (bandwidth-bound, condition S5).

- [ ] **Step 5: Commit evidence**

```bash
git add results/llama/llama_cpu_smp1.json results/llama/llama_cpu_smp4.json
git commit -m "evidence(smp): bench SMP=1 vs SMP=4 with per-vCPU placement"
```

## Task 4: (Conditional) Fix migration if placement is wrong

Only execute if Task 3 Step 3 shows `actual != target` or a hang.

**Files:**
- Modify: `.deps/src/unikraft/lib/ukschedcoop/smp.c`
- Modify: `.deps/src/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c` (Unikraft `ggml_thread_apply_affinity` path)

- [ ] **Step 1: Trace where the worker resumes**

Add a one-line `uk_pr_info` in `uk_schedcoop_smp_migrate_current_execenv` logging source→target LCPU and the thread pointer. Rebuild, rerun SMP=4, confirm whether the migrate trap fires once per worker.

- [ ] **Step 2: Form one hypothesis and test minimally**

Likely causes, test one at a time (systematic-debugging): (a) `affinity_migrate_pending` set but the `sched_yield` does not reach the execenv migrate trap → ensure ggml's post-place `sched_yield()` lands in the scheduler that checks the pending flag; (b) target run-queue insertion happens but the target LCPU is not woken → add `uk_lcpu_wakeup(target)`; (c) worker resumes on source because barrier spin starts before the yield completes.

- [ ] **Step 3: Re-run Task 3 Step 3 gate**

Expected: four `target==actual` lines, no hang. Then continue to Task 3 Step 4.

- [ ] **Step 4: Commit**

```bash
git add .deps/src/unikraft/lib/ukschedcoop/smp.c .deps/src/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c
git commit -m "fix(smp): ensure ggml worker resumes on its target vCPU"
```

## Task 5: Apply the identical path to llama-server

**Files:**
- Modify: `apps/app-llama-cpu/llama-server-entry.cpp`

- [ ] **Step 1: Use the same runtime thread/mask policy**

Locate where the server sets context/threadpool params. Set `n_threads = n_threads_batch = uk_llama_cpu_online_vcpus()`, build the same one-hot `cpumask`, `strict_cpu=true`, `poll=100`, and attach the threadpool to the server's `llama_context` exactly as Task 2. Do not patch upstream llama.cpp server code — set params in the VOGUE entry point.

- [ ] **Step 2: Build**

Run: `make llama-cpu-server-build`
Expected: exits 0; `.unikraft/build/vogue-llama-cpu-server_qemu-x86_64` exists.

- [ ] **Step 3: SMP=1 server baseline (no deadlock)**

Run:
```bash
python3 scripts/app-llama-cpu.py --mode server --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 1
cp results/llama/llama_server_cpu.json results/llama/llama_server_cpu_smp1.json
```
Expected: `status: pass`; a `READY` line; `metrics.server_toks_per_s` present.

- [ ] **Step 4: Commit**

```bash
git add apps/app-llama-cpu/llama-server-entry.cpp results/llama/llama_server_cpu_smp1.json
git commit -m "feat(llama-cpu): server pins one ggml worker per online vCPU"
```

## Task 6: Verify server SMP=4 placement and throughput

**Files:**
- Modify: `results/llama/llama_server_cpu_smp4.json`

- [ ] **Step 1: Capture SMP=4 server**

Run:
```bash
python3 scripts/app-llama-cpu.py --mode server --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 4
cp results/llama/llama_server_cpu.json results/llama/llama_server_cpu_smp4.json
```
Expected: `status: pass`; `READY`; placement evidence across LCPUs 0..3; `server_toks_per_s` present.

- [ ] **Step 2: Compare**

Run:
```bash
python3 - <<'PY'
import json; from pathlib import Path
g=lambda f: json.loads(Path(f).read_text())["metrics"].get("server_toks_per_s")
print("server toks/s smp1=%s smp4=%s"%(g("results/llama/llama_server_cpu_smp1.json"),
                                        g("results/llama/llama_server_cpu_smp4.json")))
PY
```
Expected: report both. Success = SMP=4 ≥ SMP=1 with placement distributed across vCPUs.

- [ ] **Step 3: Commit evidence**

```bash
git add results/llama/llama_server_cpu_smp4.json
git commit -m "evidence(smp): server SMP=1 vs SMP=4 with per-vCPU placement"
```

## Task 7: Record outcomes, no overstatement

**Files:**
- Modify: this plan (append a "Results" section)

- [ ] **Step 1: Append measured results**

Record four facts separately: bench placement verified y/n; bench `pp512`/`tg128` ratio; server placement verified y/n; server `server_toks_per_s` ratio. State plainly where SMP=4 wins and where it does not (expect `pp512` win, `tg128` possibly flat per S5).

- [ ] **Step 2: Final regression gate**

Run: `python3 -m unittest scripts.tests.test_scripts -v && make test-fast`
Expected: all pass.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/plans/2026-06-14-multi-vcpu-speedup.md
git commit -m "docs(smp): record measured multi-vCPU bench+server outcomes"
```

---

## Scope exclusions

General load balancing; preemptive scheduling; NUMA placement; request-level server concurrency (intra-op only this stage); Vulkan speedups; modifying upstream llama.cpp source beyond the existing `ggml-cpu.c` Unikraft affinity path; more ggml workers than online vCPUs.

## Superseded documents

This plan supersedes the active-placement portions of `docs/plan-smp-vcpu.md`, `docs/plan-smp-scheduler.md`, and `docs/superpowers/plans/2026-06-12-pthread-affinity-smp.md` (kernel affinity now implemented). Those are trimmed to historical pointers.
</content>
