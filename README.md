# VOGUE: VirtIO-GPU on Unikraft

VOGUE is a Unikraft VirtIO-GPU, Venus, Vulkan, and llama.cpp research artifact.
`Makefile` is the stable automation interface. The repo now supports both
`x86_64` and `arm64` QEMU targets; CPU llama appliances can be built and run on
Apple Silicon with `ARCH=arm64`, while the GPU/Venus path still needs a Linux
host Vulkan stack for runtime proof. The Vulkan llama server appliance is
local-model only in this repo: it always reads `/mnt/model/model.gguf` and does
not download models from Hugging Face or a remote URL at runtime.

## Prerequisites

Install the local build tools first:

```sh
brew install kraftkit qemu python
```

Linux GPU/Venus runs additionally need:

- a Vulkan-capable host driver
- `virglrenderer`/Venus-capable QEMU setup
- a valid `VK_LIB` override when the default `/usr/lib/x86_64-linux-gnu/libvulkan.so.1` does not apply

Kraft-managed dependencies come from the official manifest cache. Non-Kraft
upstreams are stored under `.deps/src/` by `make deps`:

- `llama.cpp`
- `venus-protocol`
- `Vulkan-Headers`
- `SPIRV-Headers`

Every external path can still be overridden with environment variables from
[`config/deps.json`](/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/config/deps.json:1).

## Dependency Workflow

Initial setup:

```sh
make deps
make deps-status
```

What each command does:

- `make deps`: source the official Kraft manifest, update the local package
  index, fetch the pinned non-Kraft Git dependencies into `.deps/src/`, and run
  `kraft fetch` for every tracked Kraftfile.
- `make deps-status`: show the pinned Unikraft revision, official library
  channel selections, and the state of each repo-local upstream checkout.
- `make deps-refresh`: refresh Git checkouts and rerun `kraft fetch` with
  cache-busting.

This repo uses the standard Unikraft split:

- `Kraftfile`: choose Unikraft core, official libraries, KConfig and targets.
- `Config.uk`: expose app/library options and architecture-specific dependencies.
- `Makefile.uk`: register the app as a Unikraft library, pick sources, and
  apply per-architecture build flags.
- `kraft fetch`: retrieve Unikraft core and library dependencies declared in the
  Kraftfile.
- `Makefile.uk` fetch/prepare logic: build-time handling for application
  upstream code such as `llama.cpp`.

## Build And Test

Common entrypoints:

```sh
make help
make test-fast
make test-native
make venus-check
make vulkan-check
make verify MODEL=/absolute/path/model.gguf
```

Runtime variables:

```make
ARCH ?= x86_64
QEMU ?= qemu-system-x86_64   # qemu-system-aarch64 when ARCH=arm64
MODEL ?= models/model.gguf   # falls back to the sole *.gguf under models/
RUN_TIMEOUT ?= 120
```

Supported appliance targets:

```sh
# CPU llama
make llama-cpu-build
make llama-cpu-run
make llama-cpu-server-build
make llama-cpu-server-run

# GPU/Vulkan llama
make llama-vk-build
make llama-vk-run
make llama-vk-server-build
make llama-vk-server-run

# KMSCube (VirtIO-GPU / virgl)
make kmscube-build

# Venus / Vulkan probes
make venus-probe-2d
make venus-probe-ring
make venus-check          # test-venus + both probes
make vulkan-check         # vulkan-tests + test-dispatch + vulkan_check.py

# Linux guest baseline
make linux-guest-vk-baseline
```

`make verify` is the broad release gate; it runs:
`test-fast` → `venus-check` → `vulkan-check` → all four llama runtime modes
→ `linux-guest-vk-baseline`.  Missing QEMU, images, models, or host GPU
capabilities are reported as structured `blocked:<reason>` JSON rather than
hard failures.

## Apple Silicon CPU Workflow

On an M1/M2/M3 Mac, the supported path is the CPU appliance on `qemu/arm64`:

```sh
make deps
make test-fast
make llama-cpu-server-build ARCH=arm64
make llama-cpu-server-run ARCH=arm64 MODEL=/absolute/path/model.gguf
```

Related CPU bench command:

```sh
make llama-cpu-run ARCH=arm64 MODEL=/absolute/path/model.gguf
```

Notes:

- `ARCH=arm64` selects `qemu-system-aarch64`, `virt` machine type, and
  `ttyAMA0`.
- On macOS ARM64, the runner prefers QEMU HVF acceleration and falls back to
  TCG automatically.
- CPU images are expected to work on Apple Silicon. GPU/Venus images can be
  built for `arm64`, but runtime verification still depends on a Linux host
  Vulkan/Venus stack and is not claimed on macOS.

### Build & run CPU llama (arm64 / Apple Silicon — validated path)

This is the build-and-run path validated in this repo. It targets **arm64** under `qemu-system-aarch64` with `hvf` acceleration.

```sh
make deps                              # fetch upstreams + reapply tracked patches
make llama-cpu-server-build ARCH=arm64 # or: llama-cpu-build
make llama-cpu-run ARCH=arm64          # boots the appliance under QEMU
```

The run mounts a model into the guest over virtio-9p. `MODEL` defaults to `models/model.gguf`, falling back to the sole `*.gguf` in `models/`. Override explicitly when you keep several models:

```sh
make llama-cpu-run ARCH=arm64 MODEL=models/your-model.gguf
```

**Toolchain caveats (macOS + GCC 16).** Unikraft 0.21.0 is validated against GCC 11-14, but this host uses GCC 16, so the Makefile:

- prepends Homebrew GNU make (`gnubin`) to `PATH` (KraftKit's sub-make needs GNU make >= 4.1; macOS ships 3.81),
- injects `UK_CFLAGS=-std=gnu17` / `UK_CXXFLAGS=-std=gnu++17 -fpermissive` so the GCC-16 C23 default does not break the build,
- reapplies a tracked `extern "C"` patch to unikraft's `ectx.h` via `make deps` (see `patches/unikraft/`).

**Other targets.** The `x86_64` CPU path and the Vulkan targets (`llama-vk`, `llama-vk-server`) are build-designed but not verified in this round; they may need their own toolchain adjustments.

## Running Manually

The `make llama-cpu-server-run` target runs a Python wrapper that silently
captures all QEMU output and only prints the final pass/fail result.  If you
want to **see the boot log in real time** or poke at the running unikernel, you
can drive QEMU or KraftKit directly.

### Direct QEMU (recommended for model-serving workloads)

The unikernel mounts the model file over **virtio-9p**, which `kraft run` cannot
yet forward.  QEMU is therefore the lowest-friction path:

```sh
# 1. Place the model where QEMU can share it
mkdir -p /tmp/my-model
cp models/google_gemma-3-1b-it-Q4_K_M.gguf /tmp/my-model/model.gguf

# 2. Boot the unikernel (arm64 / Apple Silicon with HVF)
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

Once the guest prints `READY`, test the HTTP API from a second terminal:

```sh
curl http://127.0.0.1:18080/health
# {"status":"ok"}
```


You can test the model by sending a request to the `/completion` endpoint:

```sh
curl http://127.0.0.1:18080/completion \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "hello!",
    "n_predict": 128
  }'
```

You can also use the **OpenAI API format**:

```sh
curl http://127.0.0.1:18080/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "messages": [{"role": "user", "content": "Hello"}],
    "stream": true
  }'
```

For **x86_64** (Linux/KVM host) replace the first two lines with:

```sh
qemu-system-x86_64 \
  -machine accel=kvm -cpu host \
  -m 4096 -nographic -no-reboot \
  -kernel .unikraft/build/vogue-llama-cpu-server_qemu-x86_64 \
  -fsdev local,id=model,path=/tmp/my-model,security_model=none \
  -device virtio-9p-pci,fsdev=model,mount_tag=model \
  -append "console=ttyS0"
```

Without KVM, replace `-machine accel=kvm -cpu host` with
`-machine accel=tcg -cpu max` (TCG software emulation, much slower).

> **Tip:** The exact command that was used for the last successful run is
> recorded verbatim in `results/llama/llama_server_cpu_arm64.json` under the
> `"command"` key. Copy it and replace the `/var/folders/…/vogue-model-*` path
> with a directory that contains your `model.gguf`.

You should see the unikernel boot log followed by a line like:

```
uk-llama-upstream-server: READY model=/mnt/model/model.gguf threads=1 slots=1 …
```

Press **Ctrl-A X** to exit QEMU once you are done.

---

### Via KraftKit (`kraft run`)

`kraft run` is convenient for unikernels that do not need extra QEMU devices:

```sh
# Build first (skipped if already built)
make llama-cpu-server-build ARCH=arm64

# Run – note: no virtio-9p device, so the model is *not* mounted
kraft run \
  --target qemu/arm64 \
  --kraftfile "$(pwd)/.kraft-gen/Kraftfile.llama-cpu-server" \
  --memory 4096M \
  -- "console=ttyAMA0"
```

> **Limitation:** `kraft run` does not support arbitrary `-fsdev`/`-device`
> QEMU arguments, so the llama server will fail to open `model.gguf` when
> launched this way.  Use direct QEMU (above) for any model-serving workload.

---

## Linux GPU/Venus Workflow

Build the GPU appliances for either architecture:

```sh
make llama-vk-build ARCH=x86_64
make llama-vk-server-build ARCH=x86_64
```

Run-time proof still requires a Linux host with the expected Vulkan stack:

```sh
make llama-vk-run ARCH=x86_64 MODEL=/absolute/path/model.gguf
make llama-vk-server-run ARCH=x86_64 MODEL=/absolute/path/model.gguf
```

The server runner records readiness plus HTTP behavior in the same JSON result.
It always mounts and reads `/mnt/model/model.gguf` inside the guest. If your
host Vulkan loader is not at the default path, export `VK_LIB` before building
or running.

## Results

Canonical x86_64 result files:

- `results/venus/qemu_2d_probe.json`
- `results/venus/qemu_venus-ring_probe.json`
- `results/vulkan/vulkan_perf.json`
- `results/llama/llama_cpu.json`
- `results/llama/llama_server_cpu.json`
- `results/llama/llama_vk.json`
- `results/llama/llama_server_vk.json`
- `results/llama/vulkan_qemu_linux_baseline.json`

`arm64` runs write the same schema with an `_arm64.json` suffix, for example:

- `results/llama/llama_cpu_arm64.json`
- `results/llama/llama_server_cpu_arm64.json`

Each result file uses the same schema:

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

Runtime logs, frame dumps, and QMP files are local and ignored.
