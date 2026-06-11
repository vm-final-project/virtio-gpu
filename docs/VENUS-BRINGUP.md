# Venus Runtime Bring-Up

Use the public Make targets:

```sh
make venus-check QEMU=/path/to/qemu-system-x86_64
make llama-vk-bench-run MODEL=/path/to/model.gguf QEMU=/path/to/qemu-system-x86_64
make llama-vk-server-run MODEL=/path/to/model.gguf QEMU=/path/to/qemu-system-x86_64
```

The QEMU binary must support
`virtio-gpu-gl-pci,blob=true,venus=true`. The host also needs a working EGL
render node and a Venus-enabled virglrenderer.

## Status (x86_64)

As of the latest bring-up, all four x86_64 llama appliances **pass**: CPU/Vulkan
bench (`results/llama/llama_{cpu,vk}.json`) and CPU/Vulkan server (`/health` 200,
`/completion` 200). The Vulkan path runs the full model on the GPU over Venus.

Reproducing the Vulkan runs needs a host setup the stock distro QEMU does not
provide. The full, verified recipe is in the README under **"x86_64 Vulkan-server
host bring-up"**; the essentials:

- **Venus-capable virglrenderer + QEMU rebuilt against it.** Stock virglrenderer
  (0.9.1) lacks Venus/blob, and QEMU gates Venus/blob at *compile* time on the
  virglrenderer version. Build a recent virglrenderer (`meson -Dvenus=true
  -Dplatforms=egl`, add the repo `Vulkan-Headers/include` for vk_video headers),
  then `meson setup --reconfigure` + rebuild QEMU against it. Run QEMU with
  `LD_LIBRARY_PATH=<prefix>/lib/...` and
  `VIRGL_RENDER_SERVER_EXEC_PATH=<prefix>/libexec/virgl_render_server`.
- **EGL render node.** On a box with no GPU render node, point `egl-headless` at
  a primary KMS node driven by software: `VOGUE_EGL_RENDERNODE=/dev/dri/card0`
  (honoured by `scripts/app-llama-vk.py`) with `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast
  LIBGL_ALWAYS_SOFTWARE=1`.
- **KVM, not TCG.** The model carries AVX-512 (`-march=native`) that QEMU TCG
  `#UD`s on; the run script auto-selects KVM when `/dev/kvm` is writable.

Key in-tree fixes that make this work: `CONFIG_LIBUKPAGING` on all four llama
Kraftfiles (contiguous heap), the registered+rebased modern virtio-pci patch
(`patches/unikraft/0001-virtio-pci-modern-device-support.patch`) with
`virtio_pci_shm_region_get` + BAR mapping under paging, a page-consistent
`minMemoryMapAlignment` in the Venus dispatch, and `--no-host`/`mparams.no_host`
so weights stay device-local (the default pinned host-visible blob upload does
not complete on a software host driver and otherwise deadlocks model load).

The two Venus probes write:

- `results/venus/qemu_2d_probe.json`
- `results/venus/qemu_venus-ring_probe.json`

The Vulkan llama targets write:

- `results/llama/llama_vk.json`
- `results/llama/llama_server_vk.json`

A `blocked:<reason>` result means an external prerequisite was unavailable. It
must not be reported as a passing runtime.
