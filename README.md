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
(llama.cpp, kmscube, glmark2) keep their own logic and are consumed unmodified
through one-line include shims.

### The guest-to-host accelerated path

```text
upstream llama.cpp bench / server      (apps/app-llama-upstream[-vk])
  └─ libukggml_vk      static Vulkan/Venus dispatch for ggml-vulkan
       └─ libukvenus        guest-side Venus encoder + ring protocol
            └─ libukvirtgpu_drm   DRM virtgpu shim (Linux ABI)
                 └─ libukvirtio_gpu    VirtIO-GPU frontend
                      └─ QEMU virtio-gpu-gl-pci,blob=true,venus=true
                           └─ host Vulkan driver (e.g. NVIDIA V100)
```

The graphics appliances (kmscube, glmark2) use the lower half of the same stack
through the Linux-ABI shims (`libukdrm_compat`, `libukgbm_compat`, `libukegl`).

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

### Current stage (2026-06-02)

On the evaluation host (QEMU 11.0.1 `virtio-gpu-gl-pci,blob=true,venus=true`,
Venus-enabled virglrenderer, Tesla V100 render node) the 27-row evaluation
matrix is **27/27 PASS, 0 blocked** (`results/vogue_evaluation_matrix.md`).
Highlights:

* **llama.cpp Vulkan bench** runs end-to-end on the GPU via real Venus
  (`pp512=2232.1`, `tg128=160.2` t/s, latest same-run artifact).
* **llama.cpp Vulkan server now serves HTTP.** The appliance carries an
  in-guest TCP/IP stack (`virtio-net → libuknetdev → lwIP`, DHCP) and runs the
  upstream `llama_server()` listener on `0.0.0.0:8080`. A same-run host probe
  records `GET /health → 200`, `GET /v1/models → 200`, and `POST /completion →
  200` over the real V100/Venus path (`make llm-server-vk-check` →
  `runtime=pass http=pass`). Throughput is **measured** by
  `make llm-server-vk-throughput-check` (latest same-run burst: **2.61 req/s,
  83.51 tokens/s, TTFT 0.34 s** on the V100); only bounded same-run numbers are
  claimed, not peak capacity.
* **Graphics**: `xport.qemu-vgpu`, `proto.venus-ring`, `gfx.kmscube.submit`,
  and `gfx.kmscube.frame` all have same-run PASS artifacts.

---

## Directory Analysis

This tree is the first-party project (`virtio-gpu/`). It resolves vendored
sibling checkouts (`../unikraft`, `../llama.cpp`, `../lib-musl`, `../lib-lwip`,
`../venus-protocol`, …) via `config/external_paths.json`.

| Directory | Function |
|---|---|
| `libs/` | **First-party Unikraft libraries** — the substrate under test. DMA & buffer pool (`libukdma`, `libukdma_pool`), VirtIO-GPU frontend + virgl encoder (`libukvirtio_gpu`), DRM virtgpu shim (`libukvirtgpu_drm`), Venus encoder/ring (`libukvenus`), Vulkan ICD (`libukvk_icd`), static ggml-vulkan dispatch (`libukggml_vk`), Linux-ABI shims (`libukdrm_compat`, `libukgbm_compat`, `libukegl`), software renderer (`libukswrender`). Each carries a `README.md` contract enforced by `make lib-readme-check`. |
| `apps/` | **Unikraft applications.** Graphics: `app-kmscube`, `app-glmark2`, `app-vulkan-smoke`, `app-vkmark`. llama.cpp: `app-llama-upstream` (CPU) and `app-llama-upstream-vk` (Vulkan), each with `bench.cpp` + `server.cpp`. Each app carries a `PORTING.md` (provenance, evidence rows, claim boundaries) enforced by `make app-port-check`. |
| `kraft/` | One `Kraftfile.<name>` per single-purpose appliance (the *one image, one purpose* rule). The root `Kraftfile` is the kmscube graphics image. |
| `tests/` | **Host-native deterministic C suite** against the fake VirtIO-GPU backend — no QEMU/GPU needed. The fast inner loop and primary CI gate. See `tests/README.md`. |
| `scripts/` | Python evidence generators and claim gates invoked by the `Makefile` (eval matrix, governance, perf, boot/model-load time, Venus/Vulkan probes, llama runners, the HTTP server capture/gate). |
| `config/` | Governance + environment metadata: `external_paths.json` (vendored sibling roots), `llama_env_matrix.json` (llama.cpp env/thread/backend selection), `governance.json` + `perf_baseline.json` (drive the gates). |
| `cmake/` | `unikraft-clang.cmake` toolchain file used to cross-build upstream llama.cpp (`libllama.a`, ggml backends) for the unikernel. |
| `results/` | Generated evidence consumed by the README, paper, and gates (`vogue_evaluation_matrix.md`, `llama/*.json`, `venus/`, `kmscube_vgpu_gl/`, …). |
| `docs/` | Long-form docs: `ARCHITECTURE.md`, `GOVERNANCE.md`, `VENUS-BRINGUP.md`, the llama.cpp porting plan, and the Venus runtime enablement plan. |
| `paper/` | Typst paper + generated tables (`make paper`). |
| `rootfs/` | Host-shared assets mounted into appliances over 9pfs (e.g. the llama model directory). |
| `patches/`, `design/`, `idea/`, `resource/`, `slides/` | Supporting material: vendored patches, design notes, scratch ideas, figures, and presentation assets. Not part of the build/test path. |

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
| `app-glmark2` | benchmark | OpenGL software-substrate benchmark | `gfx.glmark2.sw` |
| `app-vulkan-smoke` | demo | Minimal Vulkan substrate smoke test | `vk.smoke` |
| `app-vkmark` | experimental | Vulkan benchmark substrate | `gfx.vkmark` |
| `app-llama-upstream` | canonical | Upstream llama.cpp CPU bench / server | `llm.bench.cpu`, `llm.server.cpu` |
| `app-llama-upstream-vk` | canonical | Upstream llama.cpp Vulkan bench / HTTP server | `llm.bench.vk`, `llm.server.vk` |

---

## How to Find What You Need

| If you want to… | Go to… |
|---|---|
| Understand the big picture | `docs/ARCHITECTURE.md`, then this file's *Project Architecture*. |
| Find **VirtIO-GPU frontend / virgl** logic | `libs/libukvirtio_gpu/` (`virgl_encoder.c`, `virtio_gpu_proto.h`). |
| Find **Venus encoder / ring protocol** | `libs/libukvenus/` (`venus_cs.c`, `venus_init.c`, `venus_compute.c`; generator in `GENERATOR.md`). |
| Find the **Vulkan ICD / DRM virtgpu shim** | `libs/libukvk_icd/` and `libs/libukvirtgpu_drm/`. |
| Find **3D rendering / GPU dispatch for ggml** | `libs/libukggml_vk/` (`uk_vulkan_dispatch.c`). |
| Find **2D/KMS graphics (kmscube)** | `apps/app-kmscube/` + shims `libs/libukdrm_compat/`, `libs/libukgbm_compat/`. |
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
| Build the paper | `make paper` (PDF) / `make paper-check` (consistency). |

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
| `make llm-server-vk-throughput-check` | Measured HTTP throughput (requests/s, tokens/s, TTFT). |
| `make perf-check` / `image-size-check` / `boot-time-check` / `model-load-time-check` | Performance & resource budgets. |
| `make current-stage-check` | Assert the documented current-stage report. |
| `make paper` / `paper-check` | Build / consistency-check the Typst paper. |
| `make clean` | Remove generated test/paper/generator outputs. |

### Artifact bundles

`make artifact-quick` (fast) · `make artifact-check` (functional) ·
`make artifact-full` (broadest, including paper + claim discipline).

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
# -> results/llama/server_vk_throughput.json  (requests/s, tokens/s, TTFT)
```

> Claim boundary: liveness + completion are proven; throughput is a **bounded
> same-run measurement** (`results/llama/server_vk_throughput.json`), not a
> peak-capacity or cross-host/cross-model claim.

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

## Key documents

* `docs/ARCHITECTURE.md` — the big picture and library boundaries.
* `docs/GOVERNANCE.md` — the gate catalogue and ownership rules.
* `docs/VENUS-BRINGUP.md` — Venus enablement walkthrough.
* `libs/libukvenus/GENERATOR.md` — Venus encoder autogeneration from
  `../venus-protocol`.
* `plan-optimize.md` / `plan-fix.md` — perf levers and current blockers.
* `tests/README.md` — the host-native test suite guide.
