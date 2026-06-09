# VOGUE: VirtIO-GPU on Unikraft

VOGUE is a small Unikraft VirtIO-GPU, Venus, Vulkan, and llama.cpp research
artifact. `Makefile` is the only stable automation interface. Python scripts
only launch QEMU, parse output, and write JSON.

## Build And Test

```sh
make help
make test-fast
make test-native
make venus-check
make vulkan-check
make verify MODEL=/path/to/model.gguf QEMU=/path/to/qemu-system-x86_64
```

Runtime settings are explicit Make variables:

```make
MODEL ?= models/model.gguf
QEMU ?= qemu-system-x86_64
RUN_TIMEOUT ?= 120
```

Appliance targets:

```sh
make llama-cpu-build
make llama-cpu-run
make llama-cpu-server-build
make llama-cpu-server-run
make llama-vk-build
make llama-vk-run
make llama-vk-server-build
make llama-vk-server-run
make linux-guest-vk-baseline
```

`make verify` runs deterministic tests, QEMU probes, Vulkan checks, all four
llama runtime modes, and the Linux guest baseline. Missing external hardware,
QEMU, images, or models produce `blocked:<reason>` JSON. Program and test
failures remain non-zero exits.

## Results

Canonical tracked results:

- `results/venus/qemu_2d_probe.json`
- `results/venus/qemu_venus-ring_probe.json`
- `results/vulkan/vulkan_perf.json`
- `results/llama/llama_cpu.json`
- `results/llama/llama_server_cpu.json`
- `results/llama/llama_vk.json`
- `results/llama/llama_server_vk.json`
- `results/llama/vulkan_qemu_linux_baseline.json`

Each file uses the same small schema:

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
