# VOGUE: VirtIO-GPU on Unikraft

VOGUE is a research artifact for running **VirtIO-GPU, Venus, Vulkan, and
llama.cpp** as single-application [Unikraft](https://unikraft.org) unikernels.
It targets both `x86_64` and `arm64` QEMU. The CPU llama appliances build and
run on Apple Silicon (`ARCH=arm64`); the GPU/Vulkan path builds for either arch
but still needs a Linux host Vulkan stack for runtime proof.

`make` is the stable automation interface — run `make help` for the full list.

## Project structure

| Path | What it holds |
|------|---------------|
| `apps/` | One directory per appliance. Each boots directly into a single entrypoint (no shell, no `fork`/`exec`). |
| `libs/` | First-party Unikraft libraries — the reusable VirtIO-GPU / Venus / Vulkan substrate. |
| `kraft/` | `Kraftfile.*` per appliance/target: Unikraft core, libraries, KConfig, and QEMU targets. |
| `mk/` | Make includes: `llama.mk` (appliances), `tests.mk`, `evidence.mk`. |
| `scripts/` | Python runners (`llama_cpu.py`, `llama_vk.py`, `deps.py`, probes) that drive QEMU and emit JSON results. |
| `tests/` | Host-native C test suite (fake VirtIO-GPU backend, no QEMU/GPU needed) — the fast CI gate. |
| `config/` | Tracked reference `.config` snapshots for static evidence gates. |
| `results/` | JSON result captures (one schema, see [Results](#results)). |
| `rootfs/` | Optional model/shader staging for the llama appliances. |
| `.deps/src/` | Pinned non-Kraft upstreams fetched by `make deps` (not vendored in git). |

### Appliances (`apps/`)

| App | Kraftfile(s) | Stack |
|-----|--------------|-------|
| `app-llama-cpu` | `Kraftfile.llama-cpu{,-bench,-server}` | upstream llama.cpp on the CPU backend |
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

## Prerequisites

```sh
brew install kraftkit qemu python      # macOS host tools
```

Linux GPU/Venus runs additionally need a Vulkan-capable host driver, a
`virglrenderer`/Venus-capable QEMU, and (if not at the default path) a `VK_LIB`
override.

Non-Kraft upstreams (`llama.cpp`, `venus-protocol`, `Vulkan-Headers`,
`SPIRV-Headers`) are fetched into `.deps/src/` by `make deps`. Every external
path can be overridden via environment variables documented in
[`config/deps.json`](config/deps.json).

## Dependency workflow

```sh
make deps          # fetch Kraft manifest + pinned git upstreams into .deps/src/, run kraft fetch
make deps-status   # show pinned Unikraft rev, library channels, checkout state
make deps-refresh  # refresh git checkouts and re-run kraft fetch (cache-busting)
```

Standard Unikraft split per appliance: `Kraftfile` (core/libs/KConfig/targets),
`Config.uk` (app options), `Makefile.uk` (register the app, pick sources,
per-arch flags).

## Build and run

```make
ARCH   ?= x86_64                # arm64 for Apple Silicon
MODEL  ?= models/model.gguf     # falls back to the sole *.gguf under models/
```

| Appliance | Build | Run |
|-----------|-------|-----|
| CPU bench | `make llama-cpu-build` | `make llama-cpu-run` |
| CPU server | `make llama-cpu-server-build` | `make llama-cpu-server-run` |
| Vulkan bench | `make llama-vk-build` | `make llama-vk-run` |
| Vulkan server | `make llama-vk-server-build` | `make llama-vk-server-run` |
| KMSCube (virgl) | `make kmscube-build` | — |

Add `ARCH=arm64` and `MODEL=/abs/path/model.gguf` as needed. Probes/gates:
`make test-fast`, `make venus-check`, `make vulkan-check`.

`make verify MODEL=...` is the broad release gate (`test-fast` → `venus-check` →
`vulkan-check` → all four llama modes → `linux-guest-vk-baseline`). Missing QEMU,
images, models, or GPU capabilities are reported as structured
`blocked:<reason>` JSON, not hard failures.

## Apple Silicon CPU quickstart (validated path)

The CPU appliance on `qemu/arm64` is the path validated in this repo:

```sh
make deps
make llama-cpu-server-build ARCH=arm64
make llama-cpu-server-run   ARCH=arm64 MODEL=/abs/path/model.gguf
```

`ARCH=arm64` selects `qemu-system-aarch64`, the `virt` machine, and `ttyAMA0`.
The runner prefers HVF acceleration and falls back to TCG. The model is mounted
into the guest over VirtIO-9P.

**Toolchain caveats (macOS + GCC 16).** Unikraft 0.21.0 is validated against GCC
11–14, so the Makefile: prepends Homebrew GNU make to `PATH` (KraftKit needs GNU
make ≥ 4.1; macOS ships 3.81); injects `-std=gnu17` / `-std=gnu++17 -fpermissive`
to tame the GCC-16 C23 default; and reapplies a tracked `extern "C"` patch to
unikraft's `ectx.h` via `make deps` (see `patches/unikraft/`). The `x86_64` CPU
path and the Vulkan targets build but are not runtime-verified in this round.

## Running manually

`make ...-run` captures all QEMU output and prints only pass/fail. To watch the
boot log or hit the HTTP API, drive QEMU directly — the model mounts over
**virtio-9p**, which `kraft run` cannot forward.

```sh
# 1. Stage the model where QEMU can share it
mkdir -p /tmp/my-model
cp models/your-model.gguf /tmp/my-model/model.gguf

# 2. Boot (arm64 / Apple Silicon, HVF)
qemu-system-aarch64 \
  -machine virt,accel=hvf -cpu host \
  -m 4096 -nographic -no-reboot \
  -kernel .unikraft/build/vogue-llama-cpu-server_qemu-arm64 \
  -fsdev local,id=model,path=/tmp/my-model,security_model=none \
  -device virtio-9p-pci,fsdev=model,mount_tag=model \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:18080-10.0.2.15:8080 \
  -device virtio-net-pci,netdev=net0 \
  -append "console=ttyAMA0 random.seed=1 2 3 4 5 6 7 8"
```

For **x86_64** (Linux/KVM): use `qemu-system-x86_64`, `-machine accel=kvm -cpu
host`, the `_qemu-x86_64` kernel, and `-append "console=ttyS0"` (drop the 9p/net
device lines or keep as needed). Without KVM use `-machine accel=tcg -cpu max`
(much slower).

The guest prints `uk-llama-upstream-server: READY ...` once the model is loaded
and the server is listening. Test it from a second terminal:

```sh
curl http://127.0.0.1:18080/health                       # {"status":"ok"}

curl http://127.0.0.1:18080/completion \
  -H "Content-Type: application/json" \
  -d '{"prompt": "hello!", "n_predict": 128}'

curl http://127.0.0.1:18080/v1/chat/completions \        # OpenAI-compatible
  -H "Content-Type: application/json" \
  -d '{"messages": [{"role": "user", "content": "Hello"}], "stream": true}'
```

Press **Ctrl-A X** to exit QEMU. The exact command of the last successful run is
recorded under the `"command"` key of `results/llama/llama_server_cpu_arm64.json`.

## Linux GPU/Vulkan workflow

```sh
make llama-vk-server-build ARCH=x86_64
make llama-vk-server-run   ARCH=x86_64 MODEL=/abs/path/model.gguf
```

Runtime proof requires a Linux host with the expected Vulkan stack. The
appliance always reads `/mnt/model/model.gguf` (local model only — no Hugging
Face / remote download at runtime). Export `VK_LIB` if your Vulkan loader is not
at the default path.

## Results

Every runner writes one JSON file (`_arm64.json` suffix for arm64 runs) under
`results/`, all sharing this schema:

```json
{
  "status": "pass | fail | blocked:<reason>",
  "generated_utc": "...",
  "command": "...",
  "inputs": {},
  "metrics": {},
  "error": null
}
```

Canonical files include `results/llama/llama_{cpu,server_cpu,vk,server_vk}.json`,
`results/venus/*.json`, and `results/vulkan/vulkan_perf.json`. Runtime logs,
frame dumps, and QMP files are local and gitignored.

## Testing

`make test-fast` runs the host-native C suite (no QEMU/GPU/model needed) plus the
VirtIO-GPU wire-ABI check — the primary CI gate. See [`tests/README.md`](tests/README.md)
for the per-group breakdown.
