# llama.cpp CPU Port

The CPU appliances are native, single-application Unikraft images. Bench and
server modes build separately and call upstream code directly. They do not use
a shell, `fork()`, or `exec()`.

```sh
make llama-cpu-run ARCH=arm64 MODEL=/path/to/model.gguf
make llama-cpu-server-run ARCH=arm64 MODEL=/path/to/model.gguf
```

The model is exposed through VirtIO-9P. Results are written to
`results/llama/llama_cpu*.json` and
`results/llama/llama_server_cpu*.json`.

The project does not vendor or modify llama.cpp. `LLAMA_ROOT` points to the
external checkout used by the Kraft build; `make deps` populates the default
repo-local path under `.deps/src/llama.cpp`.

## Embedded Web UI Assets

The upstream `llama.cpp` server embeds its Web UI assets by generating `ui.h`/`ui.cpp` via CMake during standard host builds. Because the Unikraft single-application build compiles the server files directly using `Makefile.uk` (bypassing the CMake-driven asset embedding process), a dummy `ui.h` is provided in the application root ([ui.h](file:///Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/apps/app-llama-cpu/ui.h)) to satisfy compiling requirements while intentionally omitting the embedded Web UI frontend assets.

## Target CPU Architecture & Cross-Compilation

By default, the x86_64 target compilation sets the CPU architecture compilation flag (`-march`) to `native`. However, when cross-compiling from Apple Silicon (ARM64) macOS host for an x86_64 guest target, `-march=native` is unsupported by the cross-compiler toolchain and results in a compilation error.

To address this, the build scripts dynamically detect if the host is Apple Silicon (`arm64`) and the target is `x86_64`, falling back to standard `x86-64` baseline instructions automatically.


