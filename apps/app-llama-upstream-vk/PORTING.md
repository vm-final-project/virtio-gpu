# app-llama-upstream-vk — llama.cpp Vulkan / Venus / SPIR-V port

Follows `unikraft-porting.md`. The Vulkan appliance is a single-purpose image
that boots straight into either upstream `llama-bench` or the native
server-mode entrypoint with `GGML_USE_VULKAN=1`. It does not start a shell or a
`fork()`/`exec()` launcher. All Vulkan calls are routed through:

```
upstream ggml-vulkan.cpp + generated SPIR-V (*.comp.cpp)
  -> libs/libukggml_vk (static dispatch + loader)
  -> libs/libukvk_icd (ICD bootstrap, Venus capset probe)
  -> libs/libukvenus (Venus SUBMIT_3D / ring / wire format)
  -> libs/libukvirtgpu_drm -> libs/libukvirtio_gpu
  -> QEMU virtio-gpu-gl-pci,blob=true,venus=true
```

## Upstream provenance

Current stage: the Vulkan bench and server rows pass on the evaluation host.
After enabling batched Venus submission in `libukggml_vk` and wiring explicit
llama.cpp batch controls into the appliance, `llm.bench.vk` now reports
`pp512=2232.1`, `tg128=160.2` on the latest same-run artifact, with three
post-change runs under `results/llama/post_opt_runs/` showing a `tg128` median
of `139.9` on a Tesla V100. `llm.server.vk` reaches model-loaded readiness with
`prompt_cache=true`, `batch_size=2048`, `ubatch_size=512`, and
`dispatch_batch_enabled=true`, and now **serves HTTP**: the appliance carries an
in-guest TCP/IP stack (`virtio-net → libuknetdev → lwIP`, DHCP) and `server.cpp`
hands control to the upstream `llama_server()` listener
(`--host 0.0.0.0 --port 8080 --no-mmap`). A same-run host probe records
`/health → 200`, `/v1/models → 200`, and one bounded `/completion → 200`
(`results/llama/upstream_server_vk.json`; `make llm-server-vk-check` →
`http=pass`). Throughput is **measured** by `make llm-server-vk-throughput-check`
(`results/llama/server_vk_throughput.json`: requests/s, tokens/s, TTFT) as a
bounded same-run burst; this file still forbids peak-capacity or
cross-host/cross-model throughput claims.

- Repository: <https://github.com/ggml-org/llama.cpp> (same as `app-llama-upstream`).
- Vulkan backend file: `ggml/src/ggml-vulkan/ggml-vulkan.cpp`.
- SPIR-V shaders: built once on the host with the upstream Vulkan toolchain;
  the resulting `*.comp.cpp` blobs are pulled in unchanged.
- Vulkan loader/dispatcher (`uk_vulkan_dispatch.c`, `uk_ggml_vk_loader.cpp`,
  `uk_stdcxx_compat.cpp`) is the only project-local glue.
- License: MIT.

## Stage 0 — Baselines

```sh
make llama-env-list                 # contains baremetal+vulkan, qemu+linux+vulkan, qemu+unikraft+vulkan
make llama-upstream-cmake-vk        # build upstream libllama.a + libggml-vulkan.a with the Unikraft toolchain
make llama-vulkan-api-coverage      # confirm libukggml_vk matches upstream ggml-vulkan API
```

## Stage 1 — Discovery only

Skip unless native porting hits an unknown syscall, filesystem, or socket gap.
If needed, use Unikraft ELF Loader with a PIE Linux `llama-server` binary as an
x86_64 discovery prototype only. The pinned native ICD/Venus path keeps
upstream sources unmodified and remains the release path.

## Stage 2 — Native Vulkan single-application images

Two single-purpose Kraftfiles back this app, mirroring the CPU appliance:

| Mode | Kraftfile | Kconfig | Row |
|---|---|---|---|
| Bench | `kraft/Kraftfile.llama-upstream-vk` | `CONFIG_APP_LLAMA_UPSTREAM_VK_MODE_BENCH=y` | `llm.bench.vk` |
| Server | `kraft/Kraftfile.llama-upstream-vk-server` | `CONFIG_APP_LLAMA_UPSTREAM_VK_MODE_SERVER=y` | `llm.server.vk` |

Only the selected mode's source file is compiled; `common.h` carries the
shared 9pfs mount and the Venus dispatch initialiser. The server mode follows
the in-process pattern: `server.cpp`'s `main()` calls the upstream
`llama_server(argc, argv)` directly (no second binary, no fork/exec), with the
TCP/IP stack supplied by lwIP (`CONFIG_LIBLWIP` + `virtio-net`/`libuknetdev`)
and `/dev/urandom` by `ukrandom` devfs.

```sh
make llama-upstream-vk-build               # bench image (default)
make llama-upstream-vk-server-build        # server-mode image
make llama-upstream-vk-run                 # may stay blocked on hosts without an EGL render node
make llama-upstream-vk-check
```

Kraftfiles: `kraft/Kraftfile.llama-upstream-vk{,-server}`. Single entrypoint; no shell.

External libraries: `lib-musl`, `lib-libcxx`, `lib-libcxxabi`, `lib-libunwind`,
`lib-compiler-rt`, `lib-pthread-embedded`.

Local libraries (in dependency order): `libukvirtio_gpu`, `libukvirtgpu_drm`, `libukvk_icd`, `libukvenus`,
`libukggml_vk`.

## Stage 2b host requirement

QEMU must expose a working `virtio-gpu-gl-pci,blob=true,venus=true` device
backed by a host Vulkan ICD. Hosts without an EGL render node should expect
`blocked:no-egl-render-node`; that is documented, not a pass.

## Stage 3 — Catalog wrap-up

- `results/llama/upstream_vk.json` carries the canonical row.
- Re-run `make llama-check llama-vulkan-check eval-check current-stage-check`
  after evidence regeneration.

## Evidence

Authoritative state lives in `results/vogue_evaluation_matrix.json`
and the matching `results/llama/upstream_vk.json`,
`results/llama/upstream_server_vk.json`, and
`results/llama/env10_real.json`.

## Porting boundary

- Vulkan/Venus appliance only; CPU work belongs to `app-llama-upstream`.
- Vulkan calls go through `libukggml_vk` → `libukvk_icd` → `libukvenus`;
  no host Vulkan loader inside the guest.
- Single-purpose Vulkan bench OR server entrypoint, selected at Kconfig time.
- No native Unikraft `fork()`/`exec()` supervision; ELF Loader remains
  discovery-only.

## Unikraft build system

- `Config.uk` declares `CONFIG_APP_LLAMA_UPSTREAM_VK` and selects the
  Vulkan stack libraries (`libukggml_vk`, `libukvk_icd`, `libukvenus`).
- `Makefile.uk` compiles upstream `ggml-vulkan.cpp` plus the generated
  SPIR-V `*.comp.cpp` blobs from `build-unikraft-vk/`.
- `exportsyms.uk` exports only `main`.

## Verification

```sh
make llama-vulkan-api-coverage
make llama-ggml-vk-dispatch
make llama-upstream-vk-check
```

## Claim boundaries

Allowed: GPU/Vulkan acceleration claims **only** when
`results/vogue_evaluation_matrix.json` records a same-run PASS row for
`llm.bench.vk` or `llm.bench.vk.real`.

Forbidden: treating a successful build, a static dispatch coverage PASS, or a
blocked runtime row as throughput evidence.
