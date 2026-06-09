# Repository Guidelines

## Project Structure & Module Organization

This repository is VOGUE, a Unikraft VirtIO-GPU/Venus/Vulkan research artifact. Keep first-party work inside this tree; sibling directories such as `../unikraft`, `../llama.cpp`, `../mesa`, and `../venus-protocol` are external inputs referenced by configuration.

- `libs/`: project Unikraft libraries such as `libukvirtio_gpu`, `libvulkan`, `libukvulkan_venus`, and DRM/GBM/EGL shims.
- `apps/`: single-purpose Unikraft appliances, including KMSCube, Vulkan smoke tests, and llama.cpp CPU/Vulkan bench/server images.
- `kraft/`: Kraftfiles for appliance builds.
- `tests/`: host-native deterministic C tests against the fake VirtIO-GPU backend.
- `scripts/`, `config/`, `results/`: evidence generation, governance metadata, and generated artifacts.
- `docs/`, `design/`, `results/`: architecture notes, design specs, and generated evidence consumed by repo-local gates.

## Build, Test, and Development Commands

Run commands from the repository root:

- `make help`: list supported reviewer and test targets.
- `make test-fast`: daily gate for governance, docs, native tests, and protocol checks.
- `make test-native` or `make -C tests native`: run host-native tests without QEMU/GPU/model dependencies.
- `make artifact-quick`: fast artifact gate covering native tests, Vulkan API coverage, docs, and generated evidence checks.
- `make verify`: broad release gate; may require QEMU/Venus/GPU availability.
- `make clean`: remove generated test outputs.

## Coding Style & Naming Conventions

Follow the surrounding C style: tabs for indentation where existing files use tabs, compact helper functions, and `uk_*` prefixes for project/Unikraft-facing symbols. Keep apps one-image-one-purpose: do not add shell launchers or native `fork()`/`exec()` paths. Prefer existing Makefile wrappers and project utilities over new dependencies.

## Testing Guidelines

Add or update focused tests in `tests/` for library behavior. Test files generally use descriptive names ending in `_test.c`, and single targets can be run via `make -C tests <target>` such as `venus-cs`, `virgl-enc`, or `test-dispatch`. After touching apps, libs, or claims, run `make governance-check lib-readme-check app-port-check`.

## Commit & Pull Request Guidelines

Recent history uses short, imperative subjects, sometimes with a scope prefix such as `docs:` or `bench:`. Keep commits focused and mention the evidence generated or intentionally not run. Pull requests should describe the claim boundary, changed paths, required external stack, and test results; include screenshots or result artifacts when graphics or benchmark output changes.

## Security & Configuration Tips

External roots are declared in `config/external_paths.json` and can be overridden with environment variables such as `LLAMA_ROOT`, `VENUS_PROTOCOL_ROOT`, `VK_INC`, and `VK_LIB`. Never treat `blocked:*` result rows as passing evidence.
