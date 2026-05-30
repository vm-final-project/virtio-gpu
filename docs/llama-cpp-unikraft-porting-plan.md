# llama.cpp Unikraft porting plan

This plan adapts `unikraft-porting.md` to VOGUE's minimized llama.cpp design.
The rule is simple: **upstream llama.cpp owns model parsing, ggml graph logic,
bench, and server behavior; VOGUE only provides the Unikraft appliance wiring and
the minimal VirtIO-GPU/Venus/Vulkan glue required by upstream ggml-vulkan.**

## Current ownership

| Surface | Owner | Purpose |
|---|---|---|
| `apps/app-llama-upstream` | Unikraft app | CPU bench-only and server-only single-application images. |
| `apps/app-llama-upstream-vk` | Unikraft app | Vulkan bench-only and server-only single-application images using upstream ggml-vulkan. |
| `libs/libukggml_vk` | Unikraft library | Static Vulkan loader/dispatch + Venus bridge + SPIR-V shader object wiring for upstream `ggml-vulkan.cpp`. |
| `config/llama_env_matrix.json` | Test/eval config | Selects baremetal/QEMU/Unikraft CPU, Vulkan, and CUDA environments, threads, model, args, and claim boundaries. |

Removed duplicate layers: the former local model, ggml-substrate, and llama-adapter libraries. They are replaced by upstream llama.cpp and host-native `vk.ggml-dispatch` coverage for the minimal dispatch library.

## Stage 0 — Linux and environment baselines

Objective: prove the same model and arguments in normal environments before
promoting Unikraft rows.

1. Use one pinned upstream checkout (`/home/jerrytsai/llama.cpp` by default).
2. Build upstream variants outside Unikraft:
   - CPU: `build-cpu/bin/llama-bench`
   - Vulkan: `build-vulkan/bin/llama-bench`
   - CUDA: `build-cuda/bin/llama-bench`
3. Run through `scripts/llama_env_matrix.py` so every row records:
   - family (`baremetal+cuda`, `baremetal+vulkan`, `baremetal+cpu`,
     `qemu+linux+vulkan`, `qemu+linux+cpu`, `qemu+unikraft+vulkan`,
     `qemu+unikraft+cpu`),
   - thread count,
   - model path,
   - backend flags such as `-ngl 99`,
   - allowed and forbidden claims.
4. Store raw evidence under `results/llama-env/` or `results/llama-bench/`.

Validation:

```sh
make llama-env-check
make llama-env-bench
make llama-env-server
```

## Stage 1 — ELF Loader discovery only

Binary compatibility is not the release path for this artifact. Use Unikraft
ELF Loader only as a fast x86_64 prototype when native porting hits unknown
syscall, VFS, or socket failures. This mirrors Unikraft's compatibility mode:
the loader boots and immediately loads a PIE Linux `llama-server` ELF.

1. Build a PIE/static-PIE upstream `llama-server` with the same model and args.
2. Boot `app-elfloader` with `/llama-server -m /models/model.gguf --host 0.0.0.0 --port 8080`.
3. Record missing syscalls/POSIX needs; do not add a shell image, service
   manager, or bincompat row as the benchmark path.

Exit criterion: a bounded syscall/POSIX list for the native Stage 2 path.
AArch64 pKVM/RME work should prefer native porting unless the exact Unikraft
version supports binary compatibility on that target.

## Stage 2 — Native CPU single-application images

Objective: compile upstream llama.cpp into a Unikraft image that boots directly
into one entrypoint. Native Unikraft must not use a C launcher that calls
`fork()`/`exec()` to start `llama-server`.

1. Build upstream static libraries with `cmake/unikraft-clang.cmake`.
2. Link through `apps/app-llama-upstream/Makefile.uk`.
3. Select exactly one mode in Kconfig/Kraftfile:
   - `CONFIG_APP_LLAMA_UPSTREAM_MODE_BENCH=y` for bench-only.
   - `CONFIG_APP_LLAMA_UPSTREAM_MODE_SERVER=y` for server-only.
   A future full HTTP port should refactor upstream server startup into
   `llama_server_main(argc, argv)` and call it directly from `main()`.
4. Use Unikraft external libraries in normal style: `lib-musl`, `lib-libcxx`,
   `lib-libcxxabi`, `lib-libunwind`, `lib-compiler-rt`, `lib-pthread-embedded`.
   Upstream `lib-musl` already provides `sysconf`, `getauxval`, `prctl`, and
   `pthread_setaffinity_np`; no project-local POSIX shim is needed.
5. Mount the model via 9pfs/initramfs as described by the environment matrix.

Validation:

```sh
make llama-upstream-cpu-build
make llama-upstream-cpu-run
```

A passing runtime must emit `evidence_id=llama-upstream-cpu`. Without that line,
the row stays `blocked:*` and cannot be used as throughput evidence.

## Stage 2b — Native Vulkan/Venus/SPIR-V image

Objective: link upstream `ggml-vulkan.cpp` without forking ggml or inventing a
custom compute-remoting ABI.

1. Keep upstream ggml sources in-tree through `libs/libukggml_vk/Makefile.uk`:
   `ggml.c`, `ggml-backend.cpp`, `gguf.cpp`, `ggml-vulkan.cpp`, and generated
   `*.comp.cpp` SPIR-V shader blobs from the upstream Vulkan build.
2. Keep local code limited to:
   - `uk_vulkan_dispatch.c`: pinned Vulkan C ABI entries used by ggml-vulkan,
   - `uk_ggml_vk_loader.cpp`: static dispatcher hook for Vulkan-Hpp,
   - `uk_stdcxx_compat.cpp`: narrow compatibility shims for the Unikraft C++ link.
3. Route Vulkan calls through existing Unikraft/VOGUE libraries:
   `libukvenus` -> `libukvirtgpu_drm` -> `libukvirtio_gpu` -> QEMU
   `virtio-gpu-gl-pci,blob=true,venus=true`.
4. Do not reintroduce local GGUF/model/llama libraries; upstream llama.cpp owns
   those responsibilities.

Validation:

```sh
make llama-vulkan-api-coverage
make llama-ggml-vk-dispatch
make llama-upstream-vk-build
make llama-upstream-vk-run
```

Only `llm.bench.vk` or `llm.bench.vk.real` same-run PASS artifacts may support GPU
runtime/throughput claims. `vk.ggml-dispatch` proves the dispatch substrate
only.

## Stage 3 — Catalog-style cleanup and CI

1. Keep Kraftfiles small and single-purpose:
   - `kraft/Kraftfile.llama-upstream-bench`
   - `kraft/Kraftfile.llama-upstream-server`
   - `kraft/Kraftfile.llama-upstream-vk`
   - `kraft/Kraftfile.llama-upstream-vk-server`
2. Keep dependency declarations explicit; no hidden shell/rootfs utilities.
3. Regenerate evidence and docs with:

```sh
make test-fast
make eval-check
python3 scripts/app_multi_env_bench.py
make current-stage-check
make paper-check
```

## Claim boundaries

Allowed after current local gates: source-level porting structure, environment
plans, upstream CPU/Vulkan single-app image wiring, required ggml-vulkan API
coverage, and native static dispatch regression evidence.

Forbidden without same-run runtime artifacts: Unikraft token/s, GPU acceleration,
QEMU/Venus throughput, full HTTP serving, native `fork()`/`exec()` launchers, or
any claim that a structured blocker is a pass.
