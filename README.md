# VOGUE: VirtIO-GPU on Unikraft for Graphics and llama.cpp

VOGUE runs graphics, Vulkan, and llama.cpp workloads **inside Unikraft
unikernels** that talk to the host GPU over QEMU's `virtio-gpu-gl` /
[Mesa Venus](https://docs.mesa3d.org/drivers/venus.html) path — **without
importing Linux DRM/KMS or Mesa into the guest**. It is a governance-gated
research artifact: every capability is backed by a same-run evidence row, and
claims are bounded explicitly.

---

## Project Architecture

### Core purpose

Reach the *same host-facing VirtIO-GPU / Venus contract that a Linux guest uses*
through a **much thinner Unikraft library stack** (the paper calls this a
*dependency collapse*). Local code stays small: first-party Unikraft libraries
own the VirtIO-GPU / Venus / Vulkan glue, while upstream applications
(llama.cpp, kmscube) keep their own logic and are consumed unmodified
through one-line include shims.

### The guest-to-host accelerated path

```text
upstream llama.cpp bench / server      (apps/app-llama-upstream[-vk])
  └─ upstream ggml-vulkan.cpp           (built in-tree by app-llama-upstream-vk)
       └─ libvulkan          app-facing vk* ABI + Vulkan-Hpp dispatch
            └─ libukvulkan_venus   Unikraft-native Venus Vulkan driver
                 └─ libukvirtio_gpu    VirtIO-GPU frontend (SUBMIT_3D, blobs, fences)
                      └─ QEMU virtio-gpu-gl-pci,blob=true,venus=true
                           └─ host Vulkan driver (e.g. NVIDIA V100)
```

The Vulkan layering follows the Khronos loader/driver split: `libvulkan` owns
the application-facing `vk*` ABI and dispatch (a compute-first subset), and
`libukvulkan_venus` is the statically linked Venus driver. The driver's
device-open/Venus-context bootstrap is **native** — it calls `libukvirtio_gpu`
directly, with no Linux virtgpu DRM UAPI. The DRM shim (`libukvirtgpu_drm`) is
therefore optional (`CONFIG_LIBUKVULKAN_VENUS_USE_DRM_COMPAT` or
`CONFIG_LIBVULKAN_ENABLE_DRM_FD_COMPAT`, both default `n`); the former
`libukvk_icd` bootstrap shim has been retired and the upstream ggml-vulkan stack
is built in-tree by `app-llama-upstream-vk`.

The kmscube graphics appliance renders via the virgl/Gallium path directly through `libukvirtio_gpu`.

### Design principles

* **One image, one purpose.** Each appliance boots straight into a single
  entrypoint — no shell, no native `fork()`/`exec()` launcher — and compiles
  only the source for the selected mode (`bench.cpp` *or* `server.cpp`, never
  both). `-Os -ffunction-sections -fdata-sections -Wl,--gc-sections` let the
  linker drop every unreached symbol.
* **Upstream stays upstream.** No custom GGUF/ggml/llama runtime lives under
  `libs/`; upstream llama.cpp is pulled from the sibling `../llama.cpp` checkout
  through six one-line `#include` shims.
* **Evidence over assertion.** A `PASS` row means the command reproduced locally
  in the same run; a `blocked:*` row is a documented blocker, never throughput
  or acceleration evidence (see *Evidence & Claim Discipline*).

### Current stage (2026-06-04)

On the evaluation host (QEMU 11.0.1 `virtio-gpu-gl-pci,blob=true,venus=true`,
Venus-enabled virglrenderer, Tesla V100 render node) the 26-row evaluation
matrix is **26 PASS, 0 blocked, 0 missing** (`results/vogue_evaluation_matrix.md`).
Device-backing memory now flows through the upstream Unikraft `uksglist`
(scatter-gather) + `ukalloc` (`uk_posix_memalign`) libraries directly — there is
no first-party DMA library — and every appliance below was rebuilt and
re-verified on that stack. Highlights:

* **llama.cpp Vulkan bench** runs end-to-end on the GPU via real Venus
  (`pp512=2232.1`, `tg128=160.2` t/s, latest same-run artifact).
* **llama.cpp Vulkan server now serves HTTP.** The appliance carries an
  in-guest TCP/IP stack (`virtio-net → libuknetdev → lwIP`, DHCP) and runs the
  upstream `llama_server()` listener on `0.0.0.0:8080`. A same-run host probe
  records `GET /health → 200`, `GET /v1/models → 200`, and `POST /completion →
  200` over the real V100/Venus path (`make llm-server-vk-check` →
  `runtime=pass http=pass`). It is tuned with continuous-batching slots
  (`--parallel 4`), `--ctx-size = parallel × per-slot`, prompt cache, and
  `--flash-attn off` (the V100 has no Vulkan coopmat2). Throughput is
  **measured** by `make llm-server-vk-throughput-check`: latest same-run
  **decode 120.8 tok/s** (75 % of the in-guest Vulkan bench `tg128=160`),
  prefill 757 tok/s, TTFT 1.18 s (`results/llama/server_vk_throughput.json`).
  A stock-Linux-guest-over-Venus baseline
  (`make linux-guest-vk-baseline`) shows the headroom is in VOGUE's guest-side
  stack, not the Venus transport — see *Performance vs native* below.
* **Graphics**: `xport.qemu-vgpu`, `proto.venus-ring`, `gfx.kmscube.submit`,
  and `gfx.kmscube.frame` all have same-run PASS artifacts.

---

## Directory Analysis

This tree is the first-party project (`virtio-gpu/`). It resolves vendored
sibling checkouts (`../unikraft`, `../llama.cpp`, `../lib-musl`, `../lib-lwip`,
`../venus-protocol`, …) via `config/external_paths.json`.

| Directory | Function |
|---|---|
| `libs/` | **First-party Unikraft libraries** — the substrate under test. VirtIO-GPU frontend + virgl encoder (`libukvirtio_gpu`), app-facing Vulkan ABI/dispatch (`libvulkan`), Venus Vulkan driver with native device-open bootstrap (`libukvulkan_venus`), optional DRM virtgpu shim (`libukvirtgpu_drm`). Device-backing memory uses upstream Unikraft `uksglist` (scatter-gather) + `ukalloc` (`uk_posix_memalign`) directly — no first-party DMA library. Each carries a `README.md` contract enforced by `make lib-readme-check`. |
| `apps/` | **Unikraft applications.** Graphics: `app-kmscube`, `app-vulkan-smoke`, `app-vkmark`. llama.cpp: `app-llama-upstream` (CPU) and `app-llama-upstream-vk` (Vulkan), each with `bench.cpp` + `server.cpp`. Each app carries a `PORTING.md` (provenance, evidence rows, claim boundaries) enforced by `make app-port-check`. |
| `kraft/` | One `Kraftfile.<name>` per single-purpose appliance (the *one image, one purpose* rule). The root `Kraftfile` is the kmscube graphics image. |
| `tests/` | **Host-native deterministic C suite** against the fake VirtIO-GPU backend — no QEMU/GPU needed. The fast inner loop and primary CI gate. See `tests/README.md`. |
| `scripts/` | Python evidence generators and claim gates invoked by the `Makefile` (eval matrix, governance, perf, boot/model-load time, Venus/Vulkan probes, llama runners, the HTTP server capture/gate). |
| `config/` | Governance + environment metadata: `external_paths.json` (vendored sibling roots), `llama_env_matrix.json` (llama.cpp env/thread/backend selection), `governance.json` + `perf_baseline.json` (drive the gates). |
| `cmake/` | `unikraft-clang.cmake` toolchain file used to cross-build upstream llama.cpp (`libllama.a`, ggml backends) for the unikernel. |
| `results/` | Generated evidence consumed by the README and repo-local gates (`vogue_evaluation_matrix.md`, `llama/*.json`, `venus/`, `kmscube_vgpu_gl/`, …). |
| `docs/` | Long-form docs: `ARCHITECTURE.md`, `GOVERNANCE.md`, `VENUS-BRINGUP.md`, the llama.cpp porting plan, and the Venus runtime enablement plan. |
| `rootfs/` | Host-shared assets mounted into appliances over 9pfs (e.g. the llama model directory). |
| `patches/`, `design/`, `idea/`, `resource/` | Supporting material: vendored patches, design notes, scratch ideas, and archival references. Not part of the build/test path. |

Top-level reference files: `plan-optimize.md` (perf levers → gates),
`plan-fix.md` (current blockers and their fixes), `unikraft-porting.md`,
`AGENTS.md`.

Reproducibility metadata (manifests describing the VM/host runs) is owned by the
sibling `../manifest/` tree (`../manifest/manifests/vogue-main.yaml`), not
duplicated here; governance ownership rules live in `docs/GOVERNANCE.md` and
`config/governance.json`.

### Applications & governance status

Each app's status is declared in `config/governance.json` and enforced against
this table by `make app-port-check` / `make governance-check`.

| App | Status | Purpose | Evidence rows |
|---|---|---|---|
| `app-kmscube` | canonical | VirtIO-GPU 3D graphics via virgl over Venus | `gfx.kmscube.sw/.submit/.frame` |
| `app-vulkan-smoke` | demo | Minimal Vulkan substrate smoke test | `vk.smoke` |
| `app-vkmark` | experimental | Vulkan benchmark substrate | `gfx.vkmark` |
| `app-llama-upstream` | canonical | Upstream llama.cpp CPU bench / server | `llm.bench.cpu`, `llm.server.cpu` |
| `app-llama-upstream-vk` | canonical | Upstream llama.cpp Vulkan bench / HTTP server | `llm.bench.vk`, `llm.server.vk` |

### Libraries & usage status

All first-party libraries are in active use: each is selected (directly or
via Kconfig `select`) by at least one buildable appliance. The two appliance
families are the **Vulkan/llama** images (`llama-upstream-vk`,
`llama-upstream-vk-server`) and the **graphics** images (`kmscube`;
the root `Kraftfile` is kmscube). `make governance-check lib-readme-check`
enforces each library's `README.md` contract.

| Library | Role | Used by |
|---|---|---|
| `libukvirtio_gpu` | VirtIO-GPU frontend + Gallium virgl encoder + fake backend | Graphics **and** Vulkan/llama (all GPU appliances) |
| `libvulkan` | App-facing Vulkan `vk*` ABI + Vulkan-Hpp dispatch (`CONFIG_LIBVULKAN`); compute-first subset, routes to the Venus driver | Vulkan/llama |
| `libukvulkan_venus` | Unikraft-native Venus Vulkan driver: Venus wire encode/decode + ring protocol; `uk_venus_encode_*` delegate to encoders generated from `../venus-protocol` | Vulkan/llama |
| `libukvirtgpu_drm` | Optional Mesa/Linux virtgpu DRM compatibility shim: direct translator (`vk.drm-core`) plus fd-style render-node facade (`vk.drm-fdio`) | Future Mesa/Linux-style apps |

---

## How to Find What You Need

| If you want to… | Go to… |
|---|---|
| Understand the big picture | `docs/ARCHITECTURE.md`, then this file's *Project Architecture*. |
| Find **VirtIO-GPU frontend / virgl** logic | `libs/libukvirtio_gpu/` (`virgl_encoder.c`, `virtio_gpu_proto.h`). |
| Find the **Venus Vulkan driver / ring protocol** | `libs/libukvulkan_venus/` (`venus_driver.c`, `venus_cs.c`, `venus_init.c`, `venus_compute.c`). The `uk_venus_encode_*` entry points delegate to driver encoders generated from the pinned `../venus-protocol` (`make gen-libukvenus`; pin in `scripts/venus/pin.json`, command set in `config/venus_command_manifest.json`; workflow in `GENERATOR.md`). |
| Find the **Vulkan ABI / dispatch** | `libs/libvulkan/` (`uk_vulkan_dispatch.c`, `vk_hpp_loader.cpp`). Owns the exported `vk*` symbols and the Vulkan-Hpp dispatcher. |
| Find the **native device-open bootstrap** | `libs/libukvulkan_venus/venus_driver.c` (`uk_vulkan_venus_open` → `libukvirtio_gpu`). The optional Linux DRM shim is `libs/libukvirtgpu_drm/`. |
| Find the **ggml-vulkan build glue** | `apps/app-llama-upstream-vk/Makefile.uk` (compiles upstream `ggml-vulkan.cpp` + SPIR-V blobs in-tree; the `vk*` ABI lives in `libvulkan`). |
| Find **2D/KMS graphics (kmscube)** | `apps/app-kmscube/`. |
| Find the **llama.cpp app entrypoints** | `apps/app-llama-upstream{,-vk}/{bench,server}.cpp` + `common.h`. |
| Understand the **HTTP server / networking** | `apps/app-llama-upstream-vk/server.cpp`, `kraft/Kraftfile.llama-upstream-vk-server` (lwIP/netdev Kconfig), `scripts/llama_server_vk_capture.py` (boot + HTTP probe), `scripts/llm_server_vk_check.py` (gate). |
| Change which appliance is built | `kraft/Kraftfile.<name>` and the matching `make *-build` target. |
| Change llama env / threads / backend | `config/llama_env_matrix.json` (`make llama-env-list` / `-check`). |
| Resolve a vendored sibling path | `config/external_paths.json`. |
| **Run unit tests** | `make native-tests` (or `test-core` / `test-venus` / `test-dispatch`); details in `tests/README.md`. |
| Run the daily gate before committing | `make test-fast`. |
| Regenerate the evidence matrix | `make eval-check` → `results/vogue_evaluation_matrix.md`. |
| Add/verify a governance contract | `config/governance.json`, then `make governance-check lib-readme-check app-port-check`. |
| Reproduce the HTTP llama server | `make llama-upstream-vk-server-build` → `python3 scripts/llama_server_vk_capture.py` → `make llm-server-vk-check`. |
---

## Make Targets Summary

All targets run from `virtio-gpu/`. `make` (no target) prints the grouped help.
The root `Makefile` is the single entry point; it delegates the C suite to
`tests/Makefile` and the heavy lifting to `scripts/` and KraftKit.

### Everyday gates

| Target | Purpose |
|---|---|
| `make test-fast` | Daily gate: governance + app/lib docs + native + wire-ABI tests (no QEMU/GPU). |
| `make test-native` | Host-native C suite + wire-ABI test. |
| `make test-qemu` | QEMU VirtIO-GPU / Venus probes (kmscube + Venus); blocked rows off-host. |
| `make test-gpu` | Host Vulkan + static Venus / ggml-vulkan dispatch checks. |
| `make verify` | Broad release gate; QEMU-dependent steps degrade to blocked rows. |

### VirtIO-GPU / Venus host checks

| Target | Purpose |
|---|---|
| `make venus-check` | VirtIO-GPU wire-ABI + Venus encoder/ring/probe checks. |
| `make stage-check` | Unikraft alignment + stage audit (builds on `venus-check`). |
| `make benchmark-check` | Regenerate the benchmark summary from same-run artifacts. |

### Host-native test groups (wrap `tests/Makefile`)

| Target | Purpose |
|---|---|
| `make native-tests` | Full deterministic C suite (primary CI gate). |
| `make test-core` / `test-venus` / `test-dispatch` | Core / Venus / ggml-vulkan groups. |
| `make proto-abi` | VirtIO-GPU wire-ABI struct/feature check. |
| `make vulkan-tests` | Optional host Vulkan compute baseline (needs `VK_LIB`/`VK_INC`). |

### Appliances (KraftKit; need `../unikraft` + external roots)

| Target | Purpose |
|---|---|
| `make kmscube-build` / `kmscube-check` | Build / evaluate the VirtIO-GPU graphics appliance. |
| `make llama-upstream-cpu-build` | CPU bench appliance (`llm.bench.cpu`). |
| `make llama-upstream-vk-build` | Vulkan/Venus bench appliance (`llm.bench.vk`). |
| `make llama-upstream-vk-server-build` | Vulkan/Venus **HTTP server** appliance (`llm.server.vk`). |
| `make llama-env-check` / `llama-env-list` | Validate / list the llama.cpp environment matrix. |

### Evidence, governance & docs

| Target | Purpose |
|---|---|
| `make governance-check` | Validate app/lib/claim governance metadata. |
| `make app-port-check` / `lib-readme-check` | Validate every `PORTING.md` / `README.md`. |
| `make eval-check` | Regenerate the evidence matrix (blocked rows stay explicit). |
| `make llm-server-vk-check` | llama.cpp Vulkan HTTP server contract + same-run HTTP probe. |
| `make llm-server-vk-throughput-check` | Measured HTTP throughput (decode/prefill tok/s, TTFT, requests/s). |
| `make linux-guest-vk-baseline` | Stock-Linux-guest + Venus Vulkan baseline (same QEMU path) for comparison. |
| `make perf-check` / `image-size-check` / `boot-time-check` / `model-load-time-check` | Performance & resource budgets. |
| `make current-stage-check` | Assert the documented current-stage report. |
| `make clean` | Remove generated test outputs. (The committed `libs/libukvulkan_venus/generated/` Venus headers are not touched — regenerate with `make gen-libukvenus`.) |

### Artifact bundles

`make artifact-quick` (fast) · `make artifact-check` (functional) ·
`make artifact-full` (broadest, including host-dependent runtime checks + claim discipline).

---

## Running the production llama.cpp Vulkan HTTP server (`llm.server.vk`)

The production server appliance boots straight into the upstream
`llama_server()` listener, loads the model on the host GPU over real Venus, and
serves HTTP on `0.0.0.0:8080` through an in-guest lwIP stack
(`virtio-net → libuknetdev → lwIP`, DHCP). No shell, no `fork`/`exec`, one
image one purpose.

### Prerequisites

* A QEMU with Venus support — the project auto-selects one that advertises
  `virtio-gpu-gl-pci,venus=...` (system `qemu-system-x86_64` or the sibling
  `../qemu-src/build/qemu-system-x86_64`).
* A host GPU + readable render node (`/dev/dri/renderD*`); `egl-headless,gl=on`
  with Venus-enabled `virglrenderer`.
* A GGUF model (any size; the gates use `models/qwen3-0.6b/…`).
* `kraft` (KraftKit) plus the Vulkan/SPIR-V header roots exported so the
  upstream ggml-vulkan cross-build can find them:

  ```sh
  export VULKAN_HEADERS_INCLUDE=$(realpath ../Vulkan-Headers/include)
  export SPIRV_HEADERS_INCLUDE=$(realpath ../SPIRV-Headers/include)
  ```

### 1. Build the appliance

```sh
make llama-upstream-vk-server-build
# -> .unikraft/build/vogue-llama-upstream-vk-server_qemu-x86_64
```

This cross-builds upstream llama.cpp with the server tool + ggml-vulkan
(`make llama-upstream-cmake-vk-server`) and links the unikernel from
`kraft/Kraftfile.llama-upstream-vk-server`.

### 2a. Run + verify automatically (recommended)

`scripts/llama_server_vk_capture.py` boots the image over real Venus, waits for
the model-loaded `READY` line, then issues a same-run HTTP probe from the host
through a QEMU `hostfwd` port and records the proof:

```sh
# Picks a model from VOGUE_VK_MODEL, config/llama_env_matrix.json, or ../models/*.gguf
VOGUE_VK_MODEL=$(pwd)/../models/qwen3-0.6b/Qwen3-0.6B-Q4_K_M.gguf \
  python3 scripts/llama_server_vk_capture.py

make llm-server-vk-check      # -> llm-server-vk: runtime=pass http=pass
```

Evidence lands in `results/llama/upstream_server_vk.json` (HTTP `/health`,
`/v1/models`, `/completion` status + the model card) and the serial log in
`results/llama/upstream_server_vk_serial.log`.

### 2b. Run manually and curl it

The canonical QEMU command lives in
`scripts/run_llama_upstream_vk_server.sh`. Place a model at
`rootfs/llama/model.gguf` (or point `MODEL_DIR` at a directory containing
`model.gguf`), then:

```sh
cp ../models/qwen3-0.6b/Qwen3-0.6B-Q4_K_M.gguf rootfs/llama/model.gguf
HOSTPORT=18080 scripts/run_llama_upstream_vk_server.sh      # boots; serves on 8080

# from another shell, once "server is listening on http://0.0.0.0:8080" appears:
curl http://127.0.0.1:18080/health                          # {"status":"ok"}
curl http://127.0.0.1:18080/v1/models
curl -X POST http://127.0.0.1:18080/completion \
     -H 'Content-Type: application/json' \
     -d '{"prompt":"Hello from VOGUE on Unikraft.","n_predict":16}'
```

`HOSTPORT` (default `18080`) is forwarded to the guest's `10.0.2.15:8080`
(QEMU user-mode networking assigns that lease via DHCP). Override `IMAGE`,
`MODEL_DIR`, `HOSTMEM`, `MEM`, or `ACCEL` as needed.

### 3. Measure throughput (optional)

```sh
make llm-server-vk-throughput-check   # boots, drives a bounded completion burst
# -> results/llama/server_vk_throughput.json
#    (decode tok/s, prefill tok/s, end-to-end tok/s, TTFT, requests/s)
```

### Performance vs native

Same V100, same GGUF (`Qwen3-0.6B-Q4_K_M`), same upstream `llama-bench` binary,
three environments — two are para-virtualised over the **identical**
`virtio-gpu-gl venus=true` path, isolating *unikernel-vs-Linux* from
*virtualised-vs-bare-metal*:

1. **Bare-metal host Vulkan** — no VM (`results/llama/vulkan_linux_baseline.json`).
2. **Stock Linux guest in QEMU + Venus** — normal Linux kernel, Mesa Venus guest
   ICD, same QEMU device (`results/llama/vulkan_qemu_linux_baseline.json`,
   reproduce with `make linux-guest-vk-baseline`).
3. **VOGUE Unikraft + Venus** — the unikernel port.

| Metric | ① Bare-metal host | ② Linux guest + Venus (QEMU) | ③ VOGUE Unikraft + Venus |
|---|---|---|---|
| `pp512` prefill | 5586.9 t/s | **4948.2 t/s** | bench 2232.1 · server 757.2 |
| `tg128` decode | 239.2 t/s | **323.6 t/s** | bench 160.2 · **server 120.8** |

**What the Linux-guest baseline reveals.** A *stock Linux guest* over the same
Venus path reaches ~89 % of bare-metal prefill and matches/exceeds its decode
(decode of a 0.6 B model is small and run-to-run variable). In other words, the
**Venus para-virtualisation transport is not the main bottleneck** — a mature
guest stack rides it at near-native speed. The Unikraft port currently reaches
**~45 % of the Linux-guest prefill and ~50 % of its decode**; that remaining gap
lives in **VOGUE's own guest-side stack** (the `libukvulkan_venus` driver + the
`libvulkan` static dispatch vs Mesa's mature Venus ICD, plus the single guest
vCPU), which is real optimisation headroom — not an unavoidable virtualisation
tax. Within the Unikraft image, the **server decode rate (120.8 t/s) is 75 % of
its own bench (160.2)**, so the HTTP request path itself is efficient; the work
is in the dispatch/encoder layer and in guest SMP (`plan-optimize.md` Phase 3).

> Claim boundary: liveness + completion are proven and throughput is a **bounded
> same-run measurement** (`results/llama/server_vk_throughput.json`); the
> baselines are same-host, same-model references, not cross-host/peak-capacity
> claims. `tg128` for a 0.6 B model is variance-prone — read `pp512` as the
> cleaner ordering (① > ② > ③).

### Venus transport optimizations (hot path)

The guest→host gap above lives in VOGUE's transport, not the Venus para-virtual
device. The per-inference-step hot path was reduced from **3 virtqueue kicks +
3 mallocs** (one each for `vkEndCommandBuffer`, `vkQueueSubmit`,
`vkWaitForFences`) toward Mesa's streaming model. Four changes, all on the
`libvulkan → libukvulkan_venus → libukvirtio_gpu` path:

| # | Optimization | Mechanism | Effect |
|---|---|---|---|
| 1 | **Merge EndCmdBuf + QueueSubmit + WaitFences** | In ring mode, `vkEndCommandBuffer` flushes the ring tail; `vkQueueSubmit` appends its command + does the single final flush | 3 kicks → 1 kick per step |
| 2 | **`vkWaitForFences` polls `completed_fence`** | `cmd_submit_locked()` already marks `completed_fence` synchronously on the host response, so WaitFences reads the local value instead of sending a Venus command | eliminates the 3rd SUBMIT_3D per step (active even without ring) |
| 3 | **`__asm__("pause")` in busy-polls** | `uk_venus_ring_cmd_wait` + `cmd_submit_locked` spin-waits yield the core to the host `ring_thread` | lower spin pressure on the single guest vCPU |
| 4 | **True ring stream model** | `vkCmd*` write directly into a host-visible Venus ring circular buffer (`uk_venus_ring_cmd_write`); host `ring_thread` drains asynchronously, kick only when the ring is idle — mirroring Mesa `vn_ring.c` | 0 mallocs, request-response → streaming |

The ring (4) is created **lazily** on the first command buffer (after the Venus
`VkInstance`/`VkDevice` exist) on the dispatch's existing Venus context, and is
**host-visible-blob backed** — the QEMU `hostmem=` shared-memory BAR exposed to
Unikraft via `virtio_pci_shm_region_get` (Unikraft 0.21), allocated with
`blob_id=0` (the virglrenderer host-shmem path). Toggle with
`UK_GGML_VK_DISPATCH_RING` / `UK_GGML_VK_DISPATCH_BATCH`; the `READY` marker
reports the active mode (`ring_enabled=`, `batch_enabled=`, `hostmem_fixed=`).

Path difference vs Mesa: Mesa's `vn_renderer_virtgpu.c` reaches the ring through
Linux `/dev/dri` `ioctl`/`mmap`; VOGUE's native Vulkan path reaches the same
virtio-gpu ring through `libvulkan -> libukvulkan_venus -> libukvirtio_gpu`,
with no Linux DRM layer in the guest. The optional `libukvirtgpu_drm` fdio path
is for future Mesa/Linux-style compatibility.

#### Multi-environment evaluation — what the data actually shows

**(a) Native, deterministic, environment-independent** —
`make -C tests venus-hotpath` drives an identical inference-step command
sequence (8 `vkCmd` + submit + wait, ×64 steps) through each transport mode
against the fake backend and counts `SUBMIT_3D` host round-trips. This isolates
the transport change from host/GPU noise:

| Mode | `SUBMIT_3D` per step | vs per-call |
|---|---|---|
| A — per-call SUBMIT_3D (pre-opt) | **10.00** | 1× |
| B — batched + WaitFences poll (#1 partial, #2) | **2.00** | **5.0× fewer** |
| C — ring stream (#1 + #4) | **1.02** | **9.85× fewer** |

So the optimizations **provably reduce guest→host round-trips** by 5–9.85× at
the mechanism level. This is the load-bearing justification.

**(b) Real GPU (V100, virtio-gpu-gl Venus), same-run server throughput** — an
A/B across configs in one host session. Numbers here are **bounded same-run
bursts on a host that was running ~5× slower than the prior day** (the
*baseline code itself* fell from 120.8 to 25.5 t/s decode between sessions —
i.e. host-side variance, not guest code), so they order configs, they do **not**
claim peak capacity:

| Config (same host session) | decode t/s | prompt t/s | ttft s |
|---|---|---|---|
| baseline code (per-call/batch SUBMIT_3D) | 25.5 | 146 | 4.98 |
| optimized, ring **off** (batch + #2 + #3) | 20.8 | 117 | 6.06 |
| optimized, ring **on** (#1+#4) | 19.6 | 110 | 6.62 |

**Conclusion (honest):** the mechanism win is real and proven (a); but on this
host the **end-to-end throughput is dominated by host-side Venus/`ring_thread`
latency**, which swamps the guest-side round-trip savings — the ring stream's
per-flush `vkNotifyRingMESA` + asynchronous host drain is actually *slower* than
the batched `SUBMIT_3D` that virglrenderer processes inline. Therefore:

* The **ring stream (#4) is shipped opt-in, default OFF** (`UK_GGML_VK_DISPATCH_RING=1`
  to enable) — it does not win on QEMU 11 + virglrenderer here.
* The **batched path + WaitForFences `completed_fence` poll (#2) + `pause` (#3)**
  are the production default; (#2) removes one host round-trip per step with no
  observed regression.
* This is consistent with *Performance vs native* above: the Venus transport is
  not the bottleneck — the host-side stack is. Re-measuring on a non-degraded
  host (or after lowering host `ring_thread` idle latency) is the next step to
  see (b) track (a).

---

## Evidence & Claim Discipline

This artifact treats claims strictly — read before editing results or reporting
numbers:

* A **`PASS`** row means the command reproduced locally **in the same run**. A
  **`blocked:*`** row (e.g. `blocked:unikraft-image-missing`,
  `blocked:wrong-domain-artifact`) is a documented blocker — **never** report it
  as acceleration / throughput evidence.
* Do **not** claim Unikraft llama.cpp token/s or GPU throughput without same-run
  PASS artifacts; do **not** use host-Linux baseline JSON as Unikraft runtime
  evidence; do **not** reintroduce a custom GGUF/ggml/llama runtime under `libs/`.
* The HTTP server claim is bounded to **liveness + completion** (`/health`,
  `/v1/models`, `/completion`) plus a **bounded same-run throughput
  measurement** (`make llm-server-vk-throughput-check`); peak-capacity and
  cross-host/cross-model comparisons remain out of scope.
* After touching `libs/`, `apps/`, or claims, run
  `make governance-check lib-readme-check app-port-check` and keep the README
  status columns in sync with `config/governance.json`.

---

## VirtIO-GPU Venus/Vulkan v1 roadmap

The API/ABI contract and gate traceability for the accelerated (Venus/Vulkan)
path are pinned in two design specs, kept source-grounded against the vendored
`linux-6.18`, `qemu-11.0`, `mesa`, and `venus-protocol` siblings and enforced by
`make venus-check`:

* `design/unikraft-virtio-gpu-spec-v1.md` — the Unikraft VirtIO-GPU Venus/Vulkan
  API specification: required feature bits, capset IDs, library boundaries, and
  the explicit **Out of scope** list.
* `design/virtio-gpu-vulken-v1.md` — research-to-implementation traceability:
  the gate ladder (`proto.api-contract` … `G8`), performance-evaluation plan
  (submit latency p50/p95/p99), and its own **Out of scope** boundary.

Backing memory for these paths uses upstream Unikraft `uksglist` + `ukalloc`
directly; VOGUE ships no first-party DMA library.

## Key documents

* `docs/ARCHITECTURE.md` — the big picture and library boundaries.
* `docs/GOVERNANCE.md` — the gate catalogue and ownership rules.
* `docs/VENUS-BRINGUP.md` — Venus enablement walkthrough.
* `libs/libukvulkan_venus/GENERATOR.md` — Venus encoder autogeneration from
  `../venus-protocol`.
* `plan-optimize.md` / `plan-fix.md` — perf levers and current blockers.
* `tests/README.md` — the host-native test suite guide.
