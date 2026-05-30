# Config reference snapshots


## llama.cpp environment matrix

`config/llama_env_matrix.json` is the dependency-free benchmark matrix used by `scripts/llama_env_matrix.py`. It is intentionally JSON so `make llama-env-check` works without PyYAML. It currently contains 9 selectable environment IDs: 7 bench families (`baremetal+cuda`, `baremetal+vulkan`, `baremetal+cpu`, `qemu+linux+vulkan`, `qemu+linux+cpu`, `qemu+unikraft+vulkan`, `qemu+unikraft+cpu`) plus CPU and Vulkan server-only appliance rows (`qemu-unikraft-cpu-server`, `qemu-unikraft-vulkan-server`). The bench dry run expands CPU thread sweeps into 10 rows; the server dry run emits the two direct-entrypoint server rows. `config/bench_env.yaml` remains a human-readable legacy summary for older scripts; new environment selection and thread/argument overrides should be added to `llama_env_matrix.json` first.


This directory contains the tracked reference Unikraft `.config` snapshots used by
static evidence gates. Root-level `.config.*_qemu-x86_64` files are KraftKit build
outputs and are intentionally ignored.

The primary checked snapshot is `config/.config.vogue_qemu-x86_64`, which
`make real-path-check` uses to ensure production builds select the real
`libukvirtio_gpu` backend rather than the fake native-test backend.
