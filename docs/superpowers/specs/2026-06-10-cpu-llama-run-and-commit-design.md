# Design: Commit build fixes, fix CPU-llama run, document workflow

Date: 2026-06-10
Branch: `dev-jerry-arm64`

## Problem

The arm64 `llama-cpu-server` build chain was repaired across a prior session
(GCC-16 / GNU-make compatibility, Kraftfile path placeholder, libstdc++ EH
shims, an upstream `ectx.h` `extern "C"` patch, Vulkan/SPIRV header source fix).
Those changes sit uncommitted in the working tree. Two gaps remain:

1. **Runtime blocker.** `make llama-cpu-run ARCH=arm64` aborts with
   `blocked:model-missing`. `MODEL` defaults to `$(CURDIR)/models/model.gguf`,
   which does not exist; the host actually carries
   `models/google_gemma-4-E2B-it-Q4_K_M.gguf`.
2. **Undocumented workflow.** `README.md` does not yet describe the
   build → run flow for CPU llama, nor record that the validated target is
   arm64 (Apple Silicon) with specific GCC-16 toolchain caveats.

## Goals

- Commit the working-tree changes as a clean, logically grouped history.
- Make `make llama-cpu-run ARCH=arm64` work out-of-the-box on this host.
- Prove the CPU-llama appliance boots and reaches a PASS/READY marker under QEMU.
- Document the build-and-run workflow in `README.md`, explicitly noting the
  validated architecture and toolchain caveats.

## Non-goals

- Verifying x86_64, `llama-vk`, `llama-vk-server`, or kmscube targets this round
  (documented as build-designed but unverified).
- Committing scratch artifacts: `.kraft-gen/`, `.config.vogue_*`,
  `plan-optimize.md`, `vogue_architecture_guide.md`, `results/llama/*.json`.

## Design

### 1. Runtime fix — auto-detect default MODEL (Makefile)

Replace the fixed default with one that prefers an explicit `model.gguf` and
otherwise falls back to the sole gguf present in `models/`:

```makefile
MODEL ?= $(or $(wildcard $(CURDIR)/models/model.gguf),$(firstword $(wildcard $(CURDIR)/models/*.gguf)))
```

- Resolves to the gemma gguf on this host with no file copying.
- `MODEL=/path/to.gguf` still overrides (it is `?=`).
- No change to `scripts/llama_cpu.py` or `scripts/common.py`.
- Edge case: if `models/` holds multiple ggufs and no `model.gguf`,
  `firstword` picks the lexicographically-first wildcard match; the README
  documents `MODEL=` override for that case.

### 2. README workflow + architecture note

Add a "Build & run CPU llama" subsection covering:

- `make deps` → `make llama-cpu-server-build ARCH=arm64`
  (or `llama-cpu-build`) → `make llama-cpu-run ARCH=arm64`.
- The `MODEL=` override and the auto-detect default behavior.
- An explicit **validated-architecture** note: arm64 / Apple Silicon,
  `qemu-system-aarch64` with `hvf` acceleration.
- The **GCC-16 toolchain caveats**: Homebrew GNU make on PATH (gnubin),
  `UK_CFLAGS=-std=gnu17` / `UK_CXXFLAGS=-std=gnu++17 -fpermissive`, and the
  repo-tracked `ectx.h` `extern "C"` patch reapplied by `make deps`.
- A line noting x86_64 and the Vulkan targets are build-designed but not
  verified in this round.

### 3. Verification (implementation-phase test gate)

- Build-system test: `make llama-cpu-server-build ARCH=arm64` stays green
  (exit 0, image produced under `.unikraft/build/`).
- Full run proof: `make llama-cpu-run ARCH=arm64` boots the arm64 unikernel
  under QEMU with the gemma model and reaches the bench PASS marker
  (`uk-llama-cpu: PASS` / `pp512=… tg128=…`) captured to `results/llama/`.
  A non-pass marker is treated as a failure to investigate, not a deliverable.

### 4. Commit split (logical, dependency order)

1. **deps: fix Vulkan/SPIRV header sources & add patch mechanism**
   — `scripts/deps.py`, `config/deps.json`,
   `patches/unikraft/0001-pal-ectx-extern-c-linkage.patch`
2. **build: resolve Kraftfile unikraft path via @@UNIKRAFT_LOCAL@@ placeholder**
   — 6 × `kraft/Kraftfile.*`, `mk/llama.mk`, `.gitignore`
3. **build: GCC-16 / GNU-make compatibility (flags + gnubin PATH)**
   — `Makefile`
4. **app: fix llama-cpu sources, source variants & libstdc++ EH shim**
   — `apps/app-llama-cpu/Makefile.uk`, `apps/app-llama-cpu/uk_stdcxx_compat.cpp`
5. **vk: libvulkan EH shim & Makefile.uk touch-ups**
   — `apps/app-llama-vk/Makefile.uk`, `libs/libvulkan/Makefile.uk`,
   `libs/libvulkan/uk_stdcxx_compat.cpp`, `libs/libukvulkan_venus/Makefile.uk`
6. **run+docs: auto-detect MODEL default & document CPU-llama workflow**
   — `Makefile` (MODEL line), `README.md`

The staged `deleted: Kraftfile` (obsolete root Kraftfile) rides with commit 2,
since it is part of the Kraftfile path/placeholder reorganization.

Each commit message: concise imperative subject (≤ ~72 chars) + a short body
explaining the why. Co-authored-by trailer appended.

## Risks

- The full QEMU run may surface a deeper runtime issue beyond the model path
  (e.g. 9p mount, console, or ggml-on-unikraft behavior). If so, that becomes a
  debugging sub-task (systematic-debugging) before the run+docs commit can claim
  a verified pass.
- `firstword` model auto-detect is host-state dependent; mitigated by the
  documented `MODEL=` override.
