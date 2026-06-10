# CPU-llama Run Fix & Commit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Commit the in-tree arm64 build fixes as a clean grouped history, make `make llama-cpu-run ARCH=arm64` work out-of-the-box, prove it boots to a PASS marker under QEMU, and document the workflow + validated architecture in README.

**Architecture:** The build edits already exist uncommitted in the working tree. This plan verifies the build gate, applies the two remaining changes (auto-detect `MODEL` default; README workflow/arch docs), proves the run end-to-end under QEMU, then records everything as six logically grouped commits in dependency order. The build and the QEMU run are the test gates — run them, observe the expected output, then commit.

**Tech Stack:** Unikraft 0.21.0 + KraftKit, GNU Make (Homebrew gnubin), aarch64-elf-gcc 16, QEMU `qemu-system-aarch64` (hvf), Python run harness (`scripts/llama_cpu.py`).

---

## File Structure

- `Makefile` — build vars + the `MODEL ?=` default line (edited twice across two commits: existing GCC/gnubin edits land in commit 3; the new MODEL auto-detect lands in commit 6).
- `README.md` — add a "Build & run CPU llama" subsection (commit 6).
- All other touched files already carry their final content in the working tree; their tasks are *stage + commit*, not *edit*.

Commit order (dependency order):
1. deps fixes + patch mechanism
2. Kraftfile placeholder + path resolution (includes `deleted: Kraftfile`)
3. GCC-16 / GNU-make compatibility (Makefile)
4. llama-cpu app sources/variants/EH shim
5. libvulkan/venus EH shim + touch-ups
6. MODEL auto-detect + README docs

---

## Task 0: Baseline verification (build gate must already be green)

**Files:** none (verification only)

- [ ] **Step 1: Confirm GNU make + toolchain on PATH**

Run:
```bash
gmake --version | head -1
ls /opt/homebrew/opt/make/libexec/gnubin/make 2>/dev/null || ls /usr/local/opt/make/libexec/gnubin/make
which qemu-system-aarch64
```
Expected: GNU Make ≥ 4.1, a gnubin `make` path printed, and a qemu path.

- [ ] **Step 2: Run the build gate**

Run:
```bash
make llama-cpu-server-build ARCH=arm64 2>&1 | tail -20
```
Expected: ends with a successful kraft build and exit 0; image exists:
```bash
ls -la .unikraft/build/vogue-llama-cpu-server_qemu-arm64
```
Expected: file present (multi-MB).

- [ ] **Step 3: If the build is NOT green**, STOP and switch to
  superpowers:systematic-debugging before continuing. Do not proceed to commits
  with a red build. (Per prior session this should be green; this step only
  guards against drift.)

---

## Task 1: Commit 1 — deps fixes + patch mechanism

**Files:**
- Modify: `scripts/deps.py`
- Modify: `config/deps.json`
- Create: `patches/unikraft/0001-pal-ectx-extern-c-linkage.patch`

- [ ] **Step 1: Confirm patch reapply is idempotent (gate)**

Run:
```bash
python3 - <<'PY'
import subprocess, pathlib
root = pathlib.Path(".").resolve()
uni = root/".deps/src/unikraft"
patch = root/"patches/unikraft/0001-pal-ectx-extern-c-linkage.patch"
# already-applied check used by deps.py:
r = subprocess.run(["git","-C",str(uni),"apply","--reverse","--check",str(patch)],
                   capture_output=True)
print("reverse-check rc:", r.returncode, "(0 == already applied)")
PY
```
Expected: `reverse-check rc: 0 (0 == already applied)` — confirms the tracked
patch matches the in-tree `ectx.h` and `deps.py` will skip re-applying.

- [ ] **Step 2: Stage and commit**

```bash
git add scripts/deps.py config/deps.json patches/unikraft/0001-pal-ectx-extern-c-linkage.patch
git commit -m "deps: fix Vulkan/SPIRV header sources and add patch mechanism

Re-pin Vulkan-Headers/SPIRV-Headers to KhronosGroup upstream and add an
idempotent patch-apply step to scripts/deps.py so repo-tracked fixes to
pinned upstreams (the ectx.h extern \"C\" patch) survive deps-refresh.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 3: Verify commit isolated**

Run: `git show --stat HEAD | head -20`
Expected: exactly the three files above.

---

## Task 2: Commit 2 — Kraftfile placeholder + path resolution

**Files:**
- Modify: `kraft/Kraftfile.kmscube-vgpu-gl`, `kraft/Kraftfile.llama-cpu`,
  `kraft/Kraftfile.llama-cpu-bench`, `kraft/Kraftfile.llama-cpu-server`,
  `kraft/Kraftfile.llama-vk`, `kraft/Kraftfile.llama-vk-server`
- Modify: `mk/llama.mk`, `.gitignore`
- Delete: `Kraftfile` (obsolete root Kraftfile, already staged-deleted)

- [ ] **Step 1: Sanity-check placeholder wiring (gate)**

Run:
```bash
grep -l '@@UNIKRAFT_LOCAL@@' kraft/Kraftfile.* | wc -l
grep -n 'UNIKRAFT_LOCAL\|kraft_build\|KRAFT_GEN_DIR' mk/llama.mk | head
grep -n 'kraft-gen' .gitignore
```
Expected: 6 Kraftfiles contain the placeholder; `mk/llama.mk` defines
`KRAFT_GEN_DIR`, `UNIKRAFT_LOCAL`, and the `kraft_build` macro; `.gitignore`
ignores `.kraft-gen/`.

- [ ] **Step 2: Stage and commit**

```bash
git add kraft/Kraftfile.kmscube-vgpu-gl kraft/Kraftfile.llama-cpu \
        kraft/Kraftfile.llama-cpu-bench kraft/Kraftfile.llama-cpu-server \
        kraft/Kraftfile.llama-vk kraft/Kraftfile.llama-vk-server \
        mk/llama.mk .gitignore Kraftfile
git commit -m "build: resolve unikraft path via @@UNIKRAFT_LOCAL@@ placeholder

Kraftfiles resolved paths relative to their own directory, breaking the
local unikraft checkout reference. Replace the git source/version with a
@@UNIKRAFT_LOCAL@@ placeholder that mk/llama.mk substitutes with an
absolute path into .kraft-gen/ before kraft build. Drop the obsolete
root Kraftfile and ignore the generated .kraft-gen/ dir.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 3: Verify**

Run: `git show --stat HEAD | head -20`
Expected: the 6 Kraftfiles, `mk/llama.mk`, `.gitignore`, and deleted `Kraftfile`.

---

## Task 3: Commit 3 — GCC-16 / GNU-make compatibility

**Files:**
- Modify: `Makefile` (gnubin PATH prepend + `UK_CFLAGS`/`UK_CXXFLAGS` defaults + exports)

> NOTE: The MODEL line is also in `Makefile` but belongs to commit 6. Do NOT
> change it yet. Stage `Makefile` with `git add -p` to pick only the
> GCC/gnubin/UK_*FLAGS hunks here, leaving the MODEL hunk for Task 6.
> Since the MODEL edit has not been made yet at this point, a plain
> `git add Makefile` is safe — the working tree still has the original MODEL
> line, which is unchanged and will not appear as a diff.

- [ ] **Step 1: Confirm the compat edits are present (gate)**

Run:
```bash
grep -n 'GNUBIN\|UK_CFLAGS\|UK_CXXFLAGS\|gnubin' Makefile | head
```
Expected: a `GNUBIN := $(firstword $(wildcard ...gnubin ...))` line, an
`export PATH := $(GNUBIN):$(PATH)` guarded by `ifneq`, `UK_CFLAGS ?= -std=gnu17`,
`UK_CXXFLAGS ?= -std=gnu++17 -fpermissive`, and `export UK_CFLAGS UK_CXXFLAGS`.

- [ ] **Step 2: Confirm MODEL line is still the ORIGINAL (not yet edited)**

Run: `grep -n 'MODEL ?=' Makefile`
Expected: `MODEL ?= $(CURDIR)/models/model.gguf` (unchanged — its edit is Task 6).

- [ ] **Step 3: Stage and commit**

```bash
git add Makefile
git commit -m "build: GCC-16 and GNU-make compatibility on macOS

Prepend Homebrew gnubin so KraftKit's unikraft sub-make sees GNU make
>= 4.1, and inject UK_CFLAGS=-std=gnu17 / UK_CXXFLAGS=-std=gnu++17
-fpermissive via the sanctioned global flag hooks so the GCC-16 C23
default does not break unikraft (validated against GCC 11-14).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 4: Verify**

Run: `git show --stat HEAD`
Expected: only `Makefile`. Run `git diff -- Makefile`: empty (no MODEL change yet).

---

## Task 4: Commit 4 — llama-cpu app sources/variants/EH shim

**Files:**
- Modify: `apps/app-llama-cpu/Makefile.uk`
- Create: `apps/app-llama-cpu/uk_stdcxx_compat.cpp`

- [ ] **Step 1: Confirm shim + variant wiring present (gate)**

Run:
```bash
grep -n '__cxa_call_terminate' apps/app-llama-cpu/uk_stdcxx_compat.cpp
grep -n '|core\||x86\||arm\||mdl\|BUILTIN_INC\|uk_stdcxx_compat' apps/app-llama-cpu/Makefile.uk | head
```
Expected: shim defines `__cxa_call_terminate`; `Makefile.uk` has the
`source|variant` suffixes, the builtin-include `-isystem`, and adds the shim
to SRCS.

- [ ] **Step 2: Stage and commit**

```bash
git add apps/app-llama-cpu/Makefile.uk apps/app-llama-cpu/uk_stdcxx_compat.cpp
git commit -m "app: fix llama-cpu sources, variants and libstdc++ EH shim

Disambiguate colliding ggml basenames with source|variant suffixes, add
the compiler builtin-include dir for arm_neon.h/immintrin.h, drop a stray
line-continuation backslash, and provide a weak __cxa_call_terminate shim
(absent from LLVM libc++abi) so GCC-compiled ggml links.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 3: Verify**

Run: `git show --stat HEAD`
Expected: exactly the two files above.

---

## Task 5: Commit 5 — libvulkan/venus EH shim + touch-ups

**Files:**
- Modify: `apps/app-llama-vk/Makefile.uk`, `libs/libvulkan/Makefile.uk`,
  `libs/libvulkan/uk_stdcxx_compat.cpp`, `libs/libukvulkan_venus/Makefile.uk`

- [ ] **Step 1: Confirm vk-side shim present (gate)**

Run: `grep -n '__cxa_call_terminate' libs/libvulkan/uk_stdcxx_compat.cpp`
Expected: the shim is present in the libvulkan compat file.

- [ ] **Step 2: Stage and commit**

```bash
git add apps/app-llama-vk/Makefile.uk libs/libvulkan/Makefile.uk \
        libs/libvulkan/uk_stdcxx_compat.cpp libs/libukvulkan_venus/Makefile.uk
git commit -m "vk: add libvulkan EH shim and Makefile.uk touch-ups

Mirror the weak __cxa_call_terminate shim into the libvulkan compat unit
so the Vulkan llama path resolves the GCC EH helper, plus minor Makefile.uk
adjustments for the vk/venus libraries.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 3: Verify**

Run: `git show --stat HEAD`
Expected: exactly the four files above.

---

## Task 6: Commit 6 — MODEL auto-detect + README docs (the new edits)

**Files:**
- Modify: `Makefile` (the `MODEL ?=` line)
- Modify: `README.md`

- [ ] **Step 1: Edit the MODEL default in `Makefile`**

Replace:
```makefile
MODEL ?= $(CURDIR)/models/model.gguf
```
with:
```makefile
# Default model: prefer models/model.gguf, else the sole *.gguf in models/.
# Override with MODEL=/path/to/your.gguf.
MODEL ?= $(or $(wildcard $(CURDIR)/models/model.gguf),$(firstword $(wildcard $(CURDIR)/models/*.gguf)))
```

- [ ] **Step 2: Verify the default now resolves (gate)**

Run:
```bash
make -f - <<'MK'
include Makefile
print-model:
	@echo "MODEL=$(MODEL)"
MK
```
If the include-based probe is awkward, use instead:
```bash
make ARCH=arm64 -p 2>/dev/null | grep '^MODEL =' | head -1
```
Expected: `MODEL = .../models/google_gemma-4-E2B-it-Q4_K_M.gguf` (a real,
existing file). Confirm it exists:
```bash
test -f "$(make ARCH=arm64 -p 2>/dev/null | sed -n 's/^MODEL = //p' | head -1)" && echo MODEL_OK
```
Expected: `MODEL_OK`.

- [ ] **Step 3: Add the README "Build & run CPU llama" subsection**

In `README.md`, after the existing build/dependency documentation, add a
subsection with this content (adapt heading depth to match surrounding doc):

```markdown
### Build & run CPU llama (arm64 / Apple Silicon — validated path)

This is the build-and-run path validated in this repo. It targets
**arm64** under `qemu-system-aarch64` with `hvf` acceleration.

```sh
make deps                              # fetch upstreams + reapply tracked patches
make llama-cpu-server-build ARCH=arm64 # or: llama-cpu-build
make llama-cpu-run ARCH=arm64          # boots the appliance under QEMU
```

The run mounts a model into the guest over virtio-9p. `MODEL` defaults to
`models/model.gguf`, falling back to the sole `*.gguf` in `models/`. Override
explicitly when you keep several models:

```sh
make llama-cpu-run ARCH=arm64 MODEL=models/your-model.gguf
```

**Toolchain caveats (macOS + GCC 16).** Unikraft 0.21.0 is validated against
GCC 11-14, but this host uses GCC 16, so the Makefile:

- prepends Homebrew GNU make (`gnubin`) to `PATH` (KraftKit's sub-make needs
  GNU make >= 4.1; macOS ships 3.81),
- injects `UK_CFLAGS=-std=gnu17` / `UK_CXXFLAGS=-std=gnu++17 -fpermissive`
  so the GCC-16 C23 default does not break the build,
- reapplies a tracked `extern "C"` patch to unikraft's `ectx.h` via
  `make deps` (see `patches/unikraft/`).

**Other targets.** The `x86_64` CPU path and the Vulkan targets
(`llama-vk`, `llama-vk-server`) are build-designed but not verified in this
round; they may need their own toolchain adjustments.
```

- [ ] **Step 4: Run the build gate after the Makefile edit**

Run:
```bash
make llama-cpu-server-build ARCH=arm64 2>&1 | tail -5
```
Expected: still green (the MODEL change does not affect the build target).

- [ ] **Step 5: Stage and commit**

```bash
git add Makefile README.md
git commit -m "run: auto-detect MODEL default and document CPU-llama workflow

Default MODEL now falls back to the sole gguf in models/ so
'make llama-cpu-run' works without an explicit MODEL=. Document the
arm64-validated build/run flow, the MODEL override, and the GCC-16
toolchain caveats in README.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 6: Verify**

Run: `git show --stat HEAD`
Expected: exactly `Makefile` and `README.md`.

---

## Task 7: Run proof — boot CPU llama under QEMU to PASS

**Files:**
- Output (untracked, NOT committed): `results/llama/llama_cpu_arm64.json`

- [ ] **Step 1: Run the bench appliance end-to-end**

Run (allow time — 3.4 GB model copy + QEMU boot; bump timeout):
```bash
make llama-cpu-run ARCH=arm64 RUN_TIMEOUT=600 2>&1 | tail -30
```
Expected: prints `llama-cpu-bench: pass`. (The harness writes
`results/llama/llama_cpu_arm64.json`.)

- [ ] **Step 2: Inspect the result JSON**

Run:
```bash
python3 -c "import json;d=json.load(open('results/llama/llama_cpu_arm64.json'));print(d['status']);print(d.get('metrics'))"
```
Expected: `status` is `pass` and `metrics` contains `pp512` / `tg128` numbers.

- [ ] **Step 3: If status is NOT pass**

The model path is fixed, so a non-pass here is a deeper runtime issue (9p
mount, console wiring, ggml-on-unikraft). STOP and switch to
superpowers:systematic-debugging. Capture the last 2000 chars of the log
(already embedded in the result JSON `error` field) as the investigation seed.
Do not claim completion until status is `pass`.

- [ ] **Step 4: Do NOT commit the results JSON** (per spec non-goals).

Run: `git status --porcelain results/llama/`
Expected: the JSON shows as untracked (`??`) and is left uncommitted.

---

## Task 8: Final verification (verification-before-completion)

**Files:** none

- [ ] **Step 1: Confirm clean, grouped history**

Run: `git log --oneline -7`
Expected: 6 implementation commits + the earlier spec commit, in order.

- [ ] **Step 2: Confirm working tree holds only non-deliverables**

Run: `git status --porcelain`
Expected: only untracked scratch — `.kraft-gen/` (gitignored, won't show),
`.config.vogue_*`, `plan-optimize.md`, `vogue_architecture_guide.md`,
`results/llama/*.json`, and the `docs/superpowers/` plan file (commit or leave
per preference). No tracked modifications remain.

- [ ] **Step 3: Re-run the build gate one final time**

Run: `make llama-cpu-server-build ARCH=arm64 2>&1 | tail -3`
Expected: green (exit 0).

- [ ] **Step 4: State the evidence**

Report, with the actual command outputs: build green, run `status: pass` with
the pp512/tg128 numbers, and the six-commit log. No success claim without this
evidence.

---

## Self-Review Notes

- **Spec coverage:** runtime fix (Task 6), README + arch note (Task 6 Step 3),
  build-system test (Tasks 0/3/6/8 gates), full QEMU run proof (Task 7),
  six-commit logical split (Tasks 1-6), non-goals respected (Task 7 Step 4,
  Task 8 Step 2). All spec sections map to a task.
- **Placeholders:** none — every step has concrete commands/content.
- **Type/name consistency:** image name `vogue-llama-cpu-server_qemu-arm64`,
  result file `results/llama/llama_cpu_arm64.json` and marker
  `llama-cpu-bench: pass` match `scripts/llama_cpu.py` and `scripts/common.py`.
