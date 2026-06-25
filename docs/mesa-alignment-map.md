# Mesa Alignment Map

This file maps VOGUE's lib-local source layout to Mesa's source layout. It is
not a claim that VOGUE imports Mesa wholesale. It records which behavior is kept
aligned, which behavior is adapted for a single-application Unikraft VM, and
which Linux/desktop mechanisms are intentionally excluded.

## `libs/libukvirtio_gpu`

| VOGUE file | Mesa analogue | Same as Mesa | VOGUE adaptation | Must not import |
| --- | --- | --- | --- | --- |
| `libs/libukvirtio_gpu/protocol/virtio_gpu_proto.h` | `src/virtio/virtio-gpu/*.h` | VirtIO-GPU wire struct layout and command ids | Unikraft-native names and compile-time ABI checks | Linux DRM ioctl wrappers |
| `libs/libukvirtio_gpu/transport/virtio_gpu.c` | `src/virtio/vulkan/vn_renderer_virtgpu.c` transport helpers | feature/capset/context/blob/submit ordering | native VirtIO queues, Unikraft allocators, no fd state | `drmDevicePtr`, render-node enumeration |
| `libs/libukvirtio_gpu/transport/virtio_gpu_priv.h` | private state behind `src/virtio/vulkan/vn_renderer_virtgpu.c` | transport-only device/resource/context ownership | Unikraft virtqueue and allocator state | Vulkan object policy |
| `libs/libukvirtio_gpu/include/uk/virtio_gpu.h` | Mesa virtgpu renderer-facing API shape | exposes caps, capsets, context, blob, map, submit primitives | C API over Unikraft native device instead of Linux ioctls | DRM discovery and syncobj fd APIs |

## `libs/libukvulkan_venus`

| VOGUE file | Mesa analogue | Same as Mesa | VOGUE adaptation | Must not import |
| --- | --- | --- | --- | --- |
| `libs/libukvulkan_venus/include/uk/venus_renderer.h` | `src/virtio/vulkan/vn_renderer.h` | central renderer info for params, capset data, context status | native `libukvirtio_gpu` handles replace DRM fd state | dma-buf/fd import-export |
| `libs/libukvulkan_venus/renderer/venus_renderer.c` | `src/virtio/vulkan/vn_renderer_virtgpu.c` | params -> capset -> context init sequence | native `libukvirtio_gpu` backend and strict/probe modes | DRM fd import/export |
| `libs/libukvulkan_venus/protocol/venus_capset.c` | `src/virtio/virtio-gpu/venus_hw.h`, `src/virtio/vulkan/vn_renderer_virtgpu.c` | versioned `virgl_renderer_capset_venus` field decode | decoded subset exported through `uk_venus_caps` | Linux winsys policy |
| `libs/libukvulkan_venus/ring/venus_ring.c` | `src/virtio/vulkan/vn_ring.c`, `vn_renderer_virtgpu.c` | ring layout, head/tail/status ordering, command wait model | host-visible blob mapping through Unikraft VirtIO-GPU | Mesa device discovery |
| `libs/libukvulkan_venus/ring/vn_ring_shim.c` | `src/virtio/vulkan/vn_ring.h` submit/reply surface | provides `vn_ring_*` callbacks required by generated wrappers | process-current Unikraft Venus context binding | full Mesa thread/winsys model |
| `libs/libukvulkan_venus/protocol/venus_cs.c` | `src/virtio/venus-protocol/vn_protocol_driver*.h` | generated Venus command encoding | scalar helper wrappers for VOGUE callers | hand-written duplicate wire encoders |
| `libs/libukvulkan_venus/protocol/venus_compute.c` | `src/virtio/venus-protocol/vn_protocol_driver*.h` compute/object encoders | real `Vk*` structs and generated `vn_encode_vk*` helpers | compact scalar API for ggml-vulkan | ad hoc compute wire layout |
| `libs/libukvulkan_venus/compat/venus_driver.c` | `src/virtio/vulkan/vn_renderer_virtgpu.c` init entry path | probe VirtIO-GPU, inspect caps/capsets, create Venus context | compatibility API over native transport | Linux DRM UAPI |
| `libs/libukvulkan_venus/generated/vn_protocol_driver*.h` | `src/virtio/venus-protocol/vn_protocol_driver*.h` | generated Venus protocol source of truth | pinned copy for Unikraft image builds | local manual edits |
| `libs/libukvulkan_venus/include/uk/venus.h` | `src/virtio/vulkan/vn_ring.h`, `vn_renderer.h`, Venus protocol headers | public structs mirror ring/capset/protocol responsibilities | single-app Unikraft API boundary | desktop WSI and multiple-driver loader state |
| `libs/libukvulkan_venus/include/uk/vn_cs.h` | `src/virtio/venus-protocol/vn_protocol_driver_cs.h` support surface | encoder/decoder interface expected by generated protocol | small shim over VOGUE encoder buffers | generated protocol rewrites |
| `libs/libukvulkan_venus/include/uk/vn_ring.h` | `src/virtio/vulkan/vn_ring.h` | callback names and submit/reply abstraction expected by generated code | thunk to `uk_venus_ring` and native submit path | Mesa winsys allocation model |
| `libs/libukvulkan_venus/include/uk/vulkan_venus.h` | Venus driver-facing renderer open/close boundary | exposes device/context/probe status to libvulkan | compatibility surface for existing VOGUE callers | full Vulkan ICD object model |

## `libs/libvulkan`

| VOGUE file | Mesa analogue | Same as Mesa | VOGUE adaptation | Must not import |
| --- | --- | --- | --- | --- |
| `libs/libvulkan/runtime/vk_state.h` | `src/vulkan/runtime` object/device state helpers | object ids and lifetime flags live in runtime state | singleton state for static ggml-vulkan ABI subset | multi-ICD loader state |
| `libs/libvulkan/runtime/vk_state.c` | `src/vulkan/runtime` object/device state helpers | handle allocation and lifecycle helpers are separate from entrypoints | fixed singleton handle range for Venus guest ids | generic Vulkan object database |
| `libs/libvulkan/runtime/vk_entrypoints.c` | `src/vulkan/runtime` entrypoint dispatch patterns, `src/virtio/vulkan/vn_*.c` object families | exported ABI grouped behind runtime state and proc lookup | static single-image proc table for ggml-vulkan only | dynamic ICD loader |
| `libs/libvulkan/hpp/vk_hpp_loader.cpp` | Vulkan-Hpp dispatcher storage pattern | application-facing Vulkan-Hpp dispatch loader hook | resolves against VOGUE static proc table | system loader discovery |
| `libs/libvulkan/compat/uk_stdcxx_compat.cpp` | none | no Mesa behavior | Unikraft C++ ABI compatibility for linked upstream C++ code | graphics policy |
| `libs/libvulkan/include/uk/vulkan.h` | Vulkan loader-facing local helper boundary | exposes init/proc-addr entry used by app code | Unikraft-specific public helper API | full loader JSON/ICD manifest handling |
| `libs/libvulkan/include/vulkan/vulkan.h` | Khronos Vulkan-Headers include path | keeps application include spelling stable | forwarding shim to pinned headers | vendored rewritten Vulkan headers |

## `libs/libukvirtgpu_drm`

| VOGUE file | Mesa analogue | Same as Mesa | VOGUE adaptation | Must not import |
| --- | --- | --- | --- | --- |
| `libs/libukvirtgpu_drm/include/drm/virtgpu_drm.h` | Linux `virtgpu_drm.h` UAPI used by Mesa | ioctl numbers and UAPI structs for compatibility builds | repo-local shim header | native path dependency on DRM |
| `libs/libukvirtgpu_drm/drm_virtgpu.c` | Mesa virtgpu DRM ioctl users | Linux-style virtgpu UAPI compatibility shape | optional shim, not used by native VOGUE Vulkan path | DRM node enumeration into native images |
| `libs/libukvirtgpu_drm/drm_virtgpu_fdio.c` | fd/ioctl compatibility glue around DRM callers | fd-like compatibility surface | Unikraft fdio adapter for opt-in compatibility | making fdio mandatory for Venus |
| `libs/libukvirtgpu_drm/include/uk/drm_virtgpu*.h` | none | no Mesa behavior | explicit Unikraft-only shim API | Vulkan runtime ownership |
