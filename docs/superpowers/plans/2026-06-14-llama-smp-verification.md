# Llama SMP Verification Implementation Plan

> **FOLDED INTO (2026-06-14):** `docs/superpowers/plans/2026-06-14-multi-vcpu-speedup.md`,
> which adds the runtime worker-count fix that this verification-only plan
> assumed but which was missing (the cause of the `blocked:no-pass-marker` hang).

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Verify that `llama-server` runs across multiple vCPUs and that both `llama-bench` and `llama-server` show higher throughput on `SMP=4` than on `SMP=1`.

**Architecture:** Reuse the existing Unikraft worker-entry placement path and treat this turn as a verification-first exercise. Build a clean measurement path for `bench` and `server`, use `SMP=1` as the baseline, use `SMP=4` as the comparison point, and only patch runners or result parsing when current evidence is insufficient to prove placement or throughput.

**Tech Stack:** Unikraft `RELEASE-0.21.0`, llama.cpp CPU appliance, QEMU/KVM `x86_64`, Python `unittest`, local JSON result artifacts, curl-based server probing.

---

## File Map

- Modify: `scripts/app-llama-cpu.py`
  - Add or refine `server` runtime parsing so one request can yield throughput metrics and placement evidence.
- Modify: `scripts/tests/test_scripts.py`
  - Lock in any new parser behavior for `server` throughput / placement output.
- Modify: `docs/superpowers/plans/2026-06-12-pthread-affinity-smp.md`
  - Record verified `SMP=1` vs `SMP=4` evidence and any gaps that remain.
- Create or update: `results/llama/llama_cpu.json`
  - Bench `SMP=1` and `SMP=4` evidence.
- Create or update: `results/llama/llama_server_cpu.json`
  - Server `SMP=1` and `SMP=4` evidence.
- Create or update: `docs/results-smp/llama-server-*.log`
  - Optional raw server traces if JSON alone is not enough to prove placement.

### Task 1: Re-establish the Bench Baseline and SMP Comparison

**Files:**
- Modify: `results/llama/llama_cpu.json`
- Modify: `docs/superpowers/plans/2026-06-12-pthread-affinity-smp.md`

- [ ] **Step 1: Run the `SMP=1` bench baseline**

Run:

```bash
python3 scripts/app-llama-cpu.py --mode bench --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 1
```

Expected:

- command exits 0;
- `results/llama/llama_cpu.json` is refreshed;
- result contains `pp512` and `tg128`;
- status is `pass`.

- [ ] **Step 2: Save the `SMP=1` bench metrics separately**

Run:

```bash
cp results/llama/llama_cpu.json results/llama/llama_cpu_smp1.json
```

Expected:

- `results/llama/llama_cpu_smp1.json` exists and preserves the baseline metrics.

- [ ] **Step 3: Run the `SMP=4` bench comparison**

Run:

```bash
python3 scripts/app-llama-cpu.py --mode bench --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 4
```

Expected:

- command exits 0;
- `results/llama/llama_cpu.json` is refreshed;
- status is `pass`;
- result contains `pp512`, `tg128`, and `kernel_placements`.

- [ ] **Step 4: Save the `SMP=4` bench metrics separately**

Run:

```bash
cp results/llama/llama_cpu.json results/llama/llama_cpu_smp4.json
```

Expected:

- `results/llama/llama_cpu_smp4.json` exists and preserves the comparison metrics.

- [ ] **Step 5: Compare `bench` throughput**

Run:

```bash
python3 - <<'PY'
import json
from pathlib import Path
s1 = json.loads(Path("results/llama/llama_cpu_smp1.json").read_text())
s4 = json.loads(Path("results/llama/llama_cpu_smp4.json").read_text())
print("smp1", s1["metrics"]["pp512"], s1["metrics"]["tg128"])
print("smp4", s4["metrics"]["pp512"], s4["metrics"]["tg128"])
PY
```

Expected:

- printed metrics show whether `SMP=4` beats `SMP=1`;
- if not, record that fact directly rather than smoothing it over.

### Task 2: Get a Runnable `llama-server` x86_64 SMP Image

**Files:**
- Modify: `kraft/Kraftfile.llama-cpu-server`
- Modify: `results/llama/llama_server_cpu.json`

- [ ] **Step 1: Build the `SMP=1` server image**

Run:

```bash
make llama-cpu-server-build
```

Expected:

- build exits 0;
- `.unikraft/build/vogue-llama-cpu-server_qemu-x86_64` exists.

- [ ] **Step 2: Run the `SMP=1` server baseline**

Run:

```bash
python3 scripts/app-llama-cpu.py --mode server --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 1
```

Expected:

- result status is `pass`;
- result contains a `READY` line;
- baseline JSON is refreshed.

- [ ] **Step 3: Save the `SMP=1` server baseline separately**

Run:

```bash
cp results/llama/llama_server_cpu.json results/llama/llama_server_cpu_smp1.json
```

Expected:

- `results/llama/llama_server_cpu_smp1.json` exists.

### Task 3: Add Minimal Server Throughput and Placement Measurement

**Files:**
- Modify: `scripts/app-llama-cpu.py`
- Modify: `scripts/tests/test_scripts.py`
- Modify: `results/llama/llama_server_cpu.json`

- [ ] **Step 1: Write the failing parser test for server throughput output**

Add a focused test in `scripts/tests/test_scripts.py` that feeds a synthetic server log plus a synthetic HTTP measurement result into the runner and asserts that:

- `metrics["ready"]` is preserved;
- `metrics["server_toks_per_s"]` is recorded;
- `metrics["kernel_placements"]` is recorded when dequeue lines exist.

- [ ] **Step 2: Run the focused test to verify failure**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts -v
```

Expected:

- the new server-throughput test fails before parser changes.

- [ ] **Step 3: Implement the minimal `server` measurement path**

Update `scripts/app-llama-cpu.py` so that, for `--mode server`, it can:

- start the server guest;
- wait for the `READY` line;
- issue one local HTTP request;
- measure request wall-clock time;
- compute a response token/s metric from returned generated text length or returned usage fields if present;
- preserve guest log parsing for `kernel_placements`.

- [ ] **Step 4: Run the focused test to verify pass**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts -v
```

Expected:

- the new parser test passes;
- no existing runner tests regress.

### Task 4: Measure `llama-server` on `SMP=1` and `SMP=4`

**Files:**
- Modify: `results/llama/llama_server_cpu.json`
- Create or modify: `docs/results-smp/llama-server-smp1.log`
- Create or modify: `docs/results-smp/llama-server-smp4.log`
- Modify: `docs/superpowers/plans/2026-06-12-pthread-affinity-smp.md`

- [ ] **Step 1: Run the `SMP=1` server throughput measurement**

Run:

```bash
python3 scripts/app-llama-cpu.py --mode server --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 1
cp results/llama/llama_server_cpu.json results/llama/llama_server_cpu_smp1.json
```

Expected:

- status is `pass`;
- JSON contains `ready`;
- JSON contains `server_toks_per_s`.

- [ ] **Step 2: Run the `SMP=4` server throughput and placement measurement**

Run:

```bash
python3 scripts/app-llama-cpu.py --mode server --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 4
cp results/llama/llama_server_cpu.json results/llama/llama_server_cpu_smp4.json
```

Expected:

- status is `pass`;
- JSON contains `ready`;
- JSON contains `server_toks_per_s`;
- JSON contains guest placement evidence showing workers on multiple LCPUs.

- [ ] **Step 3: Compare `server` throughput**

Run:

```bash
python3 - <<'PY'
import json
from pathlib import Path
s1 = json.loads(Path("results/llama/llama_server_cpu_smp1.json").read_text())
s4 = json.loads(Path("results/llama/llama_server_cpu_smp4.json").read_text())
print("smp1", s1["metrics"].get("server_toks_per_s"))
print("smp4", s4["metrics"].get("server_toks_per_s"))
print("smp4 placements", s4["metrics"].get("kernel_placements"))
PY
```

Expected:

- output shows whether `SMP=4` is faster than `SMP=1`;
- output shows whether `SMP=4` placement is actually distributed.

### Task 5: Update the Main SMP Plan with Verified Outcomes

**Files:**
- Modify: `docs/superpowers/plans/2026-06-12-pthread-affinity-smp.md`

- [ ] **Step 1: Append the bench `SMP=1` vs `SMP=4` comparison**

Record:

- baseline metrics;
- comparison metrics;
- whether throughput increased.

- [ ] **Step 2: Append the server `SMP=1` vs `SMP=4` comparison**

Record:

- server `READY` evidence;
- `server_toks_per_s` baseline and comparison;
- `SMP=4` placement evidence across multiple vCPUs.

- [ ] **Step 3: State the outcome without overstating it**

The final plan update must separate these cases clearly:

- `bench` placement verified / not verified;
- `bench` throughput improved / did not improve;
- `server` placement verified / not verified;
- `server` throughput improved / did not improve.

- [ ] **Step 4: Run the final verification commands**

Run:

```bash
python3 -m unittest scripts.tests.test_scripts -v
python3 scripts/app-llama-cpu.py --mode bench --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 1
python3 scripts/app-llama-cpu.py --mode bench --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 4
python3 scripts/app-llama-cpu.py --mode server --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 1
python3 scripts/app-llama-cpu.py --mode server --arch x86_64 --model models/tiny-random-llama-GGUF/tiny-random-llama-Q2_K.gguf --timeout 180 --smp 4
```

Expected:

- all commands complete;
- the result artifacts are fresh;
- the outcome is proven by current evidence rather than memory.
