# Patch Split + Docs Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single monolithic `0003` Unikraft patch with three logically-split patches, update README.md and docs to reflect the completed SMP work (3.83x pp512 / 2.10x server), and commit everything with clean messages.

**Architecture:** The Unikraft `.deps/src/unikraft` tree has 13 commits on top of the base tag `7351f8b` (RELEASE-0.21.0). We collapse them into three clean, independently-applicable patches by logical layer: (1) AP bring-up + allocator safety, (2) per-LCPU cooperative scheduler, (3) per-thread affinity + migration fixes. `config/deps.json` lists the patches; we update it to list the three new ones. Docs get updated in a separate VOGUE commit.

**Tech Stack:** `git format-patch` / `git apply`, `config/deps.json`, `scripts/deps.py`, `README.md`, `docs/notes-smp-bringup-status.md`.

---

## Background: what is in the Unikraft tree

All commits from `7351f8b..HEAD` in `.deps/src/unikraft`:

```
e4806ef  smp: per-LCPU cooperative scheduler (A3)           ← initial A3 scaffold
c7ba47b  smp(WIP): low .bss AP bootstrap stack + CR3 adopt
af34014  smp(WIP): SMP-safe bbuddy allocator (spinlock)
afcdb9e  smp(WIP diag): uk_lcpu_init in ap_entry
38d0026  smp(A3): WORKING under KVM - 4 per-LCPU schedulers
d91af20  smp(A3): round-robin thread placement
2a1b211  smp(A3): cleanup debug traces
8fe9a8c  feat(uksched): add per-thread affinity state
0580ce9  fix(uksched): separate bootstrap and ready affinity
1d018ea  fix(uksched): preserve AP bootstrap scheduler affinity
19035b1  feat(ukschedcoop): expose ready per-LCPU scheduler registry
8b018ad  feat(ukschedcoop): migrate current thread across LCPU schedulers
5733acb  feat(uksched): implement per-thread CPU affinity syscalls
```

The existing `patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch` was generated from an earlier snapshot and does NOT include the Task 4 migration fixes (`schedcoop_thread_migrate_execenv` GS_BASE + IPI on futex wakeup). We regenerate from the live tree.

## Logical patch split

| New patch file | Covers | Key files |
|---|---|---|
| `0003-smp-ap-bringup-allocator.patch` | AP bootstrap stack, CR3 adoption, EFER.NXE, `uk_lcpu_init` in AP entry, SMP-safe bbuddy spinlock | `plat/kvm/x86/lcpu_start.S`-area, `lib/ukallocbbuddy/bbuddy.c`, `lib/ukboot/boot.c` |
| `0004-smp-per-lcpu-coop-scheduler.patch` | One `ukschedcoop` instance per online vCPU, per-CPU scheduler registry, round-robin placement in `uk_clone`, `PLACEMENT_TRACE` | `lib/ukschedcoop/smp.c`, `lib/ukschedcoop/schedcoop.c`, `lib/posix-process/clone.c`, `lib/ukschedcoop/Config.uk` |
| `0005-smp-per-thread-affinity-migration.patch` | Per-thread affinity state in `uk_thread`, `uk_schedcoop_smp_place_current`, GS_BASE patch in `migrate_current_execenv`, IPI on cross-CPU futex wakeup, `sched_setaffinity`/`sched_getaffinity` syscalls | `lib/uksched/thread.h`, `lib/uksched/sched.c`, `lib/ukschedcoop/smp.c` (migration), `lib/ukschedcoop/schedcoop.c` (wakeup IPI) |

## File map

- Regenerate: `patches/unikraft/0003-smp-ap-bringup-allocator.patch` (replaces old 0003)
- Create: `patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch`
- Create: `patches/unikraft/0005-smp-per-thread-affinity-migration.patch`
- Modify: `config/deps.json` — update patches list from `[…0003]` to `[…0003, 0004, 0005]`
- Modify: `README.md` — update Status table, SMP section with real numbers
- Modify: `docs/notes-smp-bringup-status.md` — mark complete, add final measured results

---

## Task 1: Understand the exact file-level split from the live tree

**Files:** read-only investigation

- [ ] **Step 1: Generate a full diff from base to HEAD**

Run in `.deps/src/unikraft`:
```bash
git -C .deps/src/unikraft diff 7351f8b HEAD -- lib/ukallocbbuddy/ lib/ukboot/ plat/ > /tmp/patch-ap-bringup.diff
git -C .deps/src/unikraft diff 7351f8b HEAD -- lib/ukschedcoop/ lib/posix-process/ > /tmp/patch-schedcoop.diff
git -C .deps/src/unikraft diff 7351f8b HEAD -- lib/uksched/ > /tmp/patch-affinity.diff
wc -l /tmp/patch-ap-bringup.diff /tmp/patch-schedcoop.diff /tmp/patch-affinity.diff
```

Verify the three diffs together cover all changed files by comparing to:
```bash
git -C .deps/src/unikraft diff 7351f8b HEAD --stat
```
Expected: every file in the stat appears in exactly one of the three diffs.

- [ ] **Step 2: Confirm no file appears in two diffs**

Run:
```bash
git -C .deps/src/unikraft diff 7351f8b HEAD --name-only | sort > /tmp/all-changed.txt
{ git -C .deps/src/unikraft diff 7351f8b HEAD --name-only -- lib/ukallocbbuddy/ lib/ukboot/ plat/
  git -C .deps/src/unikraft diff 7351f8b HEAD --name-only -- lib/ukschedcoop/ lib/posix-process/
  git -C .deps/src/unikraft diff 7351f8b HEAD --name-only -- lib/uksched/ ; } | sort > /tmp/split-changed.txt
diff /tmp/all-changed.txt /tmp/split-changed.txt
```
Expected: no diff output (all files accounted for, none duplicated).

Note: `plat/native/pal/include/uk/plat/pal/ectx.h` is covered by 0001 (the existing ectx patch). Verify it is NOT in the `7351f8b..HEAD` diff; if it is, it belongs in 0003 (AP bring-up layer).

---

## Task 2: Generate the three patches

**Files:**
- Create: `patches/unikraft/0003-smp-ap-bringup-allocator.patch`
- Create: `patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch`
- Create: `patches/unikraft/0005-smp-per-thread-affinity-migration.patch`
- Delete: old `patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch`

- [ ] **Step 1: Generate patch 0003 — AP bring-up + allocator**

```bash
git -C .deps/src/unikraft diff 7351f8b HEAD \
  -- lib/ukallocbbuddy/ lib/ukboot/ plat/ \
  > patches/unikraft/0003-smp-ap-bringup-allocator.patch
```

Verify it is non-empty:
```bash
wc -l patches/unikraft/0003-smp-ap-bringup-allocator.patch
```
Expected: > 0 lines. If 0 lines, those files weren't changed — check with `git -C .deps/src/unikraft diff 7351f8b HEAD --stat -- lib/ukallocbbuddy/`.

- [ ] **Step 2: Generate patch 0004 — per-LCPU coop scheduler**

```bash
git -C .deps/src/unikraft diff 7351f8b HEAD \
  -- lib/ukschedcoop/ lib/posix-process/ \
  > patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch
```

Verify non-empty:
```bash
wc -l patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch
```
Expected: > 200 lines (schedcoop has smp.c which is ~330 lines new).

- [ ] **Step 3: Generate patch 0005 — per-thread affinity + migration fixes**

```bash
git -C .deps/src/unikraft diff 7351f8b HEAD \
  -- lib/uksched/ \
  > patches/unikraft/0005-smp-per-thread-affinity-migration.patch
```

Verify non-empty:
```bash
wc -l patches/unikraft/0005-smp-per-thread-affinity-migration.patch
```
Expected: > 200 lines (sched.c has ~244 lines added).

- [ ] **Step 4: Verify the three patches together equal the full diff**

```bash
# total lines in new patches
cat patches/unikraft/0003-smp-ap-bringup-allocator.patch \
    patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch \
    patches/unikraft/0005-smp-per-thread-affinity-migration.patch | wc -l

# full diff line count
git -C .deps/src/unikraft diff 7351f8b HEAD | wc -l
```
Expected: the concatenated patches total equals the full diff total (same content, different files).

- [ ] **Step 5: Dry-run apply on a clean base to confirm correctness**

```bash
# Test: reset a temp worktree, apply all patches, verify build
cd /tmp
git clone --local .deps/src/unikraft /tmp/uk-patch-test --branch v0.21.0 --depth 1 2>/dev/null || \
  git -C /mydata/JerryT/vm-final-project/virtio-gpu/.deps/src/unikraft worktree add --detach /tmp/uk-patch-test 7351f8b 2>/dev/null || true

for p in patches/unikraft/0001-pal-ectx-extern-c-linkage.patch \
          patches/unikraft/0002-ukpod-anon-include-assert.patch \
          patches/unikraft/0001-virtio-pci-modern-device-support.patch \
          patches/unikraft/0003-smp-ap-bringup-allocator.patch \
          patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch \
          patches/unikraft/0005-smp-per-thread-affinity-migration.patch; do
  git -C /tmp/uk-patch-test apply "$PWD/$p" && echo "OK: $p" || echo "FAIL: $p"
done
```

If a patch fails to apply, the file-path split likely has an ordering dependency. Fix: move the conflicting file to the earlier patch.

- [ ] **Step 6: Remove the old monolithic patch**

```bash
rm patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch
```

---

## Task 3: Update config/deps.json patch list

**Files:**
- Modify: `config/deps.json`

- [ ] **Step 1: Read the current patches list**

Read `config/deps.json`. Find the `"patches"` array under the unikraft entry. Current content (approximate):
```json
"patches": [
  "patches/unikraft/0001-pal-ectx-extern-c-linkage.patch",
  "patches/unikraft/0002-ukpod-anon-include-assert.patch",
  "patches/unikraft/0001-virtio-pci-modern-device-support.patch",
  "patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch"
]
```

- [ ] **Step 2: Replace with the three new patches**

Edit `config/deps.json` to replace the old `0003` entry with three entries:
```json
"patches": [
  "patches/unikraft/0001-pal-ectx-extern-c-linkage.patch",
  "patches/unikraft/0002-ukpod-anon-include-assert.patch",
  "patches/unikraft/0001-virtio-pci-modern-device-support.patch",
  "patches/unikraft/0003-smp-ap-bringup-allocator.patch",
  "patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch",
  "patches/unikraft/0005-smp-per-thread-affinity-migration.patch"
]
```

Also update the `"description"` field to read:
```
"Unikraft core. Patches in patches/unikraft/ are re-applied after every checkout/refresh (see scripts/deps.py). Patches 0003-0005 add multi-vCPU SMP support: AP bring-up + SMP-safe allocator (0003), per-LCPU cooperative scheduler with round-robin placement (0004), per-thread CPU affinity state + worker migration fixes + sched_{get,set}affinity syscalls (0005). Together they enable ggml workers to run one-per-vCPU, achieving 3.83x pp512 speedup at SMP=4."
```

- [ ] **Step 3: Verify `make deps` re-applies patches cleanly**

```bash
make deps 2>&1 | grep -E "patch|error|OK|already" | head -20
```
Expected: all three patches show `= patch already applied` (because the live tree already has them) — this proves `deps.py` accepts the new names.

- [ ] **Step 4: Commit patches + deps.json**

```bash
git add patches/unikraft/0003-smp-ap-bringup-allocator.patch \
        patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch \
        patches/unikraft/0005-smp-per-thread-affinity-migration.patch \
        config/deps.json
git rm patches/unikraft/0003-smp-per-lcpu-coop-scheduler-and-affinity.patch
git commit -m "patches(unikraft): split monolithic SMP patch into three logical layers

0003: AP bring-up (low .bss stack, CR3/EFER.NXE, uk_lcpu_init, SMP-safe bbuddy)
0004: per-LCPU ukschedcoop (one instance per vCPU, registry, round-robin placement)
0005: per-thread affinity (uk_thread mask, place_current, GS_BASE migration fix,
      IPI on cross-CPU futex wakeup, sched_setaffinity/sched_getaffinity syscalls)

The old monolithic 0003 did not include the Task 4 migration fixes. These three
patches apply cleanly in order on top of RELEASE-0.21.0 (tag 7351f8b)."
```

---

## Task 4: Update README.md

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Read README.md**

Read `README.md` in full.

- [ ] **Step 2: Update the Status at a glance table**

Find the table under `## Status at a glance`. The SMP rows currently show old numbers. Replace them with verified real-model numbers:

Replace these rows:
```markdown
| CPU bench (SMP) | `make llama-cpu-bench-run VOGUE_SMP=N` | `pass` — pp512 3699.3 / tg128 192.3 tok/s (4 vCPU, tiny model) |
| CPU server (SMP) | `make llama-cpu-server-run VOGUE_SMP=N` | `pass` — server_toks_per_s 54.34 (smp=1, tiny model) |
```

With:
```markdown
| CPU bench (SMP=4) | `make llama-cpu-bench-run VOGUE_SMP=4` | `pass` — pp512 115.8 / tg128 38.5 tok/s · **3.83x / 3.56x** vs SMP=1 (769MB model, KVM) |
| CPU server (SMP=4) | `make llama-cpu-server-run VOGUE_SMP=4` | `pass` — 19.2 tok/s · **2.10x** vs SMP=1 (769MB model, KVM) |
```

- [ ] **Step 3: Update the SMP subsection**

Find `### SMP: per-LCPU cooperative scheduler`. Replace its body with:

```markdown
### SMP: per-LCPU cooperative scheduler (branch `smp-a3-per-lcpu-scheduler`)

The CPU bench and server support `-smp N` multi-vCPU operation via three
Unikraft patches (`patches/unikraft/0003–0005`):

- **0003 — AP bring-up + allocator safety:** Low `.bss` AP bootstrap stack,
  CR3 + EFER.NXE adoption in AP entry, `uk_lcpu_init` call, SMP-safe bbuddy
  spinlock.
- **0004 — per-LCPU `ukschedcoop`:** One cooperative scheduler instance per
  online vCPU, per-LCPU scheduler registry, round-robin thread placement in
  `uk_clone` under `CONFIG_LIBUKSCHEDCOOP_SMP`.
- **0005 — per-thread affinity + migration:** Per-thread CPU affinity mask in
  `uk_thread`, `uk_schedcoop_smp_place_current` for explicit worker migration,
  correct GS_BASE patch in `schedcoop_thread_migrate_execenv`, IPI on
  cross-CPU futex wakeup (prevents cooperative-scheduler deadlock at ggml
  barrier), `sched_setaffinity` / `sched_getaffinity` syscalls wired to the
  Unikraft affinity path.
- **Runtime vCPU count** (`uk_schedcoop_smp_online_count`) replaces the
  build-time `@@VOGUE_SMP@@` macro so bench/server never spawn more polling
  workers than online vCPUs (which would deadlock the cooperative scheduler).

**Measured results (769 MB model, KVM, x86_64, `-smp 4`):**

| | SMP=1 | SMP=4 | Ratio |
|---|---|---|---|
| bench pp512 (prompt eval) | 30.2 tok/s | 115.8 tok/s | **3.83x** |
| bench tg128 (token gen) | 10.8 tok/s | 38.5 tok/s | **3.56x** |
| server toks/s | 9.2 tok/s | 19.2 tok/s | **2.10x** |

Set `VOGUE_SMP=N` on the make command line to build for N vCPUs and launch with `-smp N`:

```sh
make llama-cpu-bench-run VOGUE_SMP=4 ARCH=x86_64 MODEL=models/model.gguf
make llama-cpu-server-run VOGUE_SMP=4 ARCH=x86_64 MODEL=models/model.gguf
```
```

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs(README): update SMP status with real-model results (3.83x pp512, 2.10x server)

Replace tiny-model placeholder numbers with verified 769MB-model measurements.
Update SMP section to describe the three logical patches (0003-0005) and list
the three root-cause fixes from the migration debugsession."
```

---

## Task 5: Update docs/notes-smp-bringup-status.md

**Files:**
- Modify: `docs/notes-smp-bringup-status.md`

- [ ] **Step 1: Read notes-smp-bringup-status.md**

Read `docs/notes-smp-bringup-status.md`.

- [ ] **Step 2: Prepend a status banner and results summary**

Add at the very top of the file (before any existing content):

```markdown
# SMP Bring-up Status

**Status: COMPLETE (2026-06-14).** Multi-vCPU bench and server both verified.
Active plan: `docs/superpowers/plans/2026-06-14-multi-vcpu-speedup.md`.
Patches: `patches/unikraft/0003–0005`.

## Final measured results (769 MB model, KVM x86_64, -smp 4)

| Metric | SMP=1 | SMP=4 | Ratio |
|--------|-------|-------|-------|
| bench pp512 | 30.2 tok/s | 115.8 tok/s | **3.83x** |
| bench tg128 | 10.8 tok/s | 38.5 tok/s | **3.56x** |
| server toks/s | 9.2 tok/s | 19.2 tok/s | **2.10x** |

## Root causes fixed during implementation

1. **Uninitialized `target_lcpu`** in `schedcoop_thread_migrate_execenv`
   caused wrong GS_BASE computation → every migrated thread reported
   `sched_getcpu()==0`.
2. **Wrong GS_BASE in migrated thread's execenv** — now patched to
   `target_lcpu * _uk_pcpuvar_tmpl_size_ptr` after migration (same formula as
   `lcpu_start.S` AP bootstrap).
3. **Missing IPI on cross-CPU futex wakeup** — `schedcoop_thread_woken_isr`
   now calls `uk_lcpu_wakeup()` for remote LCPUs, preventing the cooperative
   scheduler deadlock at ggml graph barriers.

---

```

- [ ] **Step 3: Commit**

```bash
git add docs/notes-smp-bringup-status.md
git commit -m "docs(smp): mark bring-up complete, add final results and root causes"
```

---

## Task 6: Final verification

- [ ] **Step 1: Verify all patches listed in deps.json exist on disk**

```bash
python3 -c "
import json, pathlib
cfg = json.loads(pathlib.Path('config/deps.json').read_text())
for entry in cfg.get('dependencies', cfg if isinstance(cfg, list) else [cfg]):
    for p in entry.get('patches', []):
        ok = pathlib.Path(p).exists()
        print('OK' if ok else 'MISSING', p)
"
```
Expected: all lines start with `OK`.

- [ ] **Step 2: Verify make test-fast still passes**

```bash
make test-fast 2>&1 | tail -5
```
Expected: all pass.

- [ ] **Step 3: Verify make deps reports patches as already applied**

```bash
make deps 2>&1 | grep -E "patch|already|error"
```
Expected: each of the 6 patches shows `= patch already applied`.

- [ ] **Step 4: Verify git log is clean**

```bash
git log --oneline -6
```
Expected: three new commits on top (patches+deps, README, notes).
