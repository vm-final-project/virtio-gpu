= Graphics Runtime and Application Support <sec:runtime>

== Minimal DRM/GBM/EGL Compatibility Layer

VOGUE does _not_ implement Linux DRM/KMS. Instead, it provides a narrow compatibility layer — libukegl — that presents the DRM/GBM/EGL API surface expected by the target applications while routing all operations to the VirtIO-GPU 2D backend. This is an honest engineering choice: we implement exactly the subset needed to link and run the target applications, with clear documentation of what is stubbed versus real.

The framing in the codebase is explicit: the library is named libukegl (Unikraft EGL shim), not a Linux DRM port. Headers use the same standard names as the Linux EGL, GBM, and GLES2 headers, so application code requires no include-path changes.

=== DRM Stubs

Applications call legacy or atomic DRM initialization functions to set up the DRM device. In libukegl, these functions return a static DRM descriptor whose width and height fields are taken from the display-info query response, and whose run-loop field points to the VirtIO-GPU 2D render loop. All ioctl-based fields (CRTC, connector, and plane objects) are zero — no actual DRM ioctls are issued.

The framebuffer-from-buffer-object helper returns a static stub descriptor with framebuffer ID zero. This satisfies _kmscube_'s reference to the structure without triggering any real page-flip logic.

=== GBM Stubs

The GBM device-creation function returns a non-null sentinel cast from the VirtIO-GPU device pointer. The GBM surface-creation function likewise returns a non-null sentinel. The front-buffer lock function returns null, because the gfx.kmscube.sw path does not use GBM buffer objects for rendering; the software framebuffer is managed directly by libukswrender.

=== EGL Implementation

The EGL functions implement real semantics on top of the VirtIO-GPU backend:

- *Display and initialization*: connect to the VirtIO-GPU device and query its capability sets.
- *Configuration and context*: select a configuration and return a real EGL context handle backed by the internal context structure.
- *Window surface creation*: allocate a software framebuffer and a DMA buffer, create a 2D resource, attach backing, and set scanout.
- *Context binding*: bind the context and surface; set the active software framebuffer.
- *Buffer swap*: invoke the software rasterizer to update the pixel buffer, copy pixels to DMA memory, issue a host transfer followed by a resource flush, then poll the fence.

=== GLES2 Implementation

The GLES2 entry points have enough real behavior for _kmscube_'s smooth-cube path:

- *Shader and program management*: maintain a shader and program table in the context structure; compilation always succeeds and shaders are parsed for uniform and attribute names, but are not JIT-compiled — the software rasterizer handles actual pixel output.
- *Uniform upload*: capture the model-view-projection matrix into the context; the software rasterizer reads it on the next draw call.
- *Draw calls*: invoke the CPU rasterizer with the current MVP matrix and vertex attribute pointer state.
- *Clear operations*: fill the software framebuffer with the specified clear color and respect viewport state.
- *Vertex buffer objects*: maintain a shadow copy of vertex buffer data so attribute pointer offsets can be computed correctly.

== Software Rasterizer (libukswrender)

The software rasterizer provides correct BGRA output for rotating-cube geometry. Given a normalized vertex buffer (position, normal, color), an MVP matrix, and a viewport size, it transforms vertices through the MVP matrix using 4×4 matrix multiplication, clips to the view frustum, projects to screen space, rasterizes each triangle strip using edge-function coverage tests, and applies a simple diffuse lighting model: color × max(0, _n_ · _l_) where _l_ is a fixed directional light. The output is written as packed 32-bit BGRA words.

The rasterizer is 264 lines of C with no external dependencies, making it trivially portable to any Unikraft configuration.

== Static Venus Dispatch Layer (vk.ggml-dispatch)

`libukggml_vulkan` provides a static Vulkan ICD dispatch layer for the `ggml-vulkan.cpp` compute substrate, eliminating the dependency on a host-side dynamic Vulkan loader (`dlopen` / `libvulkan.so`). The library exposes 80+ Vulkan C ABI entry points — covering `vkCreateInstance`, `vkCreateDevice`, memory allocation, buffer management, descriptor sets, compute pipelines, command buffers, and synchronization primitives — all backed by the `libukvenus` Venus SUBMIT\_3D encoder over the vk.drm-shim DRM virtgpu shim.

The entry-point table in `uk_vulkan_dispatch.c` is structured so that `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` return function pointers directly into the Venus encoder, bypassing Vulkan loader extension chains entirely. `uk_ggml_vk_loader.cpp` sets `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` and calls `VULKAN_HPP_DEFAULT_DISPATCHER.init(uk_vkGetInstanceProcAddr)`, satisfying `ggml-vulkan.cpp`'s initialization contract without modification to the upstream source.

Struct layout is read at Vulkan 1.3 spec byte offsets, verified against the Khronos Vulkan-Headers (v1.3.352) located via `$VULKAN_HEADERS_INCLUDE` and the Mesa `vn_protocol_driver_defines.h`. A 164-check dispatch test (`ggml_vk_dispatch_test`, `make llama-ggml-vk-dispatch`) validates API coverage, proc lookup, per-stub return values, handle allocation, descriptor/copy/fill paths, cleanup, and a full 23-step compute bootstrap sequence from `vkCreateInstance` through `vkWaitForFences`.

The vk.ggml-dispatch path is the bridge between VOGUE's Venus transport layer and the upstream `ggml-vulkan.cpp` compute graph executor. Remaining work: Venus ring-buffer reads (`uk_venus_ring_wait_reply`) for real device property queries, and `virtio_gpu_resource_flush` for host VRAM write coherency after each `vkQueueSubmit`.

== Application Porting

=== kmscube

_kmscube_'s upstream source is copied verbatim into the application port directory. The Unikraft-specific modifications are confined to two files: a compatibility header that defines the GBM, EGL, and cube structures using the shim's internal types, and a glue file that provides cube geometry, a shader program factory, GBM and EGL initialization wrappers, and performance counter stubs — all backed by libukegl.

The application harness calls the upstream smooth-cube initializer and then runs a three-frame render loop via EGL buffer swap. No changes were made to the upstream cube rendering or matrix transformation source files.

=== glmark2-es2

The glmark2 port is an official-source, bounded Unikraft adapter for the upstream `glmark2-es2` scene-clear workload (`https://github.com/glmark2/glmark2.git`, reference `22c527cb0556f3a1ac4445aaa52cc532760928d5`). It preserves the source-level workload shape that matters for this artifact: initialize EGL/GLES2, clear the active framebuffer for a deterministic frame count, present each frame, and emit a glmark2-style score marker. The port deliberately omits the full C++ scene framework, libpng/image assets, and unrelated platform backends; therefore gfx.glmark2.sw is a substrate proof, not a full benchmark-suite score.
