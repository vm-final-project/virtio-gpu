# Experimental Environment

This document records the **actual** environment used to run the VOGUE
(`virtio-gpu/`) experiments and the `vogue-baselines/` comparison runs. It was
produced by probing the live machine on **2026-06-19**, not by reading the
project docs — where the two disagree, the values here are authoritative.

All five trees live side-by-side under `/home/rwei/vm-final/`:

| Path | Role |
|------|------|
| `virtio-gpu/`     | VOGUE — Unikraft unikernel appliances (the system under test) |
| `vogue-baselines/`| Comparison baselines (Linux guest, microvm, baremetal, libkrun) |
| `qemu-11.0.1/`    | QEMU source/build used by every VM experiment |
| `virglrenderer/`  | Venus-capable host GPU renderer (source for the installed lib) |
| `llama.cpp/`      | Upstream llama.cpp checkout used by the baselines |

---

## 1. Host machine

CloudLab node `node0.virtio-gpu.ntucsie-pg0.wisc.cloudlab.us`, hardware type
**d7525** (Dell PowerEdge R7525).

| Component | Value |
|-----------|-------|
| CPU       | **2 × AMD EPYC 7302** (16 cores each → **32 cores / 64 threads**), AMD-V |
| Topology  | 2 NUMA nodes (node0: CPUs 0–15,32–47; node1: CPUs 16–31,48–63) |
| Memory    | **~123 GiB** RAM + 8 GiB swap |
| GPU       | **NVIDIA A30** (GA100GL, PCI `0000:25:00.0`, deviceID `0x20b7`, **24 GiB** VRAM, 165 W) |
| Display   | Matrox G200eW3 (onboard, boot console only) |

### DRM / render nodes (matters for `egl-headless`)

| Node | Driver | Usable as GPU render node? |
|------|--------|----------------------------|
| `/dev/dri/card0`      | `mgag200` (Matrox) | No — display only, no render node |
| `/dev/dri/card1`      | `nvidia`           | A30 |
| `/dev/dri/renderD128` | `nvidia`           | **Yes — the only working render node** |

`/dev/dri/by-path/pci-0000:25:00.0-render → renderD128`. Because the Matrox card
has no render node, **the only render node EGL can bind is the A30**. The
Venus/virgl GL path is therefore **GPU-backed**, even though some docs and env
knobs (`GALLIUM_DRIVER=llvmpipe`, `LIBGL_ALWAYS_SOFTWARE=1`) describe a software
path.

---

## 2. Operating system & kernel

| Item | Value |
|------|-------|
| Distro | **Ubuntu 26.04 LTS** (Resolute Raccoon) |
| Kernel | **7.0.0-22-generic** x86_64 |
| KVM    | `/dev/kvm` present; all VM runs use `-machine accel=kvm -cpu host` |
| User / groups | `rwei` ∈ `sudo video users docker render kvm` |

> Note: `sudo` is non-interactive on this host (no controlling terminal), so
> `apt`/`modprobe` must be run manually by the operator, not by tooling.

---

## 3. GPU driver & CUDA

| Item | Value |
|------|-------|
| NVIDIA kernel driver | **595.71.05** (open kernel module) |
| CUDA runtime (per `nvidia-smi`) | 13.2 |
| CUDA **toolkit** (`nvcc`) installed | **12.4** (`nvidia-cuda-toolkit 12.4.131`) |
| Modules loaded | `nvidia`, `nvidia_drm`, `nvidia_modeset`, `nvidia_uvm` |

The toolkit (12.4) — not the driver's advertised 13.2 — is what compiles the
`baremetal-cuda` baseline.

---

## 4. Virtualization stack (QEMU + Venus)

**Every VM experiment (VOGUE *and* baselines) uses the locally built QEMU 11.**

| Item | Value |
|------|-------|
| QEMU used | **`/opt/qemu-11.0.1/bin/qemu-system-x86_64`** (v**11.0.1**) |
| Build dir | `/home/rwei/vm-final/qemu-11.0.1/build` |
| Configure | `-Dkvm=enabled -Dvirglrenderer=enabled -Dopengl=enabled -Dslirp=enabled -Dvirtfs=enabled` |
| Host renderer | **virglrenderer 1.11.0** (Venus), `/usr/local/lib/x86_64-linux-gnu/libvirglrenderer.so.1` |
| Venus render server | `/usr/local/libexec/virgl_render_server` |
| virglrenderer source | `/home/rwei/vm-final/virglrenderer` |

> A distro QEMU **10.2.1** sits at `/usr/bin/qemu-system-x86_64` and is first on
> `PATH`, but it is **not** the binary used for the recorded runs. The baseline
> `results/env/qemu_probe.json` shows 10.2.1 only because that probe used the
> default `PATH`; the actual run commands in every result JSON invoke
> `/opt/qemu-11.0.1/...`. A distro `libvirglrenderer1` 1.2.0 is likewise present
> but not linked by the `/opt` QEMU.

GPU device line used by the GPU experiments:

```
-display egl-headless,gl=on -vga none
-device virtio-gpu-gl-pci,hostmem=<2G|4G>,blob=true,venus=true
```

---

## 5. Vulkan host stack

| Item | Value |
|------|-------|
| Loader (`libvulkan1`) | **1.4.341** |
| Mesa | **26.0.3** (`mesa-vulkan-drivers`, LLVM 21.1.8) |
| Shader compiler | `glslc` / **shaderc 2026.1**, glslang 16.x, SPIR-V tools |
| API headers (pinned in repo) | Vulkan-Headers / SPIRV-Headers `vulkan-sdk-1.4.350.0` |

Installed Vulkan ICDs (`/usr/share/vulkan/icd.d/`):

| ICD file | Driver | Notes |
|----------|--------|-------|
| `nvidia_icd.json` | NVIDIA proprietary | Vulkan API **1.4.329**, driverVersion 595.71.5.0 |
| `lvp_icd.json`    | **lavapipe / llvmpipe** (Mesa 26.0.3) | CPU device, Vulkan 1.4.335. **Filename is `lvp_icd.json`**, not `*.x86_64.json` |
| `virtio_icd.json` | Mesa **Venus** (`libvulkan_virtio.so`) | Used **inside the Linux baseline guest** |
| (also present) | radeon, intel, nouveau, asahi, gfxstream | unused here |

To force the CPU device on the host:
`VK_ICD_FILENAMES=VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`.

---

## 6. Unikraft & build toolchain (VOGUE)

| Item | Value |
|------|-------|
| KraftKit | `kraft` **0.12.13** (go1.26.2 build) at `/usr/bin/kraft` |
| Unikraft | **0.21.0** (`RELEASE-0.21.0`) under `virtio-gpu/.unikraft`, + 5 local patches |
| Key patches | modern virtio-pci device / 1:1 MMIO BAR mapping; `CONFIG_LIBUKPAGING=y` (contiguous heap) |
| C / C++ compiler | **gcc 15.2.0**, clang 21.1.8 |
| Build flags (gcc-15) | `-std=gnu17`, `-std=gnu++17 -fpermissive` |
| cmake / make / ninja | **4.2.3** / 4.4.1 / **1.13.2** |
| Python | **3.14.4** (runner scripts) |

(No Rust/Go on `PATH`; `kraft` ships as a prebuilt Go binary.)

---

## 7. Container / lightweight-VMM tools

| Item | Value |
|------|-------|
| Docker | **29.5.3** (overlayfs, runc) — builds the Linux baseline guest image |
| crun   | present (`/usr/bin/crun`) |
| **libkrun (`krun`)** | **not installed** → `libkrun-vulkan` baseline records `blocked:krun-missing` |

---

## 8. Workload

| Item | Value |
|------|-------|
| Model | **`google_gemma-3-1b-it-Q4_K_M.gguf`** (~806 MB / 768 MiB), in `virtio-gpu/models/` |
| Inference engine | upstream **llama.cpp tag `b9581`** |
| Benchmark | `llama-bench -p 512 -n 128` → metrics **pp512** (prompt) and **tg128** (gen), tokens/s |
| GPU offload (baremetal/Vulkan) | `-ngl 99` (all layers on GPU) |
| Core counts (baselines) | run at **1** and **16** threads/vCPUs (`CORES="1 16"`) |

---

## 9. Configurations and their QEMU launch

### VOGUE appliances (`virtio-gpu/scripts/app-llama-*.py`)

| Mode | Image | vCPU | RAM | GPU device | Net |
|------|-------|------|-----|------------|-----|
| `llama-cpu`        | `vogue-llama-cpu_qemu-x86_64`        | 1 | 8 GiB | — (`-nographic`) | — |
| `llama-cpu-server` | `vogue-llama-cpu-server_qemu-x86_64` | 1 | 8 GiB | — | virtio-net + `hostfwd …:8080` |
| `llama-vk`         | `vogue-llama-vk_qemu-x86_64`         | 1 | 8 GiB | `virtio-gpu-gl-pci,hostmem=2G,blob=true,venus=true` | — |
| `llama-vk-server`  | `vogue-llama-vk-server_qemu-x86_64`  | 4 | 8 GiB | same as `llama-vk` | virtio-net + `hostfwd …:8080` |

Representative GPU (vk) command:

```
/opt/qemu-11.0.1/bin/qemu-system-x86_64 \
  -machine accel=kvm -cpu host -smp 1 -m 8192 -no-reboot \
  -kernel .unikraft/build/vogue-llama-vk_qemu-x86_64 \
  -display egl-headless,gl=on -vga none \
  -device virtio-gpu-gl-pci,hostmem=2G,blob=true,venus=true \
  -append console=ttyS0 -serial mon:stdio -monitor none \
  -fsdev local,id=model,path=<tmp>,security_model=none \
  -device virtio-9p-pci,fsdev=model,mount_tag=model
```

The model is shared into the guest over **virtio-9p**; output and timing markers
are read from the serial console.

### Baselines (`vogue-baselines/scripts/runners/*.py`)

| Baseline | VMM / machine | GPU path | Status |
|----------|---------------|----------|--------|
| `qemu-linux`      | QEMU full machine (`accel=kvm`) | virtio-gpu-gl + **Venus** (Mesa in guest) | pass |
| `qemu-microvm`    | `-machine microvm,accel=kvm,pcie=on,acpi=on,rtc=on` (reuses the Linux guest) | virtio-gpu-gl + Venus | pass |
| `baremetal-vulkan`| none (native host) | native Mesa/NVIDIA Vulkan | pass |
| `baremetal-cuda`  | none (native host) | native CUDA (`-ngl 99`) | pass |
| `libkrun-vulkan`  | libkrun/crun | passthrough | **blocked (krun missing)** |

`qemu-linux` GPU device uses `hostmem=4G` (vs VOGUE's `2G`); both use
`-display egl-headless,gl=on`.

---

## 10. Guest images

| Image | Built artifact | Size | Kernel |
|-------|----------------|------|--------|
| VOGUE CPU        | `vogue-llama-cpu_qemu-x86_64`        | ~4.3 MB  | Unikraft 0.21.0 |
| VOGUE CPU server | `vogue-llama-cpu-server_qemu-x86_64` | ~7.9 MB  | Unikraft 0.21.0 |
| VOGUE VK         | `vogue-llama-vk_qemu-x86_64`         | ~35 MB   | Unikraft 0.21.0 |
| VOGUE VK server  | `vogue-llama-vk-server_qemu-x86_64`  | ~38.7 MB | Unikraft 0.21.0 |
| Linux baseline   | `linux-vulkan-vmlinuz` + `linux-vulkan-initramfs.cpio.gz` | ~15 MB + ~305 MB | **6.8.0-124-generic** |

The Linux/microvm baseline guest is built in a **Ubuntu 24.04 Docker** image, so
its guest kernel is **6.8.0-124** — distinct from the host kernel (7.0.0). It
forces the Venus ICD inside the guest via
`VK_ICD_FILENAMES=VK_DRIVER_FILES=/usr/share/vulkan/icd.d/virtio_icd.json`.

---

## 11. Environment variables

| Variable | Purpose |
|----------|---------|
| `VOGUE_EGL_RENDERNODE` | Pin the `egl-headless` render node (only `/dev/dri/renderD128` works here; unset → auto-scan) |
| `LLAMA_BUILD` / `LLAMA_SRC` | Pre-built / source llama.cpp location (baselines) |
| `KVER` | Baseline guest kernel version (default `6.8.0-124-generic`) |
| `CORES` | Baseline thread/vCPU sweep (default `1 16`) |
| `QEMU`, `MODEL`, `ARCH`, `RUN_TIMEOUT`, `VOGUE_RESULTS` | Baseline Makefile knobs |

---

## 12. Open caveat

What this document does **not** establish: which host Vulkan driver
virglrenderer's Venus path actually dispatches to (the A30 vs. lavapipe). The
only usable render node is the A30, but the host-side Vulkan device Venus
selects has not been pinned down here, and the VK inference *output* has been
observed to be unreliable. Treat VK throughput numbers (especially `tg128`) as
**submission-rate**, not validated end-to-end completion, until that is
confirmed.
