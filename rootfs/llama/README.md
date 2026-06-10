# llama.cpp model rootfs staging

Optional model/shader staging area for the llama.cpp Unikraft appliances.
Current Kraftfiles prefer 9pfs for local runs, but initramfs-style staging may
copy the same files from here:

```text
/models/<name>.gguf      mounted at /mnt/model/model.gguf
/shaders/<name>.spv      optional SPIR-V from upstream ggml-vulkan
```

Real GGUF and SPIR-V artifacts are not vendored into git; the Make runners record
the model path, thread counts, and backend flags per result row.

For Unikraft rows the rootfs must not contain a shell or unrelated app: a llama
appliance boots directly into exactly one entrypoint (CPU/Vulkan × bench/server)
with no `fork()`/`exec()` launcher.
