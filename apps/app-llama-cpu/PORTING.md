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
