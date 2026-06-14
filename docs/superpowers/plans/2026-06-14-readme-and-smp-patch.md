# README Update + SMP Unikraft Patch Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Export the 13 VOGUE SMP commits on top of `RELEASE-0.21.0` as a new tracked patch under `patches/unikraft/`, update `config/deps.json` to register it, update `README.md` to reflect current SMP status, then commit all pending changes in logical groups.

**Architecture:** `.deps/src/unikraft` currently has 13 commits above `RELEASE-0.21.0`. Three of those are already tracked as patches (`0001-pal-ectx`, `0002-ukpod`, `0001-virtio-pci`). The remaining SMP-specific files (`lib/uksched`, `lib/ukschedcoop`, `lib/ukboot`, `lib/ukallocbbuddy`, `lib/posix-process`) are untracked changes. We export a single squashed SMP patch via `git diff`, register it in `deps.json`, then update README and commit.

**Tech Stack:** git, Python (`scripts/deps.py` applies patches), JSON (`config/deps.json`), Markdown.

---

## File Map

- Create: `patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch`
- Modify: `config/deps.json` — add the new patch to the `patches` array for `unikraft`
- Modify: `README.md` — update status table, add SMP appliances/patches section

---

## Task 1: Generate the SMP patch from `.deps/src/unikraft`

The 13 commits above `RELEASE-0.21.0` contain the existing 3 patches + SMP work.
Generate a squashed diff of only the SMP-new files, excluding files already covered by existing patches.

**Files:**
- Create: `patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch`

- [ ] **Step 1: Generate the squashed diff**

```bash
cd /mydata/JerryT/vm-final-project/virtio-gpu/.deps/src/unikraft && \
git diff RELEASE-0.21.0..HEAD -- \
  lib/ukallocbbuddy/bbuddy.c \
  lib/ukboot/boot.c \
  lib/posix-process/clone.c \
  lib/posix-process/exportsyms.uk \
  "lib/posix-process/include/uk/process.h" \
  lib/posix-process/process.c \
  lib/uksched/exportsyms.uk \
  "lib/uksched/include/uk/sched.h" \
  "lib/uksched/include/uk/sched_impl.h" \
  "lib/uksched/include/uk/thread.h" \
  lib/uksched/sched.c \
  lib/uksched/thread.c \
  lib/ukschedcoop/ \
  > /mydata/JerryT/vm-final-project/virtio-gpu/patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch
```

- [ ] **Step 2: Verify the patch is non-empty and sane**

```bash
wc -l /mydata/JerryT/vm-final-project/virtio-gpu/patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch
head -30 /mydata/JerryT/vm-final-project/virtio-gpu/patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch
```

Expected: several hundred lines, starts with `diff --git a/lib/...`

- [ ] **Step 3: Dry-run apply against a fresh RELEASE-0.21.0 to confirm patch applies**

This validates that when `make deps` re-clones and applies patches in order, the new patch works.
**Only run if you have a second unikraft checkout available; otherwise skip** — the diff was computed against the same tree the existing patches were applied to, so it is guaranteed to apply cleanly.

---

## Task 2: Register the new patch in `config/deps.json`

**Files:**
- Modify: `config/deps.json`

- [ ] **Step 1: Add the patch entry**

Open `config/deps.json`. In the `git_sources.unikraft.patches` array, append the new entry after `0001-virtio-pci-modern-device-support.patch`:

```json
"patches": [
  "patches/unikraft/0001-pal-ectx-extern-c-linkage.patch",
  "patches/unikraft/0002-ukpod-anon-include-assert.patch",
  "patches/unikraft/0001-virtio-pci-modern-device-support.patch",
  "patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch"
]
```

Also update the `description` field for `unikraft` to mention the SMP patch:

```json
"description": "Unikraft core. Kraftfiles reference this via the @@UNIKRAFT_LOCAL@@ placeholder resolved by mk/llama.mk. Patches in patches/unikraft/ are re-applied after every checkout/refresh (see scripts/deps.py). Patch 0003 adds the per-LCPU cooperative scheduler, per-thread CPU affinity state, and sched_{get,set}affinity syscalls needed for multi-vCPU ggml worker placement."
```

- [ ] **Step 2: Validate JSON**

```bash
python3 -c "import json; json.load(open('config/deps.json')); print('ok')"
```

Expected: `ok`

- [ ] **Step 3: Commit patch + deps.json**

```bash
cd /mydata/JerryT/vm-final-project/virtio-gpu
git add patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch config/deps.json
git commit -m "feat(smp): export per-LCPU coop scheduler + affinity as tracked unikraft patch

Squashes 13 SMP commits above RELEASE-0.21.0 (ukschedcoop SMP registry,
per-thread affinity state, sched_setaffinity/getcpu syscalls, thread
migration) into patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch.
Registers it in config/deps.json so make deps re-applies it on fresh checkout.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 3: Update README.md

Current README has a status table with 4 llama appliances. It does not mention:
- SMP / multi-vCPU worker placement (now verified pass for bench + server at smp1)
- `pthread-affinity` probe appliance (new in this branch)
- The SMP unikraft patch (§10 Testing table and §7 notes refer to existing patches only)

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update the "Status at a glance" table**

The current table has 4 rows. Add SMP verification evidence. Replace the existing table with:

```markdown
| Appliance | Command | Result |
|-----------|---------|--------|
| CPU bench | `make llama-cpu-bench-run` | `pass` — pp512 31.2 / tg128 10.9 tok/s |
| Vulkan bench | `make llama-vk-bench-run` | `pass` — pp512 4582.6 / tg128 353.8 tok/s |
| CPU server | `make llama-cpu-server-run` | `pass` — `/health` 200, `/completion` 200 |
| Vulkan server | `make llama-vk-server-run` | `pass` — `/health` 200, `/completion` 200 |
| CPU bench (SMP) | `make llama-cpu-bench-run VOGUE_SMP=N` | `pass` — pp512 3699.3 / tg128 192.3 tok/s (4 vCPU, tiny model) |
| CPU server (SMP) | `make llama-cpu-server-run VOGUE_SMP=N` | `pass` — server_toks_per_s 54.34 (smp=1, tiny model) |
| pthread-affinity probe | `make pthread-affinity-run` | `pass` (SMP placement verification, 4 vCPUs) |
```

- [ ] **Step 2: Add SMP section to "Project structure" appliances table**

In the `apps/` table, add the new app after `llama-common/`:

```markdown
| `app-pthread-affinity` | `Kraftfile.pthread-affinity` | per-LCPU affinity probe — verifies one thread per vCPU placement under SMP |
```

- [ ] **Step 3: Add SMP subsection to §7 (Host setup)**

After the existing "Guest-side fixes already in-tree" list, add:

```markdown
### SMP: per-LCPU cooperative scheduler (branch `smp-a3-per-lcpu-scheduler`)

The CPU bench and server support `-smp N` multi-vCPU operation via a set of
VOGUE patches to Unikraft (`patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch`):

- **Per-LCPU `ukschedcoop` instances** — one cooperative scheduler per online vCPU,
  instantiated in `lib/ukschedcoop/smp.c`. Each LCPU bootstraps its own run queue
  and idle thread.
- **Per-thread CPU affinity state** in `uk_thread` + `uk_schedcoop_smp_place_current`
  for explicit worker migration before compute.
- **`sched_setaffinity` / `sched_getcpu` syscalls** wired to the Unikraft affinity
  path, surfaced to llama.cpp's ggml CPU backend via the Unikraft affinity conditional
  in `ggml-cpu.c`.
- **Runtime vCPU count** (`uk_schedcoop_smp_online_count`) replaces the build-time
  `@@VOGUE_SMP@@` macro, preventing oversubscription deadlock on the cooperative
  scheduler (polling workers > online vCPUs would spin-lock the run queue).

Set `VOGUE_SMP=N` on the make command line to build for N vCPUs and launch with `-smp N`:

```sh
make llama-cpu-bench-run VOGUE_SMP=4 ARCH=x86_64 MODEL=models/model.gguf
```
```

- [ ] **Step 4: Update §10 Testing table to add pthread-affinity probe**

Append to the test table:

```markdown
| `pthread_affinity` | SMP per-vCPU worker placement (4 vCPUs) | `make pthread-affinity-run` |
```

- [ ] **Step 5: Commit README**

```bash
cd /mydata/JerryT/vm-final-project/virtio-gpu
git add README.md
git commit -m "docs: update README with SMP multi-vCPU status and patch overview

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 4: Commit remaining modified files in logical groups

Many tracked files are modified. Commit them in four logical groups.

**Files to commit:**
- Group A (build system): `Makefile`, `mk/llama.mk`, `kraft/Kraftfile.llama-cpu`, `kraft/Kraftfile.llama-cpu-bench`, `kraft/Kraftfile.llama-cpu-server`, `kraft/Kraftfile.pthread-affinity`
- Group B (app code): `apps/app-llama-cpu/llama-server-entry.cpp`, `apps/app-pthread-affinity/`
- Group C (scripts + tests): `scripts/app-llama-cpu.py`, `scripts/smp_topology.py`, `scripts/app-pthread-affinity.py`, `scripts/tests/test_scripts.py`
- Group D (results + docs): `results/llama/llama_server_cpu.json`, `results/llama/llama_cpu_smp1.json`, `results/llama/llama_cpu_smp4.json`, `results/llama/llama_server_cpu_smp1.json`, `results/smp/`, `docs/notes-smp-source.md`, `docs/plan-smp-scheduler.md`, `docs/plan-smp-vcpu.md`, `docs/superpowers/plans/2026-06-12-pthread-affinity-smp.md`, `docs/superpowers/plans/2026-06-14-llama-smp-verification.md`, `docs/superpowers/plans/2026-06-14-multi-vcpu-speedup.md`

- [ ] **Step 1: Verify what's outstanding**

```bash
git status --short
```

Confirm no surprises (secret files, large binaries). Note which files are `M` (modified tracked) vs `??` (untracked).

- [ ] **Step 2: Commit Group A — build system**

```bash
git add Makefile mk/llama.mk kraft/Kraftfile.llama-cpu kraft/Kraftfile.llama-cpu-bench kraft/Kraftfile.llama-cpu-server kraft/Kraftfile.pthread-affinity
git commit -m "build: add pthread-affinity target and VOGUE_SMP variable support

mk/llama.mk: sed replaces @@VOGUE_SMP@@ at build time; clears stale
kconfig before cpu bench/server builds.
Makefile: exposes pthread-affinity-{build,run} targets.
Kraftfiles: reference @@VOGUE_SMP@@ for SMP worker count.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

- [ ] **Step 3: Commit Group B — app code**

```bash
git add apps/app-llama-cpu/llama-server-entry.cpp apps/app-pthread-affinity/
git commit -m "feat(app): pthread-affinity probe app + server runtime vCPU placement

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

- [ ] **Step 4: Commit Group C — scripts + tests**

```bash
git add scripts/app-llama-cpu.py scripts/smp_topology.py scripts/app-pthread-affinity.py scripts/tests/test_scripts.py
git commit -m "feat(scripts): SMP runner, topology helper, pthread-affinity runner, tests

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

- [ ] **Step 5: Commit Group D — results + docs**

```bash
git add results/ docs/notes-smp-source.md docs/plan-smp-scheduler.md docs/plan-smp-vcpu.md docs/superpowers/plans/
git commit -m "evidence(smp): bench/server smp1/smp4 results + updated plans

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

- [ ] **Step 6: Verify clean state**

```bash
git status
git log --oneline -8
```

Expected: working tree clean, 5+ new commits visible.

---

## Self-Review

- **Spec coverage:** patch export ✓, deps.json ✓, README ✓, commit grouping ✓
- **Placeholder scan:** no TBDs
- **Type consistency:** no cross-task type references
- **Risk:** The patch generation in Task 1 excludes the 3 already-tracked files. If `scripts/deps.py` applies patches in order on a fresh RELEASE-0.21.0 checkout, patch 0003 must apply cleanly after the first 3 patches. Since patches 0001/0002 touch different files (pal/ectx.h, ukpod/anon.c, drivers/virtio/pci/virtio_pci.c) and 0003 touches lib/uksched*, lib/ukschedcoop*, lib/ukboot*, lib/posix-process*, there is no overlap — the order does not matter for correctness.
