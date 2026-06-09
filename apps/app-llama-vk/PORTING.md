# llama.cpp Vulkan Port

The Vulkan appliances build upstream ggml-vulkan into native, single-purpose
Unikraft images:

```text
llama.cpp / ggml-vulkan
  -> libvulkan
  -> libukvulkan_venus
  -> libukvirtio_gpu
  -> QEMU Venus
```

Bench and server modes use separate images and direct entrypoints. No shell,
`fork()`, or `exec()` launcher is present. This appliance is local-model only:
it always reads `/mnt/model/model.gguf` and does not download from Hugging Face
or a remote URL at runtime.

```sh
make llama-vk-run ARCH=x86_64 MODEL=/path/to/model.gguf
make llama-vk-server-run ARCH=x86_64 MODEL=/path/to/model.gguf
```

Results are written to `results/llama/llama_vk*.json` and
`results/llama/llama_server_vk*.json`. The server capture records readiness and
HTTP behavior in the same run. `make deps` populates the default repo-local
`llama.cpp` and Vulkan header checkouts used by the build.
