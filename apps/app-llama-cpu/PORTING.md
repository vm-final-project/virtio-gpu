# llama.cpp CPU port

Native, single-application Unikraft images that link **unmodified** upstream
llama.cpp on the ggml CPU backend. Bench and server modes build as separate
images and call upstream code directly — no shell, `fork()`, or `exec()`.

```sh
make llama-cpu-bench-run   ARCH=arm64 MODEL=/path/to/model.gguf   # bench
make llama-cpu-server-run  ARCH=arm64 MODEL=/path/to/model.gguf   # HTTP server
```

The model is exposed over VirtIO-9P at `/mnt/model/model.gguf`. Results go to
`results/llama/llama_cpu*.json` and `results/llama/llama_server_cpu*.json`.

The project does not vendor or patch llama.cpp: `LLAMA_ROOT` points at the
external checkout, which `make deps` populates at `.deps/src/llama.cpp`.

## Layout

- `llama-server-entry.cpp` / `bench.cpp` — the two appliance entrypoints (one is
  selected per build; the other becomes an empty TU via its `#if` guard).
- `llama-cpu-common.h` — CPU helpers; delegates shared logic to
  `apps/llama-common/llama-uk-common.h`.
- `compat-spawn.c`, `compat/`, `build-info.cpp`, `ui.h` — link/build shims (below).

## Unikraft build notes

- **Basename collisions.** Unikraft names external objects by basename only, so
  TUs that share a basename (`src/models/llama.cpp` vs `src/llama.cpp`,
  `tools/mtmd/models/*` vs `src/models/*`, the app entry vs upstream
  `tools/server/server.cpp`) collide at link. `Makefile.uk` disambiguates with
  `|variant` tags (e.g. `…/llama.cpp|mdl`) and a renamed app entry
  (`llama-server-entry.cpp`).
- **Stack size.** llama.cpp's graph setup has deep call chains and large frames
  (`llm_graph_result::reset()` alone uses ~25 KiB), so the Kraftfile raises
  `CONFIG_STACK_SIZE_PAGE_ORDER` to 9 (2 MiB); the 64 KiB default overflows
  during `llama_context::graph_reserve()`.
- **Paging / contiguous heap.** Both CPU Kraftfiles (`Kraftfile.llama-cpu{,-server}`)
  set `CONFIG_LIBUKPAGING` so the boot allocator maps all RAM into one contiguous
  heap. Without it `x86_64`/q35 fragments low RAM (32-bit PCI MMIO hole) and the
  large model/compute buffers fail with `ggml_aligned_malloc: insufficient memory`
  regardless of `-m`. `arm64`/virt is unaffected (single contiguous RAM block).
- **Web UI.** Upstream generates `ui.h`/`ui.cpp` via CMake; the direct
  `Makefile.uk` build supplies a dummy `ui.h` and omits the embedded frontend.
- **Cross-compile `-march`.** `-march=native` is invalid when cross-compiling
  from Apple Silicon to an x86_64 guest, so the build falls back to the `x86-64`
  baseline automatically; arm64 uses `armv8-a`.
