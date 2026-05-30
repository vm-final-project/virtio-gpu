# app-llama-upstream — llama.cpp CPU single-application port

This port follows the four-stage template in `unikraft-porting.md`. The CPU
appliance is a single-purpose image: the VM boots straight into `llama-bench`
or a native server-mode entrypoint. It is not a Linux VM that starts a shell or
`fork()`/`exec()` launcher.

## Upstream provenance

- Repository: <https://github.com/ggml-org/llama.cpp>
- License: MIT
- Pinned commit and local checkout (`LLAMA_ROOT`) are recorded in
  `results/vogue_latest_evaluation_matrix.json` and `config/llama_env_matrix.json`.
- No upstream source modifications: `git -C $LLAMA_ROOT diff --stat` must be empty.

## Stage 0 — Baselines

Verify the same model + flags on Linux first so any Unikraft regression is
attributable to the port, not to llama.cpp itself.

```sh
make llama-env-list                 # Show baremetal/QEMU/Unikraft × CPU/Vulkan/CUDA cells
make llama-env-check                # Validate config/llama_env_matrix.json
make llama-upstream-cmake           # Build upstream libllama.a with cmake/unikraft-clang.cmake
```

## Stage 1 — Binary compatibility (discovery only)

Optional and x86_64-only for this artifact. Use Unikraft ELF Loader to run a
PIE Linux `llama-server` binary only when discovering missing syscalls, VFS, or
network assumptions. Do not promote the bincompat image as the release path;
the native image below is the research artifact.

## Stage 2 — Native single-application image

Static-link upstream `libllama.a`, `libggml*.a` against Unikraft's external
libraries and run exactly one entrypoint. A future full HTTP port should expose
`llama_server_main(argc, argv)` and call it directly from `main()`, not launch a
second binary. Two single-purpose Kraftfiles back this app:

| Mode | Kraftfile | Kconfig |
|---|---|---|
| Bench | `kraft/Kraftfile.llama-upstream-bench` | `CONFIG_APP_LLAMA_UPSTREAM_MODE_BENCH=y` |
| Server | `kraft/Kraftfile.llama-upstream-server` | `CONFIG_APP_LLAMA_UPSTREAM_MODE_SERVER=y` |

External libraries used (sibling source trees, not vendored):

- `lib-musl`, `lib-libcxx`, `lib-libcxxabi`, `lib-libunwind`, `lib-compiler-rt`,
  `lib-pthread-embedded`.

Build/run:

```sh
make llama-upstream-cpu-build
make llama-upstream-cpu-run
make llama-upstream-cpu-check
```

The model is mounted at `/mnt/model` via `lib-9pfs` + `lib-virtio-9p`; the
9pfs `-virtfs` argument is set by `scripts/llama_env_matrix.py` for the
`qemu+unikraft+cpu` row.

## Stage 3 — Catalog wrap-up

- Kraftfiles stay single-purpose; no shell, no second app.
- `results/llama/upstream_cpu_latest.json` carries the canonical PASS row.
- Re-run `make eval-check current-stage-check` after evidence regeneration.

## Evidence

Authoritative state lives in `results/vogue_latest_evaluation_matrix.json`
and the matching `results/llama/upstream_cpu_latest.json`. Throughput numbers
must always be quoted from a same-run PASS row, never inferred.

## Porting boundary

- CPU appliance only; Vulkan/GPU work belongs to `app-llama-upstream-vk`.
- No synthetic GGUF, ggml, or llama runtime layers in local libs.
- Single-purpose entrypoint: bench-only OR server-only per Kraftfile.
- No native Unikraft `fork()`/`exec()` supervision; ELF Loader remains
  discovery-only.

## Unikraft build system

- `Config.uk` declares `CONFIG_APP_LLAMA_UPSTREAM` and the bench/server mode
  switches; selects required `LIBUK*` / `LIB*` dependencies.
- `Makefile.uk` registers the app and compiles upstream `libllama.a` /
  `libggml*.a` artifacts produced by `make llama-upstream-cmake`.
- `exportsyms.uk` exports only `main`.

## Verification

```sh
make llama-env-check
make llama-upstream-cpu-check
make eval-check
```

## Claim boundaries

Allowed: bench throughput, or server boot/readiness, **when**
`results/vogue_latest_evaluation_matrix.json` records same-run PASS with the
matching evidence row.

Forbidden: GPU acceleration claims (those belong to `app-llama-upstream-vk`),
upstream-source modifications, or treating a `blocked:*` row as a pass.
