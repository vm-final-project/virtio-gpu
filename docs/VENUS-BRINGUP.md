# Venus Runtime Bring-Up

Use the public Make targets:

```sh
make venus-check QEMU=/path/to/qemu-system-x86_64
make llama-vk-run MODEL=/path/to/model.gguf QEMU=/path/to/qemu-system-x86_64
make llama-vk-server-run MODEL=/path/to/model.gguf QEMU=/path/to/qemu-system-x86_64
```

The QEMU binary must support
`virtio-gpu-gl-pci,blob=true,venus=true`. The host also needs a working EGL
render node and a Venus-enabled virglrenderer.

The two Venus probes write:

- `results/venus/qemu_2d_probe.json`
- `results/venus/qemu_venus-ring_probe.json`

The Vulkan llama targets write:

- `results/llama/llama_vk.json`
- `results/llama/llama_server_vk.json`

A `blocked:<reason>` result means an external prerequisite was unavailable. It
must not be reported as a passing runtime.
