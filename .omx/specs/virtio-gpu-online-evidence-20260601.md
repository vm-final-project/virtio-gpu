# Online Evidence Requirements

Date: 2026-06-01

Execution must verify and cite current upstream/official material for:
- llama.cpp server throughput/runtime knobs (`--parallel`, `--batch-size`, prompt cache, related tuning guidance),
- QEMU VirtIO-GPU Venus `hostmem/blob/venus` requirements and performance-relevant notes,
- Mesa Venus host-visible/mmap transport behavior,
- any upstream Vulkan/KV-cache/dispatch performance work adopted or referenced.

The final implementation/reporting pass should update this note with the exact URLs actually used.

## Verified on 2026-06-01

- **QEMU VirtIO-GPU docs** — <https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html>
  - Why it matters: documents that Venus requires `hostmem` + `blob` + `venus`, says the hostmem window is typically 256M–8G, and states best performance depends on reliably sharing GPU memory with the guest.
- **Mesa Venus docs** — <https://docs.mesa3d.org/drivers/venus.html>
  - Why it matters: documents Venus requirements around external/host-visible memory support and required virtio-gpu parameters (`RESOURCE_BLOB`, `HOST_VISIBLE`, `CONTEXT_INIT`), plus a QEMU example using `virtio-gpu-gl,hostmem=4G,blob=true,venus=true`.
- **llama.cpp server README** — <https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md>
  - Why it matters: confirms first-class server knobs and features relevant to this plan: parallel decoding, continuous batching, `--parallel`, `--batch-size`, `--ubatch-size`, and `--cache-prompt`.
- **llama.cpp Discussion #4130** — <https://github.com/ggml-org/llama.cpp/discussions/4130>
  - Why it matters: maintainer guidance explains that `--ctx-size` must scale with parallel sequences and that continuous batching needs extra KV space to absorb cache fragmentation.
