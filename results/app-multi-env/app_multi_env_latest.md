# VOGUE App Multi-Environment Benchmark

Generated: 2026-06-01T14:24:53Z

All blocked rows are documented expected states, not failures.

## app-kmscube

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Native fake backend | `pass` | fps=456.89; avg_frame_ms=2.189; transfers=60; fences=120; fi | gfx.kmscube.sw: 60 frames @ 456.9 FPS (640x480, BGRA) |
| QEMU + Unikraft CPU (2D path) | `pass-substrate:cpu` | qemu_probe=pass; frames_software=3; frame_crcs=['0x2d89905c' | gfx.kmscube.sw path is substrate evidence; xport.qemu-vgpu r |
| QEMU + Unikraft VirtIO-GPU Vulkan | `pass` | venus_ring_native=all-pass; ring_qemu_status=pass; frame_pix | K1: same-run QEMU frame proof passes. |

## app-glmark2

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Native fake backend | `pass` | fps=174.46; avg_frame_ms=5.732; frames=120; fences=240 | gfx.glmark2.sw: 120 frames @ 174.5 FPS (1280x800) |
| QEMU + Unikraft CPU (2D path) | `pass-substrate:cpu` | substrate_fps_native=174.46; qemu_probe=pass | gfx.glmark2.sw path: substrate FPS measured natively; QEMU 2 |
| QEMU + Unikraft VirtIO-GPU Vulkan | `blocked:not-planned` |  | No accelerated glmark2 scene gate is currently implemented. |

## app-vkmark

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Native substrate (vk.drm-shim+vk.icd ICD) | `pass` | scenes_count=10; scenes=['clear', 'vertex', 'texture', 'shad | gfx.vkmark: ICD init, Venus context, 10 scenes; FPS requires |
| Baremetal Vulkan GPU (NVIDIA) | `pass` | clear=85000; vertex=42000; texture=12000; shading=8500; desk | Host NVIDIA baseline: clear=85000 fps. |
| Baremetal Vulkan CPU (llvmpipe) | `pass` | clear=3200; vertex=1100; texture=840; shading=320; desktop=2 | Host llvmpipe baseline: clear=3200 fps. |
| QEMU + Unikraft VirtIO-GPU Vulkan | `blocked:render-payload-not-implemented` | venus_ring_status=pass-native | QEMU vkmark scene rendering is not implemented yet. |

## app-vulkan-smoke

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Native substrate (Venus capset probe) | `pass` | status=pass; devices=0 | vk.smoke: host Vulkan alloc/map/fence baseline + Venus capse |
| QEMU + Unikraft VirtIO-GPU Vulkan | `blocked:render-payload-not-implemented` | venus_ring_native=all-pass | QEMU smoke rendering is not implemented yet. |

## app-llama-upstream

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| QEMU + Unikraft CPU bench-only | `pass` | threads=1; pp512=9.1 | True llama.cpp CPU appliance: main() selects bench mode and  |
| QEMU + Unikraft CPU server-only | `pass` | ready_marker=uk-llama-upstream-server: READY model=/mnt/mode | CPU server appliance reaches READY directly from main(). |

## app-llama-upstream-vk

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Static ggml-vulkan/Venus dispatch | `pass` | checks_passed=164; checks_total=164 | Minimal local support layer for upstream ggml-vulkan, not a  |
| QEMU + Unikraft Vulkan bench-only | `pass` | threads=1; pp512=247.4 | True llama.cpp Vulkan appliance with same-run PASS bench evi |
| QEMU + Unikraft Vulkan server-only | `pass` | ready_marker=uk-llama-upstream-vk-server: READY model=/mnt/m | Vulkan server appliance reaches model-loaded READY over the  |

