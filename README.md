# VOGUE: VirtIO-GPU on Unikraft

VOGUE keeps a small first-party surface around Unikraft VirtIO-GPU, Venus, and
the upstream llama.cpp appliances. The repository now exposes one concise
testing and evidence structure:

- `tests/` holds the deterministic fake-backend native suite.
- `libs/*/tests/test_*.c` holds library-local Unikraft `uktest` suites.
- `scripts/` keeps only the canonical JSON generators and the runtime helper
  collectors they depend on.
- `results/` stores the canonical report artifacts and a small set of retained runtime evidence files.

`blocked:*` remains the explicit partial-progress state. Blocked artifacts are
never passing evidence.

## Canonical Test Surface

Run everything from the repository root:

```sh
make native-tests
make test-core
make test-compat
make test-venus
make test-dispatch
make proto-abi
make vulkan-tests
```

Retained deterministic native binaries:

- `virtio_gpu_core_test`
- `virtgpu_drm_compat_test`
- `venus_encoder_core_test`
- `venus_ring_core_test`
- `virgl_encoder_core_test`
- `vulkan_dispatch_core_test`

The Unikraft-side tests follow the official `uktest` structure from the
Unikraft “Writing Tests” guide: one suite per `libs/*/tests/test_*.c`, suite
registration with `uk_testsuite_register(...)`, and `Config.uk` / `Makefile.uk`
wiring under `LIB..._TEST` with `LIBUKTEST_ALL` support.

## Canonical Automation Surface

Make targets are the stable public interface. Their implementation uses the
single `python3 -m scripts.vogue` CLI with `capture`, `probe`, `evaluate`, and
`report` command groups. Shared artifact and process behavior lives under
`scripts/vogue/core/`; command implementations are internal.

Canonical JSON artifacts:

- `results/vogue_evaluation_matrix.json`
- `results/vulkan/vulkan_perf.json`
- `results/venus/venus_perf.json`
- `results/llama/server_vk_check.json`
- `results/llama/server_vk_throughput.json`
- `results/stage/current_stage_report.json`

## Minimal Make Surface

Everyday aggregate targets:

```sh
make test-fast
make test-native
make test-qemu
make test-gpu
make verify
```

Canonical evidence/report targets:

```sh
make venus-check
make vulkan-check
make llm-server-vk-check
make llm-server-vk-throughput-check
make eval-check
make current-stage-check
```

Build/run helpers kept for the retained runtime collectors:

```sh
make kmscube-build
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

## Verification

The reduced cleanup pass is validated with:

```sh
make -C tests clean
make -C tests native
python3 -m scripts.vogue evaluate vulkan --repetitions 1 --allow-blocked
python3 -m scripts.vogue evaluate venus --repetitions 1 --allow-blocked
python3 -m scripts.vogue evaluate server-vk
python3 -m scripts.vogue evaluate server-vk-throughput
python3 -m scripts.vogue evaluate matrix --check
python3 -m scripts.vogue report stage --check
git diff --check
```
