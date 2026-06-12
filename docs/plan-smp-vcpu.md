# Multi-vCPU (SMP) Support for the llama.cpp Appliances — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the VOGUE llama.cpp appliances actually boot and use multiple guest vCPUs under Unikraft `RELEASE-0.21.0`, then measure whether multi-vCPU improves llama.cpp throughput — claiming a win only when a branch-local artifact beats its baseline.

**Architecture:** Unikraft 0.21.0 ships a *complete SMP bring-up layer* (`uklcpu` + `ukpcpuvar` + `ukpal`, AP start via `uk_lcpu_start/run/wait`, IPIs, APIC/ACPI auto-selected by `HAVE_SMP`) but a *single-LCPU cooperative scheduler* (`ukschedcoop`). So the work splits into (Phase 0–2) wiring `-smp N` through the launch harness so a guest built with `CONFIG_UKPLAT_CPU_MAXCOUNT>1` truly boots N vCPUs and measuring the result, and (Phase 3, decision-gated) unlocking real cross-vCPU thread parallelism since the default scheduler will not distribute pthreads on its own.

**Tech Stack:** Unikraft `RELEASE-0.21.0` (`uklcpu`, `ukpcpuvar`, `ukschedcoop`, `libukintctlr/APIC`, `libukacpi`), QEMU `-smp`/`-cpu host`/KVM, llama.cpp `b9581` (ggml CPU + Vulkan/Venus backends), VOGUE Python launch harness (`scripts/common.py`, `scripts/app-llama-*.py`), Python `unittest` host-native gate (`make test-fast`).

---

## Background: verified state of SMP in Unikraft 0.21.0

All claims below were verified against the **exact pinned source** (`config/deps.json` pins `unikraft` at tag `RELEASE-0.21.0`) and the official Unikraft docs/blog. This is the source-of-truth that the repo's evidence-first rule requires.

### What works (SMP infrastructure — complete)

- **CPU-count config.** `plat/Config.uk` defines `config UKPLAT_CPU_MAXCOUNT` — *"Maximum number of supported logical CPUs"*, `range 1 256`, `default 1`. (Note: the symbol is `UKPLAT_CPU_MAXCOUNT`, **not** the older `UKPLAT_LCPU_MAXCOUNT` — it was renamed in the 0.21.0 PAL rework. The VOGUE Kraftfile already uses the correct name.)
- **Auto-enable.** `config HAVE_SMP` is `default y if UKPLAT_CPU_MAXCOUNT > 1` and, on x86_64, `select LIBUKINTCTLR_APIC` and `select LIBUKACPI`. So setting the count > 1 is sufficient to pull in the interrupt-controller + ACPI discovery needed for SMP. There is also `HAVE_CPU_MULTI_PHASE_STARTUP`.
- **Generic logical-CPU lib.** `lib/uklcpu` (`menuconfig LIBUKLCPU`, *"uklcpu: Generic logical CPU interface"*, `default y`) `select LIBUKPAL` + `select LIBUKPCPUVAR`. Public API (`lib/uklcpu/exportsyms.uk`) includes `uk_lcpu_mp_init`, `uk_lcpu_start`, `uk_lcpu_run`, `uk_lcpu_wait`, `uk_lcpu_wakeup`, `uk_lcpu_get_current`, `uk_lcpus`, plus IRQ/ectx/sysctx helpers. (The old `ukplat_lcpu_*` names are gone; the API is now `uk_lcpu_*`.)
- **Per-CPU variables.** The 0.21.0 release notes call out that *"a per-CPU variables library has been introduced"* — this is `LIBUKPCPUVAR`, used e.g. by `uksched` for the current-thread pointer (`extern __uk_pcpuvar struct uk_thread *__uk_sched_thread_current;` in `lib/uksched/include/uk/thread.h`).
- **IPIs.** Under `HAVE_SMP`, `lib/uklcpu/Config.uk` exposes `LIBUKLCPU_RUN_IRQ` (x86_64 default 13) for remote-function execution and `LIBUKLCPU_WAKEUP_IRQ` (x86_64 default 14). This is the inter-processor mechanism (`uk_lcpu_run`/`uk_lcpu_wakeup`).
- **History.** SMP was added incrementally upstream (x86 PR #244, ARM64 PR #373, arch-independent API PR #469) and has been part of releases for several versions; 0.21.0 is the current, PAL-rearchitected form.

### The hard limitation (scheduler — single-LCPU)

- **The default cooperative scheduler runs threads on ONE LCPU.** `lib/ukschedcoop/schedcoop.c` contains, verbatim:
  ```c
  /* NOTE: We only support one processing LCPU (for now) */
  if (proc_id > 0)
          return NULL;
  ```
  in `schedcoop_idle_thread()`. `ukschedcoop` is the only scheduler shipped in `lib/` at this tag.
- **Consequence:** booting `-smp 4` brings the secondary vCPUs *online* (AP start, per-CPU vars, IPIs all function), but threads created through `libpthread_embedded → uksched` are all scheduled on the boot LCPU (LCPU0). The secondary vCPUs idle. Therefore **`llama.cpp --threads 4` does not, by itself, achieve CPU parallelism across vCPUs** on the stock 0.21.0 cooperative scheduler.
- The official architecture doc's line *"each CPU core can run a different scheduler"* describes the design intent / pluggability, not an out-of-the-box SMP run-queue in `ukschedcoop`.

### What this means for the plan

1. Wiring `-smp N` into the launch path is necessary and correct (the guest is already *built* for SMP via the Kraftfile but is *launched* with one vCPU). This is the honest "ship now" step, and it is the prerequisite to **measure** the scheduler ceiling rather than guess it.
2. The real CPU-thread speedup requires crossing the scheduler limitation (Phase 3), which is a kernel-level change and is therefore **decision-gated on Phase 1/2 measurements**, consistent with the repo's stop condition: *no performance win is claimed without a branch-local artifact that beats its baseline.*

### Sources

- Unikraft v0.21.0 release notes — https://unikraft.org/blog/2026-04-20-unikraft-releases-v0.21.0 (per-CPU variables library; PAL/arch rework)
- "Adding SMP support" / synchronization blog — https://unikraft.org/blog/2022-07-19-unikraft-synchronization
- SMP API PR (arch-independent) — https://github.com/unikraft/unikraft/pull/469 ; x86 SMP — https://github.com/unikraft/unikraft/pull/244 ; ARM64 SMP — https://github.com/unikraft/unikraft/pull/373
- Pinned source at the tag: `plat/Config.uk`, `lib/uklcpu/Config.uk`, `lib/uklcpu/exportsyms.uk`, `lib/ukschedcoop/schedcoop.c`, `lib/uksched/include/uk/thread.h` (Unikraft `RELEASE-0.21.0`)
- Unikraft architecture/performance docs — https://unikraft.org/docs/internals/architecture , https://unikraft.org/docs/concepts/performance

---

## The exact branch gap (verified on `dev-jerry`, 2026-06-11)

| Layer | Current state | File evidence |
|---|---|---|
| VK-server build | **Already built for SMP**: `CONFIG_UKPLAT_CPU_MAXCOUNT: '4'`, `CONFIG_APP_LLAMA_VK_THREADS: '4'`, `--parallel 4` | `kraft/Kraftfile.llama-vk-server:45-51` |
| VK-server launch | **No `-smp`** → boots 1 vCPU | `scripts/app-llama-vk.py:57-58` (`qemu_command`: `*machine_and_cpu_args(...)`, no smp) |
| CPU bench/server build | **Single-vCPU**: no `CONFIG_UKPLAT_CPU_MAXCOUNT`, `CONFIG_APP_LLAMA_CPU_THREADS: '1'` | `kraft/Kraftfile.llama-cpu*:` |
| CPU launch | **No `-smp`** → boots 1 vCPU | `scripts/app-llama-cpu.py` (`qemu_command`: no smp) |
| Shared QEMU builder | `machine_and_cpu_args()` emits `-machine`/`-cpu` only; no smp helper exists | `scripts/common.py:68-74` |
| Server runtime proof | `concurrency=1`, READY line records `threads=1` historically | `results/llama/server_vk_throughput.json`, entry at `apps/app-llama-vk/llama-vk-server-entry.cpp:58-61` |

The single missing primitive is a `-smp` flag in the launch harness, plus (for the CPU appliances) the matching `CONFIG_UKPLAT_CPU_MAXCOUNT` build config. Everything downstream is measurement and the scheduler decision.

---

## File structure

**New / modified files (Phase 0–2):**

- Modify `scripts/common.py` — add one shared `smp_args(count)` helper (single responsibility: build the `-smp` fragment). Keeps the launch policy in the same place as `machine_and_cpu_args`.
- Modify `scripts/tests/test_scripts.py` — host-native unit tests for `smp_args` and for `-smp` appearing in each `qemu_command`.
- Modify `scripts/app-llama-vk.py` — thread an `smp` value (CLI `--smp` + `VOGUE_SMP` env, default 1) into `qemu_command`, record it in `inputs`.
- Modify `scripts/app-llama-cpu.py` — same wiring as the VK runner.
- Modify `kraft/Kraftfile.llama-cpu-bench` (and `Kraftfile.llama-cpu`, `Kraftfile.llama-cpu-server`) — add `CONFIG_UKPLAT_CPU_MAXCOUNT` so the CPU appliance is *built* for SMP, mirroring the VK-server Kraftfile.
- New `docs/results-smp/` (git-tracked) — boot logs + before/after JSON copies that back each measurement claim.

**Phase 3 (decision-gated, research track):**

- New `apps/app-smp-smoke/` — a minimal guest appliance that calls `uk_lcpu_run` to prove a function executes on a secondary LCPU (the spike that decides the unlock variant).
- Then one of: A3 (per-LCPU `ukschedcoop` instances + thread pinning), A1 (`lib/ukschedcoop` made per-LCPU), A2 (new SMP/preemptive `uksched` lib), or B (ggml threadpool → `uk_lcpu_run` offload shim). Exact files chosen after the spike — see Phase 3 Task 3.2.

Design rule (matches AGENTS.md): keep apps one-image-one-purpose; no `fork()`/`exec()`/shell launchers; prefer existing Makefile wrappers; `uk_*` prefix for project symbols.

---

## Phase 0 — Boot multiple vCPUs through the launch harness

Goal of this phase: a guest built for SMP actually boots N vCPUs, proven by a captured boot log, with the change covered by host-native tests (`make test-fast`). No performance claim yet.

### Task 0.1: Add the `smp_args` helper (TDD)

**Files:**
- Modify: `scripts/common.py` (near `machine_and_cpu_args`, ~line 68)
- Test: `scripts/tests/test_scripts.py`

- [ ] **Step 1: Write the failing test**

Add to the `CommonTests` class in `scripts/tests/test_scripts.py`:

```python
    def test_smp_args_omitted_for_single_vcpu(self) -> None:
        self.assertEqual(common.smp_args(1), [])
        self.assertEqual(common.smp_args(0), [])

    def test_smp_args_emits_flag_for_multiple_vcpus(self) -> None:
        self.assertEqual(common.smp_args(4), ["-smp", "4"])
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd /mydata/JerryT/vm-final-project/virtio-gpu && python3 -m pytest scripts/tests/test_scripts.py -k smp_args -v`
Expected: FAIL with `AttributeError: module 'common' has no attribute 'smp_args'`
(If `pytest` is unavailable, use: `python3 -m unittest scripts.tests.test_scripts -v` — same failure.)

- [ ] **Step 3: Implement the helper**

Add to `scripts/common.py` immediately after `machine_and_cpu_args`:

```python
def smp_args(count: int) -> list[str]:
    """QEMU -smp fragment for booting `count` guest vCPUs.

    count <= 1 returns [] so the historical single-vCPU command (and the
    baselines captured with it) is preserved byte-for-byte unless SMP is
    explicitly requested. The guest must additionally be built with
    CONFIG_UKPLAT_CPU_MAXCOUNT >= count for the extra vCPUs to come online.
    """
    if count <= 1:
        return []
    return ["-smp", str(count)]
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 -m pytest scripts/tests/test_scripts.py -k smp_args -v`
Expected: PASS (2 passed)

- [ ] **Step 5: Commit**

```bash
git add scripts/common.py scripts/tests/test_scripts.py
git commit -m "feat(smp): add shared smp_args QEMU helper"
```

### Task 0.2: Thread `--smp` into the VK runner (TDD)

**Files:**
- Modify: `scripts/app-llama-vk.py:47-69` (`qemu_command`) and `scripts/app-llama-vk.py:105-121` (`main`)
- Test: `scripts/tests/test_scripts.py` (`CommandTests`)

- [ ] **Step 1: Write the failing test**

Add to `CommandTests` in `scripts/tests/test_scripts.py`:

```python
    def test_vk_smp_flag_present_only_when_multivcpu(self) -> None:
        one = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64", smp=1)
        four = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64", smp=4)
        self.assertNotIn("-smp", one)
        self.assertEqual(four[four.index("-smp") + 1], "4")
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m pytest scripts/tests/test_scripts.py -k vk_smp -v`
Expected: FAIL with `TypeError: qemu_command() got an unexpected keyword argument 'smp'`

- [ ] **Step 3: Implement — add the `smp` parameter and use the helper**

In `scripts/app-llama-vk.py`, change the import line (around line 23) to also import `smp_args`:

```python
    machine_and_cpu_args,
    smp_args,
```

Change the signature and command list in `qemu_command` (line 47 and 57-58):

```python
def qemu_command(qemu: str, model: Path, mode: str, timeout: int, port: int, arch: str, smp: int = 1) -> list[str]:
    del model, timeout
    accel = acceleration(arch)
```

```python
    command = [
        qemu, *machine_and_cpu_args(arch, accel), *smp_args(smp), "-m", "3072",
```

In `main()` (after `args = parser.parse_args()`, ~line 112) read the value, and pass it at both `qemu_command(...)` call sites (lines ~120 and ~134):

```python
    parser.add_argument("--smp", type=int, default=int(os.environ.get("VOGUE_SMP", "1")))
```

```python
    base = qemu_command(qemu or qemu_name, args.model, args.mode, args.timeout, port, arch, smp=args.smp)
```

Record it in the `inputs` dict (line ~121) so every JSON artifact states the vCPU count:

```python
    inputs = {"mode": args.mode, "arch": arch, "model": str(args.model), "image": str(image(args.mode, arch)), "smp": args.smp}
```

(`os` is already imported in `app-llama-vk.py`.)

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m pytest scripts/tests/test_scripts.py -v`
Expected: PASS — including the pre-existing `test_vk_modes_select_distinct_images` (which calls `qemu_command` without `smp`, exercising the default).

- [ ] **Step 5: Commit**

```bash
git add scripts/app-llama-vk.py scripts/tests/test_scripts.py
git commit -m "feat(smp): launch VK appliance with --smp/VOGUE_SMP vCPUs"
```

### Task 0.3: Thread `--smp` into the CPU runner (TDD)

**Files:**
- Modify: `scripts/app-llama-cpu.py` (`qemu_command` + `main`)
- Test: `scripts/tests/test_scripts.py` (`CommandTests`)

- [ ] **Step 1: Write the failing test**

Add to `CommandTests`:

```python
    def test_cpu_smp_flag_present_only_when_multivcpu(self) -> None:
        one = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, "x86_64", smp=1)
        four = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, "x86_64", smp=4)
        self.assertNotIn("-smp", one)
        self.assertEqual(four[four.index("-smp") + 1], "4")
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m pytest scripts/tests/test_scripts.py -k cpu_smp -v`
Expected: FAIL with `TypeError: qemu_command() got an unexpected keyword argument 'smp'`

- [ ] **Step 3: Implement — mirror the VK runner**

In `scripts/app-llama-cpu.py`, add `smp_args` to the `common` import block, then:

```python
def qemu_command(qemu: str, model: Path, mode: str, timeout: int, arch: str, smp: int = 1) -> list[str]:
    del model, timeout
    accel = acceleration(arch)
    return [qemu, *machine_and_cpu_args(arch, accel), *smp_args(smp), "-m", "4096", "-nographic", "-no-reboot", "-kernel", str(image(mode, arch))]
```

In `main()` add the argument, pass it to `qemu_command`, and add `"smp": args.smp` to `inputs` (same three edits as Task 0.2):

```python
    parser.add_argument("--smp", type=int, default=int(os.environ.get("VOGUE_SMP", "1")))
```

```python
    base = qemu_command(qemu or qemu_name, args.model, args.mode, args.timeout, arch, smp=args.smp)
```

If `os` is not already imported in `app-llama-cpu.py`, add `import os` at the top with the other stdlib imports.

- [ ] **Step 4: Run the full host-native gate**

Run: `python3 -m pytest scripts/tests/test_scripts.py -v` then `make test-fast`
Expected: all tests PASS; `make test-fast` green (native suite + protocol/ABI checks).

- [ ] **Step 5: Commit**

```bash
git add scripts/app-llama-cpu.py scripts/tests/test_scripts.py
git commit -m "feat(smp): launch CPU appliance with --smp/VOGUE_SMP vCPUs"
```

### Task 0.4: Prove the VK-server guest boots 4 vCPUs (runtime evidence)

The VK-server appliance is already built with `CONFIG_UKPLAT_CPU_MAXCOUNT: '4'`, so no rebuild config change is needed — only launching it with `-smp 4`.

**Files:**
- Create: `docs/results-smp/vk-server-smp4-boot.log` (captured artifact)

- [ ] **Step 1: Build the VK-server image (if not already built)**

Run: `make llama-vk-server` (or the repo's documented build wrapper for `vogue-llama-vk-server`)
Expected: image at `.unikraft/build/vogue-llama-vk-server_qemu-x86_64` exists.

- [ ] **Step 2: Launch with 4 vCPUs and capture the boot log**

Run:
```bash
VOGUE_SMP=4 python3 scripts/app-llama-vk.py --mode server --arch x86_64 \
  --model $(python3 -m scripts.vogue model-path 2>/dev/null || echo rootfs/llama/model.gguf) \
  --timeout 300 2>&1 | tee docs/results-smp/vk-server-smp4-boot.log
```
(Use the same model path the existing `make llama-vk-server-run` target resolves; substitute it if the helper above is not present.)

- [ ] **Step 3: Verify multi-vCPU boot + readiness in the log**

Run:
```bash
grep -iE "lcpu|cpu .*(online|start|bring)|smp" docs/results-smp/vk-server-smp4-boot.log
grep -E "READY .* threads=4" docs/results-smp/vk-server-smp4-boot.log
```
Expected: at least one Unikraft AP/LCPU bring-up line (record the exact string emitted by this build — Unikraft logs secondary-CPU start during `uk_lcpu_mp_init`), **and** the appliance READY line `uk-llama-upstream-vk-server: READY model=/mnt/model/model.gguf threads=4 backend=vulkan ...`.
Cross-check the host view: while the guest runs, `-smp 4` means QEMU created 4 vCPUs (visible via the QEMU monitor `info cpus` if a monitor is attached). If no AP-bring-up line appears in the guest log, that is itself the finding for Phase 3 — capture it; do not fabricate one.

- [ ] **Step 4: Commit the evidence**

```bash
git add docs/results-smp/vk-server-smp4-boot.log
git commit -m "evidence(smp): VK-server boots 4 vCPUs (CONFIG_UKPLAT_CPU_MAXCOUNT=4 + -smp 4)"
```

---

## Phase 1 — CPU appliance: the cleanest multi-vCPU demonstration

The Vulkan path offloads all layers to the GPU (`--n-gpu-layers 99`), so CPU `--threads` has limited effect on its decode. The **CPU appliance** (ggml CPU backend) is where thread parallelism, if the scheduler allows it, shows the largest and clearest signal — so it is the honest first measurement of whether 0.21.0's scheduler lets threads use multiple vCPUs.

### Task 1.1: Build the CPU bench appliance for SMP

**Files:**
- Modify: `kraft/Kraftfile.llama-cpu-bench:15` (and `Kraftfile.llama-cpu`, `Kraftfile.llama-cpu-server`)

- [ ] **Step 1: Add the CPU-count build config**

In `kraft/Kraftfile.llama-cpu-bench`, under the `kconfig:` block, add (mirroring the VK-server Kraftfile comment so the rationale travels with the config):

```yaml
    # P0/SMP (docs/plan-smp-vcpu.md): build the ggml CPU appliance for multiple
    # vCPUs. UKPLAT_CPU_MAXCOUNT>1 auto-selects HAVE_SMP + APIC/ACPI on x86_64.
    # Boot with QEMU -smp N (VOGUE_SMP=N); ggml uses CONFIG_APP_LLAMA_CPU_THREADS.
    CONFIG_UKPLAT_CPU_MAXCOUNT: '4'
```

And bump the thread count it builds with:

```yaml
    CONFIG_APP_LLAMA_CPU_THREADS: '4'
```

- [ ] **Step 2: Rebuild**

Run: `make llama-cpu-bench` (the repo's CPU-bench build wrapper)
Expected: `.unikraft/build/vogue-llama-cpu_qemu-x86_64` rebuilt with APIC/ACPI/SMP pulled in (no Kconfig errors about missing interrupt controller).

- [ ] **Step 3: Commit**

```bash
git add kraft/Kraftfile.llama-cpu-bench
git commit -m "feat(smp): build CPU bench appliance with CONFIG_UKPLAT_CPU_MAXCOUNT=4"
```

### Task 1.2: Capture the single-vCPU CPU baseline (lock the comparison point)

**Files:**
- Create: `docs/results-smp/llama_cpu_smp1.json` (copy of the canonical result, frozen for comparison)

- [ ] **Step 1: Run the CPU bench with 1 vCPU and 1 thread**

Run:
```bash
VOGUE_SMP=1 make llama-cpu-bench-run
cp results/llama/llama_cpu.json docs/results-smp/llama_cpu_smp1.json
```
Expected: `results/llama/llama_cpu.json` → `pass`; record its `pp512` / `tg128` (current branch baseline is `pp512=31.2`, `tg128=10.9` per `plan-optimize.md`, software-Vulkan host; re-measure on the target host).

- [ ] **Step 2: Commit the baseline**

```bash
git add docs/results-smp/llama_cpu_smp1.json
git commit -m "evidence(smp): CPU bench single-vCPU baseline"
```

### Task 1.3: Measure the CPU bench with 4 vCPUs and decide the scheduler question

**Files:**
- Create: `docs/results-smp/llama_cpu_smp4.json`, `docs/results-smp/cpu-bench-smp4-boot.log`

- [ ] **Step 1: Run with 4 vCPUs**

Run:
```bash
VOGUE_SMP=4 make llama-cpu-bench-run 2>&1 | tee docs/results-smp/cpu-bench-smp4-boot.log
cp results/llama/llama_cpu.json docs/results-smp/llama_cpu_smp4.json
```

- [ ] **Step 2: Compare and record the verdict**

Run:
```bash
python3 - <<'PY'
import json
a=json.load(open("docs/results-smp/llama_cpu_smp1.json"))["metrics"]
b=json.load(open("docs/results-smp/llama_cpu_smp4.json"))["metrics"]
for k in ("pp512","tg128"):
    print(k, "smp1=",a.get(k), "smp4=",b.get(k),
          "speedup=", round(b.get(k,0)/a.get(k,1),2) if a.get(k) else "n/a")
PY
```
Expected, **two possible honest outcomes** — record whichever occurs:
- **(A) Speedup on `pp512`** (prompt processing is the most thread-parallel ggml stage): this means the 0.21.0 scheduler *did* place ggml workers on multiple vCPUs for this workload → Phase 3 may be unnecessary; document the win with these two artifacts.
- **(B) No speedup (≈1.0×), or a hang/timeout/slowdown**: confirms the `ukschedcoop` single-LCPU ceiling (`schedcoop.c:274`) — ggml's spin-waiting workers serialize on LCPU0. This is the **decision gate** that justifies Phase 3. Capture the boot log and the flat numbers as the branch-local evidence for the ceiling.

- [ ] **Step 3: Commit the measurement**

```bash
git add docs/results-smp/llama_cpu_smp4.json docs/results-smp/cpu-bench-smp4-boot.log
git commit -m "evidence(smp): CPU bench 4-vCPU measurement vs single-vCPU baseline"
```

---

## Phase 2 — VK server: aggregate-throughput measurement

For the GPU-offloaded server, the SMP hypothesis is *aggregate* throughput: with 4 continuous-batching slots, overlapping per-slot CPU work (sampling, tokenization, HTTP, lwIP) across vCPUs should raise concurrent request throughput even though decode is GPU-bound. This is also scheduler-gated, so it is measured, not assumed.

### Task 2.1: Capture the single-vCPU server baseline

**Files:**
- Create: `docs/results-smp/server_vk_smp1.json`

- [ ] **Step 1: Run the server throughput capture with 1 vCPU**

Run:
```bash
VOGUE_SMP=1 make llama-vk-server-run
cp results/llama/server_vk_throughput.json docs/results-smp/server_vk_smp1.json 2>/dev/null || \
  cp results/llama/llama_server_vk.json docs/results-smp/server_vk_smp1.json
```
Expected: a `pass` artifact. Baseline numbers to beat (from `plan-optimize.md`): `decode_tps_mean=140.6`, `prompt_tps_mean=865.66`, `ttft_s=1.2376`, `requests_per_s=0.412` at `concurrency=1`.

- [ ] **Step 2: Commit**

```bash
git add docs/results-smp/server_vk_smp1.json
git commit -m "evidence(smp): VK-server single-vCPU baseline"
```

### Task 2.2: Measure the server with 4 vCPUs under concurrency

**Files:**
- Create: `docs/results-smp/server_vk_smp4.json`

- [ ] **Step 1: Run with 4 vCPUs**

Run:
```bash
VOGUE_SMP=4 make llama-vk-server-run
cp results/llama/server_vk_throughput.json docs/results-smp/server_vk_smp4.json 2>/dev/null || \
  cp results/llama/llama_server_vk.json docs/results-smp/server_vk_smp4.json
```
Note: the meaningful comparison is **aggregate throughput under `concurrency > 1`**. If the current throughput harness only drives `concurrency=1`, file a follow-up to add a concurrent driver (out of scope here); record the single-stream numbers and label them as such rather than overclaiming.

- [ ] **Step 2: Compare and record**

Run:
```bash
python3 - <<'PY'
import json
a=json.load(open("docs/results-smp/server_vk_smp1.json")).get("metrics",{})
b=json.load(open("docs/results-smp/server_vk_smp4.json")).get("metrics",{})
for k in set(a)|set(b):
    print(k, "smp1=",a.get(k), "smp4=",b.get(k))
PY
```
Expected: record whether `requests_per_s` / `decode_tps_mean` improve. Treat improvement as a win only if it beats the exact baseline above; otherwise mark **scheduler-gated** and feed into the Phase 3 decision.

- [ ] **Step 3: Commit**

```bash
git add docs/results-smp/server_vk_smp4.json
git commit -m "evidence(smp): VK-server 4-vCPU measurement vs single-vCPU baseline"
```

---

## Phase 3 — DECISION GATE: cross-vCPU thread parallelism

**Enter Phase 3 only if** Phase 1.3 / 2.2 showed outcome (B): the secondary vCPUs boot but threads do not parallelize (the `ukschedcoop` single-LCPU ceiling). If outcome (A) held, stop and document the win — Phase 3 is unnecessary.

This phase is a **research track with a spike**, not a fixed implementation, because the cooperative scheduler change is kernel-level and its exact shape depends on the spike result. It is written to the repo's stop condition: no win is claimed without a new branch-local artifact beating its baseline.

### Phase 3.0 — The two source-verified facts that fix the design choice

Before choosing an unlock, pin down *who actually owns the parallelism*. Both facts below were read from the pinned sources (llama.cpp `b9581`, Unikraft `RELEASE-0.21.0`).

**Fact 1 — llama.cpp already implements multi-threading correctly; it is plain POSIX.** The deficiency is not in llama.cpp.

- ggml's CPU backend creates `n_threads - 1` workers with `pthread_create` (`ggml/src/ggml-cpu/ggml-cpu.c:3286`); the main thread is worker 0 (`ggml-cpu.c:3370`).
- Workers synchronise via `ggml_barrier()`, which **spins on an atomic and calls `ggml_thread_cpu_relax()` — a CPU `pause`, not `sched_yield()`** (`ggml-cpu.c:566-599`). Between graphs it is hybrid poll/wait: spin `1024*128*poll` rounds (`ggml_graph_compute_poll_for_work`, `ggml-cpu.c:3125`) before falling back to a condvar (`ggml-cpu.c:3199`).
- **Implication:** the spin barrier is *correct on real SMP* (each thread spins briefly on its own core) but **hangs/serialises if multiple workers share one LCPU** (a spinning thread never yields, starving its siblings on the same LCPU). On Linux the OS spreads the pthreads across cores automatically; the only thing missing under Unikraft is that same OS-level placement.

**Fact 2 — Unikraft 0.21.0 ships no SMP scheduler.** There is nothing to "select".

- The only scheduler-related libs in `lib/` at `RELEASE-0.21.0` (and on `staging`) are `uksched` (the pluggable framework) and `ukschedcoop` (the one implementation, cooperative + single-LCPU). Searches for `ukschedpreem` / `schedpreem` / "preemptive scheduler" return nothing; `lib/uksched/Config.uk` has **no scheduler choice menu**.
- The framework is *minimally* SMP-aware by design: `uk_sched_idle_thread_func_t` takes a `proc_id` (`lib/uksched/include/uk/sched.h:74`), and `struct uk_sched` has a `next` pointer so scheduler instances can be chained (`sched.h:88`) — matching the docs' "each CPU core can run a different scheduler". But `ukschedcoop` does not fill that interface: `schedcoop_idle_thread()` returns `NULL` for `proc_id > 0` (`lib/ukschedcoop/schedcoop.c:274`), and it keeps a single global run queue on LCPU0.
- **Implication:** "Option A" is not *picking* a scheduler — it is *building* one of the three variants below. The SMP bring-up plumbing (`uk_lcpu_start/run/wait/wakeup`, per-CPU vars, IPIs) already exists for that scheduler to stand on.

### Task 3.1 (spike): prove work can run on a secondary LCPU via `uk_lcpu_run`

**Files:**
- Create: `apps/app-smp-smoke/main.c`, `apps/app-smp-smoke/Config.uk`, `apps/app-smp-smoke/Makefile.uk`
- Create: `kraft/Kraftfile.smp-smoke`

- [ ] **Step 1: Write a guest smoke test that dispatches onto a secondary core**

`apps/app-smp-smoke/main.c` (uses the verified 0.21.0 API `uk_lcpu_run` / `uk_lcpu_get_current` / `uk_lcpus` from `lib/uklcpu/exportsyms.uk`):

```c
#include <uk/lcpu.h>
#include <uk/print.h>

static void hello_on_secondary(void *arg)
{
	(void)arg;
	uk_pr_info("smp-smoke: running on LCPU idx=%u\n",
		   uk_lcpu_get_current()->idx);
}

int main(void)
{
	/* Dispatch a function onto every non-boot LCPU and wait for it.
	 * Proves the AP cores execute work even though ukschedcoop will not
	 * schedule pthreads on them. */
	for (unsigned int i = 1; i < CONFIG_UKPLAT_CPU_MAXCOUNT; i++) {
		struct uk_lcpu *lcpu = &uk_lcpus[i];
		uk_lcpu_run(lcpu, hello_on_secondary, NULL, 0);
		uk_lcpu_wait(lcpu, 0);
	}
	uk_pr_info("smp-smoke: DONE all secondaries reported\n");
	return 0;
}
```

(Confirm the exact `uk_lcpu_run` / `uk_lcpu_wait` signatures and the `struct lcpu` field name against `lib/uklcpu/include/uk/lcpu.h` in the fetched `.deps/src/unikraft` tree before building — the header is the authority; adjust the field access if `idx` differs.)

`apps/app-smp-smoke/Config.uk`, `Makefile.uk`, and `kraft/Kraftfile.smp-smoke` follow the smallest existing app (model them on the simplest entry under `apps/`), with `CONFIG_UKPLAT_CPU_MAXCOUNT: '4'`, `CONFIG_LIBUKLCPU: 'y'`, and on x86_64 the auto-selected APIC/ACPI.

- [ ] **Step 2: Build and run with 4 vCPUs**

Run: `make smp-smoke && VOGUE_SMP=4 <run wrapper for smp-smoke> 2>&1 | tee docs/results-smp/smp-smoke-boot.log`
Expected: the log shows `running on LCPU idx=1`, `idx=2`, `idx=3` and `DONE`. This proves the AP cores execute work — isolating the bottleneck to the *scheduler*, not the hardware bring-up.

- [ ] **Step 3: Commit the spike + decide**

```bash
git add apps/app-smp-smoke kraft/Kraftfile.smp-smoke docs/results-smp/smp-smoke-boot.log
git commit -m "spike(smp): prove uk_lcpu_run executes work on secondary vCPUs"
```

### Task 3.2 (decision): choose the unlock

> **DECISION (2026-06-11): A3 selected.** Full implementation plan: **`docs/plan-smp-scheduler.md`** (executed via superpowers:subagent-driven-development). Rationale: ggml opens exactly `n_threads` workers with a non-yielding spin barrier (Fact 1), so pinning one worker per vCPU makes a *cooperative* scheduler sufficient — no preemptive rewrite needed; llama.cpp stays unmodified; HTTP/lwIP threads also benefit. Escalate to A1/A2 only if a workload needs >1 CPU-bound thread per vCPU or fair preemption; B only if the kernel scheduler is off-limits. The table below is retained for that future escalation.

Per Fact 2, **Option A is "build a scheduler", not "select one".** It has three concrete variants (A1–A3); Option B keeps the scheduler and works around it in the app. Decide using the table, the spike result, and the Phase 1.3 magnitude.

| Variant | What you build | Mechanism | llama.cpp change | Benefits | Effort / risk |
|---|---|---|---|---|---|
| **A3** *(recommended start)* | **One `ukschedcoop` instance per LCPU** | At boot, `uk_schedcoop_create()` a scheduler on each online LCPU (chain via `struct uk_sched.next`); add a `thread_add` placement policy that round-robins/**pins** new `uk_thread`s across LCPUs; cross-LCPU wake via `uk_lcpu_wakeup` (`LIBUKLCPU_WAKEUP_IRQ`) | **None** | ggml workers + lwIP + HTTP threads all spread | **Smallest** — reuses existing cooperative scheduler, closest to the framework's intended design |
| **A1** | **Make `ukschedcoop` itself per-LCPU** | In `lib/ukschedcoop/schedcoop.c` remove the `proc_id > 0 → NULL` restriction (`schedcoop.c:274`); give each LCPU its own run queue + idle thread; use the per-CPU current pointer already in `lib/uksched/include/uk/thread.h` | **None** | Same as A3 | Medium — edits the shared scheduler internals |
| **A2** | **A new preemptive / work-stealing `uksched` implementation** | A fresh lib implementing the `uk_sched` vtable with a timer-tick preemption + load balancing across LCPUs | **None** | Same, plus fair time-slicing for any thread count | **Largest** — effectively a new scheduler subsystem |
| **B** | **App-side ggml offload shim** | Keep cooperative scheduler; replace ggml's pthread threadpool with one that dispatches each compute-graph worker via `uk_lcpu_run()` (run-to-completion) onto a dedicated secondary LCPU | ggml threadpool adapter (VOGUE-local) | **ggml compute only** (not HTTP/lwIP) | Small blast radius but fragile; fights ggml's spin-barrier model and tracks ggml upstream |

**Why A3 is the recommended starting point.** ggml opens exactly `n_threads` workers (Fact 1). If A3 **pins one worker per vCPU**, ggml's spin barrier becomes *correct* — each worker owns its LCPU and the brief spin is the intended SMP behaviour, so a *cooperative* scheduler is sufficient and a full preemptive rewrite (A2) is not required. A3 also leaves llama.cpp/ggml completely unmodified, so it survives llama.cpp version bumps, and it benefits the HTTP/lwIP threads too — unlike B. Escalate to A1/A2 only if a workload needs more threads than vCPUs or fair preemption; fall back to B only if touching the kernel scheduler is off-limits.

Once chosen, write the selected variant as its own focused plan using the writing-plans skill, since each is a multi-step subsystem:
- A1/A2/A3 → `docs/plan-smp-scheduler.md`
- B → `docs/plan-smp-ggml-offload.md`

---

## COMPLETION — A3 implemented & verified (2026-06-12)

A3 was implemented (`docs/plan-smp-scheduler.md`) on branch `smp-a3-per-lcpu-scheduler` and **freshly verified** (per superpowers:verification-before-completion — commands run this session, outputs read):

**Verified evidence:**
- **Build:** `make llama-cpu-bench-build` → `exit 0`.
- **Multi-vCPU boot + per-LCPU schedulers online (KVM `-smp 4`):** `docs/results-smp/a3-cpu-smp4-schedulers-online-kvm.log` shows `SMP: scheduler online on LCPU 1`, `LCPU 2`, `LCPU 3`, and `brought up 3 secondary scheduler(s) (4 total LCPUs)`.
- **Correctness:** fresh `VOGUE_SMP=4 … app-llama-cpu.py --mode bench` (real 806 MB model) → `status=pass`, `inputs.smp=4`, `metrics pp512=20.6 tg128=7.9`. The 4-vCPU A3 build boots and runs the llama CPU bench correctly.
- Kernel change committed in `.deps/src/unikraft` (`lib/ukschedcoop/smp.c` bring-up + `ap_entry`, per-instance run-queue spinlock, SMP-safe `bbuddy`, `boot.c` hook, `clone.c` placement hook); VOGUE evidence committed under `docs/results-smp/`.

**What A3 delivers:** the per-LCPU cooperative scheduler the plan's **P0** required — 4 vCPUs each running their own `ukschedcoop`, with multi-vCPU boot proven and correctness preserved. This is past what upstream Unikraft exercises (no in-tree `uk_lcpu_start` caller; x86 SMP is "on-going work").

**Honest performance outcome (per the plan's stop condition — no win claimed without an artifact that beats baseline):** A3 does **NOT yet beat** the single-vCPU baseline. CPU bench: `pp512` smp1≈31.0 → smp4≈20.6 (**0.66–0.69×**), `tg128` ≈10.7 → ≈7.9 (**0.73×**). Root cause (instrumented: `schedcoop_thread_add` saw **0 placement calls** during the bench): the ggml worker threads are created by the fetched `libpthread_embedded` package via a path that **bypasses** `clone.c`/`uk_sched_thread_add`/`schedcoop_thread_add`, so they are **not distributed** to the AP schedulers — all workers stay on the BSP (4 threads on 1 vCPU) while the 3 APs idle, and the extra SMP machinery adds overhead. Details + remaining steps in `docs/notes-smp-bringup-status.md`.

**Remaining for an actual throughput win (tracked, not yet done):** route thread placement onto the embedded-pthread creation path (or apply CPU affinity there) so the N ggml workers land one-per-vCPU; keep them co-scheduled (polling threadpool already added to `apps/app-llama-cpu/bench.cpp`); address cooperative cross-LCPU wakeup overhead. Also: export the kernel change as `patches/unikraft/0002-*.patch` + register in `config/deps.json` (plan-smp-scheduler.md Task 6), and re-enable VK-server path (Phase 2). A correctness lesson banked: **test SMP under KVM, not TCG** (the `unordered_set` crash seen during bring-up was a TCG emulation artifact).

---

## Verification gates

- `make test-fast` — host-native suite + protocol/ABI checks. **Must stay green after every Phase 0 task** (the `smp_args` + runner changes are covered by `scripts/tests/test_scripts.py`).
- Per-phase runtime artifacts under `docs/results-smp/` back every measurement claim. Each "after" JSON must cite the exact "before" JSON it beats.
- `make verify` — broad release gate (Venus/Vulkan/llama runtime captures) before declaring the SMP work done.

## Success criteria — final status (2026-06-12)

1. **Phase 0 — ✅ MET.** `smp_args()` + `--smp`/`VOGUE_SMP` wired into `common.py` + both runners (host-native tests pass); a guest built with `CONFIG_UKPLAT_CPU_MAXCOUNT=4` boots 4 vCPUs under `VOGUE_SMP=4` (evidence: `docs/results-smp/a3-cpu-smp4-schedulers-online-kvm.log`).
2. **Phase 1 — ✅ MET (measurement done, scheduler ceiling confirmed).** CPU bench before/after captured (`docs/results-smp/a3_*.json`); the single-LCPU ceiling was confirmed and drove the A3 decision. **Phase 2 (VK server) — ⏸ not done** (effort focused on the CPU path). A throughput *win* is **not** claimed — smp4 does not beat smp1 (see COMPLETION section).
3. **Phase 3 — ✅ A3 BUILT & VERIFIED.** Per-LCPU cooperative scheduler implemented (`docs/plan-smp-scheduler.md`), 4 schedulers online under KVM, bench passes (correctness). The throughput optimization (distributing ggml threads onto the AP schedulers) remains open — see COMPLETION section.

**Overall:** the plan's SMP objective (multi-vCPU boot + per-LCPU scheduling, A3) is implemented and verified; the *performance* win is documented as not-yet-achieved with a root cause and remaining steps, per the plan's evidence-first stop condition.

## Risks & honest caveats

- **Scheduler ceiling is the headline risk.** `ukschedcoop` is single-LCPU at 0.21.0 (`schedcoop.c:274`); the most likely Phase 1 outcome is *no* CPU-thread speedup until Phase 3. The plan is structured so Phase 0 still ships real value (correct multi-vCPU boot + harness) regardless.
- **ggml spin-wait under cooperative scheduling can hang.** ggml CPU workers busy-wait on barriers with a CPU `pause`, never `sched_yield` (`ggml-cpu.c:566-599`); on a single-LCPU cooperative scheduler, multiple non-yielding workers starve each other. Watch for timeouts in Task 1.3 — a hang is itself evidence for the Phase 3 decision. The same property is *why* Phase 3 variant A3 (one worker pinned per vCPU) makes a cooperative scheduler sufficient.
- **No drop-in SMP scheduler exists.** Unikraft 0.21.0 ships only `uksched` + `ukschedcoop` (no preemptive/SMP scheduler, no choice menu). Do not plan around "selecting" an SMP scheduler — Phase 3 Option A *builds* one (A1/A2/A3).
- **GPU-bound VK decode limits SMP upside.** With `-ngl 99`, the VK server's decode is on the host GPU; SMP only helps the CPU-side slot/HTTP overlap. Don't overclaim a decode-rate win from vCPUs on the VK path.
- **API drift.** The lcpu API is `uk_lcpu_*` at 0.21.0 (renamed from `ukplat_lcpu_*`). The Phase 3 spike must validate exact signatures/struct fields against `.deps/src/unikraft/lib/uklcpu/include/uk/lcpu.h`.
- **Baseline integrity.** `smp_args(1)` returns `[]` so existing single-vCPU baselines and their commands are unchanged unless SMP is explicitly requested — do not "upgrade" historical baselines in place.

## References

### Official Unikraft documentation & blog

- Unikraft v0.21.0 release notes (PAL rework; per-CPU variables library introduced) — https://unikraft.org/blog/2026-04-20-unikraft-releases-v0.21.0
- "Adding SMP support" / synchronization blog (RCU/lock-free work; scheduler-on-each-CPU intent) — https://unikraft.org/blog/2022-07-19-unikraft-synchronization
- Architecture internals (pluggable schedulers, "each CPU core can run a different scheduler") — https://unikraft.org/docs/internals/architecture
- Performance concepts (allocator/threading notes) — https://unikraft.org/docs/concepts/performance
- mimalloc / multithreading benchmarking write-up — https://unikraft.org/blog/2024-08-22-unikraft-gsoc-benchmarking-mimalloc
- Unikraft Cloud roadmap: multi-core instances (upstream SMP still maturing) — https://roadmap.unikraft.com/p/multi-core-instances

### Upstream SMP pull requests (design lineage)

- arch-independent SMP API (`uk_lcpu_*`, fixed `lcpu` array, BSP allocates LCPUs) — https://github.com/unikraft/unikraft/pull/469
- x86_64 SMP bring-up — https://github.com/unikraft/unikraft/pull/244
- ARM64 SMP bring-up — https://github.com/unikraft/unikraft/pull/373

### Pinned Unikraft source — `RELEASE-0.21.0` (verified, the repo's source of truth)

- `plat/Config.uk` — `CONFIG_UKPLAT_CPU_MAXCOUNT` (range 1–256, default 1); `HAVE_SMP` (`default y if UKPLAT_CPU_MAXCOUNT > 1`, selects `LIBUKINTCTLR_APIC` + `LIBUKACPI` on x86_64); `HAVE_CPU_MULTI_PHASE_STARTUP`
- `lib/uklcpu/Config.uk` — `LIBUKLCPU` (selects `LIBUKPAL` + `LIBUKPCPUVAR`); `LIBUKLCPU_RUN_IRQ` (x86=13), `LIBUKLCPU_WAKEUP_IRQ` (x86=14) under `HAVE_SMP`
- `lib/uklcpu/exportsyms.uk` — public API `uk_lcpu_mp_init`, `uk_lcpu_start`, `uk_lcpu_run`, `uk_lcpu_wait`, `uk_lcpu_wakeup`, `uk_lcpu_get_current`, `uk_lcpus`
- `lib/uksched/Config.uk` — `LIBUKSCHED` framework (selects `LIBUKLCPU` + `LIBUKPCPUVAR`); **no scheduler choice menu**
- `lib/uksched/include/uk/sched.h:74,88` — `uk_sched_idle_thread_func_t(..., unsigned int proc_id)`; `struct uk_sched { ... struct uk_sched *next; }` (chainable, SMP-aware API)
- `lib/uksched/include/uk/thread.h` — `extern __uk_pcpuvar struct uk_thread *__uk_sched_thread_current;` (per-CPU current-thread pointer)
- `lib/ukschedcoop/schedcoop.c:274` — `/* NOTE: We only support one processing LCPU (for now) */`, `if (proc_id > 0) return NULL;` (single-LCPU limitation)

### Pinned llama.cpp source — tag `b9581` (ggml threading model)

- `ggml/src/ggml-cpu/ggml-cpu.c:3286` — `ggml_thread_create` (= `pthread_create`) of `n_threads - 1` secondary workers
- `ggml/src/ggml-cpu/ggml-cpu.c:566-599` — `ggml_barrier()` spins on atomics via `ggml_thread_cpu_relax()` (CPU `pause`, not `sched_yield`)
- `ggml/src/ggml-cpu/ggml-cpu.c:3125,3199` — hybrid poll/wait (`ggml_graph_compute_poll_for_work`; condvar fallback)
- llama.cpp server thread/option reference — `tools/server/README.md` (`--threads`, `--threads-batch`, `--threads-http`, `--parallel`)

### VOGUE branch integration points (`dev-jerry`)

- `config/deps.json` — pins `unikraft` at tag `RELEASE-0.21.0`, `llama.cpp` at tag `b9581`
- `kraft/Kraftfile.llama-vk-server:45-51` — already sets `CONFIG_UKPLAT_CPU_MAXCOUNT: '4'`, `CONFIG_APP_LLAMA_VK_THREADS: '4'`, `--parallel 4`
- `scripts/common.py:68-74` — `machine_and_cpu_args()` (where `smp_args()` is added)
- `scripts/app-llama-vk.py:47-58` / `scripts/app-llama-cpu.py` — `qemu_command()` (where `-smp` is wired in)
- `apps/app-llama-vk/llama-vk-server-entry.cpp:58-97` — READY line + `--threads` handoff
- `docs/plan-optimize.md` — prior plan flagging P0 (guest SMP) as the top remaining ROI

## Self-review (performed against this plan)

- **Coverage:** research goal → Background + Phase 3.0 with verified citations; "add vCPU support to run llama.cpp" → Phase 0 harness + Phase 1 (CPU) + Phase 2 (VK server); "optimize performance" → measurement tasks with explicit win gates + Phase 3 unlock (A1/A2/A3/B decision table). ✓
- **Placeholders:** every code/command step contains real code and real commands; the only deliberately deferred items are the Phase 3 variant choice (gated on spike + Phase 1.3 evidence) and the exact AP-bring-up log string (to be read from the first capture, not invented) — both justified by the evidence-first stop condition. ✓
- **Type/name consistency:** helper is `smp_args(count)` everywhere; `qemu_command(..., smp=...)` keyword is consistent across VK and CPU runners and their tests; config symbol is `CONFIG_UKPLAT_CPU_MAXCOUNT` (verified at the tag) everywhere; lcpu API names match `lib/uklcpu/exportsyms.uk`; ggml symbol/line citations match tag `b9581`. ✓
