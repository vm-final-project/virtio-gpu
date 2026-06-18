# VOGUE: VirtIO-GPU on Unikraft

VOGUE is a research artifact for running **VirtIO-GPU, Venus, Vulkan, and
llama.cpp** as single-application [Unikraft](https://unikraft.org) unikernels.
It targets both `x86_64` and `arm64` QEMU.

- **CPU llama appliances** build and run anywhere (incl. Apple Silicon `arm64`).
- **GPU/Vulkan llama appliances** build for either arch but need a Linux host
  with a Venus-capable Vulkan stack to run (see [§7](#7-host-setup-x86_64-venus-stack)).

`make` is the stable automation interface — run `make help` for the full list.

---

## Status at a glance

All four `x86_64` llama appliances are runtime-verified (`status: pass`,
gemma-3-1b Q4_K_M):

| Appliance | Command | Result |
|-----------|---------|--------|
| CPU bench | `make llama-cpu-bench-run` | `pass` — pp512 31.2 / tg128 10.9 tok/s |
| Vulkan bench | `make llama-vk-bench-run` | `pass` — pp512 4582.6 / tg128 353.8 tok/s |
| CPU server | `make llama-cpu-server-run` | `pass` — `/health` 200, `/v1/chat/completions` 200 |
| Vulkan server | `make llama-vk-server-run` | `pass` — `/health` 200, `/v1/chat/completions` 200 |

CPU runs need only QEMU/KVM. **Vulkan runs additionally need the host Venus stack
in [§7](#7-host-setup-x86_64-venus-stack)** — do not hand-roll their QEMU command.

---

## Project structure

| Path | What it holds |
|------|---------------|
| `apps/` | One directory per appliance. Each boots directly into a single entrypoint (no shell, no `fork`/`exec`). |
| `libs/` | First-party Unikraft libraries — the reusable VirtIO-GPU / Venus / Vulkan substrate. |
| `kraft/` | `Kraftfile.*` per appliance/target: Unikraft core, libraries, KConfig, and QEMU targets. |
| `mk/` | Make includes: `llama.mk` (appliances), `tests.mk`, `check.mk`. |
| `scripts/` | Python runners (`app-llama-cpu.py`, `app-llama-vk.py`, `deps.py`, `app-vulkan-sample.py`) that drive QEMU and emit JSON results. |
| `tests/` | Host-native C test suite (fake VirtIO-GPU backend, no QEMU/GPU needed) — the fast CI gate. |
| `config/` | Tracked reference `.config` snapshots for static evidence gates. |
| `results/` | JSON result captures (one schema, see [§8](#8-results)). |
| `rootfs/` | Optional model/shader staging for the llama appliances. |
| `.deps/src/` | Pinned non-Kraft upstreams fetched by `make deps` (not vendored in git). |

### Appliances (`apps/`)

| App | Kraftfile(s) | Stack |
|-----|--------------|-------|
| `app-llama-cpu` | `Kraftfile.llama-cpu{,-server}` | upstream llama.cpp on the CPU backend |
| `app-llama-vk` | `Kraftfile.llama-vk{,-server}` | llama.cpp → ggml-vulkan → Vulkan/Venus |
| `app-kmscube` | `Kraftfile.kmscube-vgpu-gl` | virgl command-stream graphics proof |
| `app-vkmark`, `app-vulkan-sample` | (via `make vulkan-check`) | Venus/Vulkan substrate probes |
| `llama-common/` | — | shared headers for the CPU + Vulkan llama appliances |

### The Vulkan stack (`libs/`)

```text
application / llama.cpp  ->  upstream ggml-vulkan / Vulkan-Hpp
  -> libvulkan            app-facing vk* ABI, dispatch, Hpp loader (compute-first subset)
  -> libukvulkan_venus    native Venus Vulkan driver (encode/decode, ring, context)
  -> libukvirtio_gpu      VirtIO-GPU guest frontend: SUBMIT_3D, blobs, fences
  -> QEMU virtio-gpu-gl + virglrenderer (Venus)  ->  host Vulkan driver
```

`libukvirtgpu_drm` is an optional Linux/Mesa virtgpu-UAPI shim; native builds do
not use it. The CPU llama appliance bypasses this stack entirely (ggml CPU
backend). Each library has its own README with API and design boundaries.

---

## Workflow overview

```text
1. Prerequisites   ->  2. make deps   ->  3. Build   ->  4. Run   ->  5. Talk to the server
                                                          │
                                          CPU: plain make ┤
                                          Vulkan: needs §7 host Venus stack
```

The rest of this document follows that order.

---

## 1. Prerequisites

### 1.1 Host tools (all appliances)

```sh
# macOS
brew install kraftkit qemu python
# Linux: install kraftkit (https://unikraft.org/docs/cli/install), qemu, python3
```

### 1.2 Vulkan **build** tools (only for `llama-vk*` targets)

The Vulkan targets build ggml-vulkan on the host first, which needs two tools on
`PATH`:

```sh
sudo apt-get install cmake          # configures/builds the SPIR-V shader prep step
sudo apt-get install shaderc        # provides glslc, required by ggml-vulkan
```

`glslc` ships in the `shaderc` package; where it is not packaged, install the
[LunarG Vulkan SDK](https://packages.lunarg.com/). The `llama-vk-prepare` target
fails fast with a clear message if either is missing.

### 1.3 Vulkan **runtime** host stack (only to *run* `llama-vk*`)

Running the Vulkan appliances needs a Venus-capable virglrenderer, a QEMU rebuilt
against it, an EGL render node, and KVM. This is a one-time host setup — see
[§7](#7-host-setup-x86_64-venus-stack). CPU appliances need none of it.

---

## 2. Fetch dependencies

```sh
make deps          # fetch Kraft manifest + pinned git upstreams into .deps/src/, run kraft fetch
make deps-status   # show pinned Unikraft rev, library channels, checkout state
make deps-refresh  # refresh git checkouts and re-run kraft fetch (cache-busting)
```

Non-Kraft upstreams (`llama.cpp`, `venus-protocol`, `Vulkan-Headers`,
`SPIRV-Headers`) land in `.deps/src/`. Every external path can be overridden via
the environment variables documented in [`config/deps.json`](config/deps.json).

Get a model (any GGUF works; the docs use gemma-3-1b):

```sh
hf download bartowski/google_gemma-3-1b-it-GGUF \
  --include "google_gemma-3-1b-it-Q4_K_M.gguf" --local-dir models
```

---

## 3. Build

Common variables (override on the `make` line):

```make
ARCH   ?= x86_64                # arm64 for Apple Silicon
MODEL  ?= models/model.gguf     # falls back to the sole *.gguf under models/
```

| Appliance | Build | Run |
|-----------|-------|-----|
| CPU bench | `make llama-cpu-bench-build` | `make llama-cpu-bench-run` |
| CPU server | `make llama-cpu-server-build` | `make llama-cpu-server-run` |
| Vulkan bench | `make llama-vk-bench-build` | `make llama-vk-bench-run` |
| Vulkan server | `make llama-vk-server-build` | `make llama-vk-server-run` |
| KMSCube (virgl) | `make kmscube-build` | — |

The run targets build first if needed, so you can skip the explicit build step.

---

## 4. Run

The run targets boot the appliance under QEMU, capture all output, and write one
JSON result (see [§8](#8-results)). `MODEL` is mounted into the guest over
virtio-9p at `/mnt/model/model.gguf` (local model only — no remote download).

### 4.1 CPU appliances (no special host setup)

```sh
make llama-cpu-bench-run   ARCH=x86_64 MODEL=models/google_gemma-3-1b-it-Q4_K_M.gguf  # bench
make llama-cpu-server-run  ARCH=x86_64 MODEL=models/google_gemma-3-1b-it-Q4_K_M.gguf  # HTTP server
# ARCH=arm64 on Apple Silicon (HVF; falls back to TCG)
```

### 4.2 Vulkan appliances (need the §7 host Venus stack)

`scripts/app-llama-vk.py` (driven by the `llama-vk*-run` targets) assembles the full
QEMU command for you — the `virtio-gpu-gl-pci,venus=true,blob=true` device, the
`egl-headless` display, the model/network devices, and KVM auto-selection.

> **Do not hand-roll the QEMU line for the Vulkan appliances.** Omitting the Venus
> GPU device makes the server fail immediately with
> `uk-llama-upstream-vk-server: FAIL dispatch_init failed` (it cannot open a Vulkan
> device), and a stock `qemu-system-x86_64` reports `old virglrenderer, venus
> unsupported`. Use the runner with the host Venus stack in the environment.

Point these at your Venus-capable virglrenderer prefix and the QEMU you rebuilt
against it ([§7](#7-host-setup-x86_64-venus-stack)), then run:

```sh
export VIRGL_PREFIX=/path/to/venus-virglrenderer-install
export VENUS_QEMU=/path/to/qemu-built-against-it/qemu-system-x86_64

env \
  LD_LIBRARY_PATH="$VIRGL_PREFIX/lib/x86_64-linux-gnu" \
  VIRGL_RENDER_SERVER_EXEC_PATH="$VIRGL_PREFIX/libexec/virgl_render_server" \
  MESA_LOADER_DRIVER_OVERRIDE=kms_swrast LIBGL_ALWAYS_SOFTWARE=1 \
  VOGUE_EGL_RENDERNODE=/dev/dri/card0 \
  python3 scripts/app-llama-vk.py --arch x86_64 --mode server \
    --model models/google_gemma-3-1b-it-Q4_K_M.gguf \
    --qemu "$VENUS_QEMU" --timeout 280
# --mode bench for the bench appliance
# result -> results/llama/llama_server_vk.json  (or llama_vk.json for bench)
```

Equivalently via Make (it calls the same runner) — pass the same env plus the
rebuilt QEMU:

```sh
env LD_LIBRARY_PATH="$VIRGL_PREFIX/lib/x86_64-linux-gnu" \
    VIRGL_RENDER_SERVER_EXEC_PATH="$VIRGL_PREFIX/libexec/virgl_render_server" \
    MESA_LOADER_DRIVER_OVERRIDE=kms_swrast LIBGL_ALWAYS_SOFTWARE=1 \
    VOGUE_EGL_RENDERNODE=/dev/dri/card0 \
  make llama-vk-server-run QEMU="$VENUS_QEMU" \
    MODEL=models/google_gemma-3-1b-it-Q4_K_M.gguf
```

`/dev/dri/card0` and `/dev/kvm` must be readable/writable by your user
(`sudo chmod 666 /dev/dri/card0 /dev/kvm`, or join the `render`/`kvm` groups).

### 4.3 Release gate

```sh
make verify MODEL=/abs/path/model.gguf
```

Runs `test-fast` → `venus-check` → `vulkan-check` → all four llama modes.
Missing QEMU, images, models, or GPU capabilities are
reported as structured `blocked:<reason>` JSON, **not** hard failures (and a
`blocked:*` row is never counted as passing evidence).

---

## 5. Run directly with QEMU

The `make ...-run` targets hide the boot log and print only pass/fail. To watch
the boot log, keep the server up, or run without the Python runner, invoke QEMU
yourself. First build the appliance (`make llama-…-build`), then stage the model
— it mounts over **virtio-9p** at `/mnt/model/model.gguf`:

```sh
mkdir -p /tmp/my-model
cp models/google_gemma-3-1b-it-Q4_K_M.gguf /tmp/my-model/model.gguf
```

The forwarded port `18080` is what you curl in [§6](#6-talk-to-the-server). Exit
QEMU with **Ctrl-A X**. (The exact command of the last successful runner run is
also saved under the `"command"` key of the matching `results/llama/*.json`.)

### 5.1 CPU server — arm64 (Apple Silicon, HVF)

```sh
qemu-system-aarch64 \
  -machine virt,accel=hvf -cpu host -m 4096 -nographic -no-reboot \
  -kernel .unikraft/build/vogue-llama-cpu-server_qemu-arm64 \
  -fsdev local,id=model,path=/tmp/my-model,security_model=none \
  -device virtio-9p-pci,fsdev=model,mount_tag=model \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:18080-10.0.2.15:8080 \
  -device virtio-net-pci,netdev=net0 \
  -append "console=ttyAMA0"
```

### 5.2 CPU server — x86_64 (Linux/KVM)

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm -cpu host -m 4G -nographic -no-reboot \
  -kernel .unikraft/build/vogue-llama-cpu-server_qemu-x86_64 \
  -fsdev local,id=model,path=/tmp/my-model,security_model=none \
  -device virtio-9p-pci,fsdev=model,mount_tag=model \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:18080-10.0.2.15:8080 \
  -device virtio-net-pci,netdev=net0 \
  -append "console=ttyS0"
```

`-m 4G` is plenty for the 1B model (paging makes the whole `-m` usable). Without
KVM use `-machine accel=tcg -cpu max` (much slower). Swap the kernel to
`vogue-llama-cpu_qemu-x86_64` for the **bench** appliance (no `-netdev`/`-net`
needed).

### 5.3 Vulkan server — x86_64

The Vulkan appliance additionally needs the Venus GPU device, an `egl-headless`
GL display, and the host Venus stack from [§7](#7-host-setup-x86_64-venus-stack)
in the environment. A missing GPU device is exactly the `dispatch_init failed`
case; a stock QEMU reports `old virglrenderer, venus unsupported`. The verified
invocation (`export` `$VIRGL_PREFIX` / `$VENUS_QEMU` as in §4.2; build them per
[§7](#7-host-setup-x86_64-venus-stack)):

```sh
env \
  LD_LIBRARY_PATH="$VIRGL_PREFIX/lib/x86_64-linux-gnu" \
  VIRGL_RENDER_SERVER_EXEC_PATH="$VIRGL_PREFIX/libexec/virgl_render_server" \
  MESA_LOADER_DRIVER_OVERRIDE=kms_swrast LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  "$VENUS_QEMU" \
    -machine q35,accel=kvm -cpu host -m 8G -no-reboot \
    -kernel .unikraft/build/vogue-llama-vk-server_qemu-x86_64 \
    -display egl-headless,gl=on,rendernode=/dev/dri/card0 -vga none \
    -device virtio-gpu-gl-pci,hostmem=2G,blob=true,venus=true \
    -append "console=ttyS0" -serial mon:stdio -monitor none \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:18080-10.0.2.15:8080 \
    -device virtio-net-pci,netdev=net0 \
    -fsdev local,id=model,path=/tmp/my-model,security_model=none \
    -device virtio-9p-pci,fsdev=model,mount_tag=model
```

Differences vs. the CPU line: the `virtio-gpu-gl-pci,venus=true,blob=true`
device, `-display egl-headless,gl=on,rendernode=…` (not `-nographic`, which is
`-display none` and conflicts with the GL device), the rebuilt `$VENUS_QEMU`, and
the Venus/EGL environment variables. Use the `vogue-llama-vk_qemu-x86_64` kernel
for the **bench** appliance.

---

## 6. Talk to the server

The server prints `uk-llama-upstream[-vk]-server: READY ...` once the model is
loaded and the HTTP server is listening on the forwarded port (`18080` above).
From a second terminal:

```sh
curl http://127.0.0.1:18080/health                       # {"status":"ok"}

curl http://127.0.0.1:18080/completion \
  -H "Content-Type: application/json" \
  -d '{"prompt": "hello!", "n_predict": 128}'

curl http://127.0.0.1:18080/v1/chat/completions \        # OpenAI-compatible
  -H "Content-Type: application/json" \
  -d '{"messages": [{"role": "user", "content": "Hello"}], "stream": true}'
```

---

## 7. Host setup: x86_64 Venus stack

Reproducing the Vulkan runs needs a host environment the default
`qemu-system-x86_64` on a stock box does **not** provide.

1. **Venus-capable virglrenderer + QEMU rebuilt against it.** Stock distro
   virglrenderer (0.9.1) is too old for Venus/blob, and QEMU gates Venus/blob at
   *compile* time on the virglrenderer version it was configured against. Build a
   recent virglrenderer (`meson -Dvenus=true -Dplatforms=egl`; pass the repo
   `Vulkan-Headers/include` so the bundled venus protocol finds the `vk_video`
   std headers), install it to `$VIRGL_PREFIX`, then re-`meson setup
   --reconfigure` and rebuild QEMU against it
   (`PKG_CONFIG_PATH=$VIRGL_PREFIX/lib/.../pkgconfig`, meson ≥ 1.5). Run QEMU with
   `LD_LIBRARY_PATH=$VIRGL_PREFIX/lib/...` and
   `VIRGL_RENDER_SERVER_EXEC_PATH=$VIRGL_PREFIX/libexec/virgl_render_server`.
2. **EGL render node.** `egl-headless` needs a DRM render node. On a box with no
   GPU render node, point it at a primary KMS node driven by software:
   `VOGUE_EGL_RENDERNODE=/dev/dri/card0` (honoured by `scripts/app-llama-vk.py`) with
   `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast LIBGL_ALWAYS_SOFTWARE=1`.
3. **KVM, not TCG.** The model is built with AVX-512 (`-march=native`); QEMU TCG
   raises `#UD` on some AVX-512 ops during C++ static init. The run script
   auto-selects KVM when `/dev/kvm` is writable.

### Guest-side fixes already in-tree for this path

- **`CONFIG_LIBUKPAGING`** on all four llama Kraftfiles — maps all RAM into one
  contiguous heap. Without it the boot allocator hands out fragmented physical
  regions / PCI-hole addresses and model/compute-buffer allocation fails or
  crashes (see [§9](#9-background-paging--heap)).
- **Modern virtio-pci transport patch**
  (`patches/unikraft/0001-virtio-pci-modern-device-support.patch`, registered in
  `config/deps.json`) including `virtio_pci_shm_region_get` and 1:1 MMIO BAR
  mapping under paging (high 64-bit BARs are otherwise unmapped once native
  paging replaces the static boot direct-map).
- **Page-consistent `minMemoryMapAlignment`** in the Venus dispatch so
  model-weight buffers are sized to fit aligned tensor placement.
- **`--no-host` / `mparams.no_host`** in the server entry and the shared loader so
  weights stay in device-local `Vulkan0` memory. The default pinned `Vulkan_Host`
  buffer (used for `token_embd.weight`) is a host-visible VirtIO-GPU blob whose
  upload does not complete on a software host Vulkan driver over Venus, which
  previously deadlocked model load just before `READY`.

With the above, `make llama-vk-server-run` reaches `uk-llama-upstream-vk-server:
READY`, serves `/health` (200) and `/v1/chat/completions` (200) over the Venus
GPU path, and `scripts/app-llama-vk.py` records `status: pass`.

See [`docs/VENUS-BRINGUP.md`](docs/VENUS-BRINGUP.md) for the runtime probes and
the `make venus-check` targets.

---

## 8. Results

Every runner writes one JSON file (`_arm64.json` suffix for arm64 runs) under
`results/`, all sharing this schema:

```json
{
  "status": "pass | fail | blocked:<reason>",
  "generated_utc": "...",
  "command": "...",
  "inputs": {},
  "metrics": { "pp512": 0.0, "tg128": 0.0, "boot_time_s": 0.0,
               "peak_rss_kb": 0, "image_bytes": 0, "model_load_ms": 0.0 },
  "error": null
}
```

Bench metrics carry, alongside `pp512`/`tg128` throughput:

- `boot_time_s` — for **bench**, wall-clock seconds from QEMU launch to the
  appliance's `config` line on the serial console (`scripts/common.py:run_timed`),
  printed after boot + 9p mount (VK also after Venus dispatch init) just before
  llama-bench, so it is pure unikernel startup. For **servers**, the
  launch→first-successful-`/health` time (boot + model load + HTTP listen).
- `model_load_ms` — weight-load time, where the appliance loads via
  `load_model_common` (CPU bench/server, VK server); VK bench runs upstream
  llama-bench, which bundles the load, so it has no separate line.
- `peak_rss_kb` — peak host RSS (KiB) of the QEMU process (`getrusage`): the
  appliance's host-memory footprint.
- `image_bytes` — the unikernel image size (e.g. CPU ≈3.5 MB, VK ≈35 MB).
- `query` / `completion` (servers only) — the user message POSTed to
  `/v1/chat/completions` (default `hi, what's your name`; override with
  `QUERY=...` or `--query`) and the model's generated reply, recorded verbatim so
  each server result carries a real exchange alongside `tokens_per_s` /
  `requests_per_s` / `latency_s` and the `/health` + `/v1/chat/completions`
  `http_status` codes. The chat endpoint applies the model's chat template;
  hitting raw `/completion` with an instruction-tuned model yields degenerate
  output (e.g. repeated special tokens), so the templated turn is used instead.

Canonical files include `results/llama/llama_{cpu,server_cpu,vk,server_vk}.json`
and `results/venus/*.json`. Runtime logs,
frame dumps, and QMP files are local and gitignored.

---

## 9. Background: paging & heap

The CPU server is runtime-verified on `x86_64` (QEMU/KVM, q35): it loads the
Gemma-3-1B Q4_K_M model and serves chat completions. All four llama Kraftfiles
set `CONFIG_LIBUKPAGING=y`. Without paging, ukboot hands the raw physical FREE
regions to the buddy allocator, so the largest single allocation is bounded by
the largest *contiguous* region. On `x86_64`/q35 the 32-bit PCI MMIO hole
fragments low RAM and the static boot page table maps only the first 4 GiB, so a
~762 MiB model buffer (and the bench's ~516 MiB compute buffer) fail with
`ggml_aligned_malloc: insufficient memory` regardless of `-m` (2 G…24 G all fail).
Enabling paging maps all RAM (incl. >4 GiB) into one contiguous heap. `arm64`/virt
is unaffected because its RAM is a single contiguous block.

---

## 10. Testing

```sh
make test-fast        # host-native C suite + VirtIO-GPU wire-ABI check (primary CI gate)
```

The native suite needs no QEMU/GPU/model. Every test guards a distinct module
and is wired into a gate (none are duplicate or unused):

| Test | Guards | Gate |
|------|--------|------|
| `core_test` | libukvirtio_gpu core (fake backend) | `test-core` |
| `proto_abi_test` | VirtIO-GPU wire-ABI structs/features | `proto-abi` |
| `virgl_encoder_test` | libukvirtio_gpu virgl encoder | `test-core` |
| `venus_encoder_test` | libukvulkan_venus encoders | `test-venus` |
| `venus_ring_test` | libukvulkan_venus ring transport | `test-venus` |
| `dispatch_test` | libvulkan dispatch | `test-dispatch` |
| `drm_compat_test` | libukvirtgpu_drm (optional DRM shim) | `test-compat` |

The venus/dispatch tests compile the generated Venus tree, which needs the
repo-pinned Vulkan-Headers (`VK_HEADER_VERSION 352`); `tests/Makefile` defaults
`VK_INC` to `.deps/src/Vulkan-Headers/include` (from `make deps`) so the suite
builds without a separate `venus-protocol` checkout. See
[`tests/README.md`](tests/README.md) for the per-group breakdown.

---

## Platform notes

**Apple Silicon / arm64.** The CPU appliance on `qemu/arm64` is a first-class
path: `ARCH=arm64` selects `qemu-system-aarch64`, the `virt` machine, and
`ttyAMA0`; the runner prefers HVF and falls back to TCG. The Vulkan targets build
on arm64 but are not runtime-verified there (the Venus host stack is Linux/KVM).

**Toolchain caveats (macOS + GCC 16).** Unikraft 0.21.0 is validated against GCC
11–14, so the Makefile prepends Homebrew GNU make to `PATH` (KraftKit needs GNU
make ≥ 4.1; macOS ships 3.81), injects `-std=gnu17` / `-std=gnu++17 -fpermissive`
to tame the GCC-16 C23 default, and reapplies a tracked `extern "C"` patch to
unikraft's `ectx.h` via `make deps` (see `patches/unikraft/`).
