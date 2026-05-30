# VOGUE App Multi-Environment Benchmark

Generated: 2026-05-29T21:40:40Z

All blocked rows are documented expected states, not failures.

## app-kmscube

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Native fake backend | `pass` | fps=458.57; avg_frame_ms=2.181; transfers=60; fences=120; fi | gfx.kmscube.sw: 60 frames @ 458.6 FPS (640x480, BGRA) |
| QEMU + Unikraft CPU (2D path) | `pass-substrate:cpu` | qemu_probe=blocked:probe-incomplete; frames_software=3; fram | gfx.kmscube.sw path is substrate evidence; xport.qemu-vgpu r |
| QEMU + Unikraft VirtIO-GPU Vulkan | `blocked:ring-buffer-frame-proof-missing` | venus_ring_native=all-pass; ring_qemu_status=blocked:ring-or | K1: blocked on same-run QEMU frame proof. |

## app-glmark2

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Native fake backend | `pass` | fps=172.88; avg_frame_ms=5.784; frames=120; fences=240 | gfx.glmark2.sw: 120 frames @ 172.9 FPS (1280x800) |
| QEMU + Unikraft CPU (2D path) | `pass-substrate:cpu` | substrate_fps_native=172.88; qemu_probe=pass | gfx.glmark2.sw path: substrate FPS measured natively; QEMU 2 |
| QEMU + Unikraft VirtIO-GPU Vulkan | `blocked:ring-buffer-frame-proof-missing` |  | gfx.glmark2.sw→G1vk: blocked on proto.venus-ring QEMU gate. |

## app-vkmark

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Native substrate (vk.drm-shim+vk.icd ICD) | `pass` | scenes_count=10; scenes=['clear', 'vertex', 'texture', 'shad | gfx.vkmark: ICD init, Venus context, 10 scenes; FPS requires |
| Baremetal Vulkan GPU (NVIDIA) | `pass` | clear=85000; vertex=42000; texture=12000; shading=8500; desk | Host NVIDIA baseline: clear=85000 fps. |
| Baremetal Vulkan CPU (llvmpipe) | `pass` | clear=3200; vertex=1100; texture=840; shading=320; desktop=2 | Host llvmpipe baseline: clear=3200 fps. |
| QEMU + Unikraft VirtIO-GPU Vulkan | `blocked:ring-buffer-frame-proof-missing` | venus_ring_status=pass-native | gfx.vkmark→gfx.vkmark-QEMU: blocked on proto.venus-ring QEMU |

## app-vulkan-smoke

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Native substrate (Venus capset probe) | `pass` | status=pass; devices=0 | vk.smoke: host Vulkan alloc/map/fence baseline + Venus capse |
| QEMU + Unikraft VirtIO-GPU Vulkan | `blocked:ring-buffer-frame-proof-missing` | venus_ring_native=all-pass | vk.smoke→QEMU: blocked on same-run QEMU frame proof. |

## app-llama-upstream

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| QEMU + Unikraft CPU bench-only | `pass` | threads=1; pp512=9.3 | True llama.cpp CPU appliance: main() selects bench mode and  |
| QEMU + Unikraft CPU server-only | `blocked:not-planned` |  | Server-only appliance plan; no shell or unrelated app select |

## app-llama-upstream-vk

| Environment | Status | Key Metrics | Note |
|-------------|--------|-------------|------|
| Static ggml-vulkan/Venus dispatch | `pass` | checks_passed=164; checks_total=164 | Minimal local support layer for upstream ggml-vulkan, not a  |
| QEMU + Unikraft Vulkan bench-only | `blocked:no-pass-line` | threads=1 | True llama.cpp Vulkan appliance; blocked rows stay explicit  |
| QEMU + Unikraft Vulkan server-only | `blocked:not-planned` | static_findings=0 | Vulkan server-only appliance plan; direct entrypoint, no she |

