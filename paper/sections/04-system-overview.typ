= System Overview <sec:system-overview>

@fig:vogue-arch shows the VOGUE library stack. The system is organized as Unikraft micro-libraries, each with an explicit dependency declaration and a build file that compiles only what is needed. The design rule is conservative: VOGUE reuses Unikraft's existing PCI, virtio, allocator, mmap/POSIX, and synchronization infrastructure and only adds the graphics-specific protocol and compatibility layers.

#include "../figures/vogue-arch.typ"

== Layer Responsibilities

Since the design optimizes for a narrow data plane and explicit compatibility contracts, each layer has a well-defined responsibility:

*libukdma* provides DMA buffer allocation and scatter-gather descriptor construction. It is the only VOGUE layer that interacts directly with guest-physical memory layout for virtio backing; it relies on Unikraft allocation primitives instead of introducing an independent memory manager.

*libukvirtio_gpu* implements the VirtIO-GPU protocol on top of Unikraft's existing virtio/PCI stack: command encoding and submission, response parsing, fence creation and polling, 2D resource management, capset discovery, context management, 3D submit, resource blobs, UUID assignment, and map/unmap helpers. The 2D path is exercised by native display tests. The 3D/blob path is implemented as a real control-queue surface and checked by ABI/static/readiness gates. On the evaluation host the current generated matrix records QEMU/Venus execution, the Mesa-compatible Venus ring-buffer protocol, KMSCube virgl frame proof, and upstream llama.cpp Vulkan runtime as PASS with same-run artifacts.

*libukswrender* is a CPU software rasterizer that renders rotating-cube geometry into a BGRA framebuffer. It takes a rotation angle and an output pixel buffer; internally it clips triangles, performs perspective-correct interpolation, and writes packed 32-bit pixels. It has no dependency on any graphics API.

*libukegl* provides the EGL/GLES2/GBM/DRM shim. On EGL initialization, it opens a connection to `libukvirtio_gpu` and queries capsets. On EGL window surface creation, it allocates a software framebuffer and a DMA buffer. On EGL buffer swap, it invokes `libukswrender` to produce the current frame's pixels, then copies them to DMA memory and issues a host-transfer followed by a resource-flush command. Unsupported Linux DRM behavior remains an explicit safe stub rather than a hidden partial reimplementation.

== Display Pipeline

A single software-rendered frame traverses the following path. The application calls the EGL buffer-swap routine, which triggers `libukswrender` to produce the current frame's pixels at the current rotation angle. The shim copies the resulting pixel data to DMA-backed memory, then issues a `TRANSFER_TO_HOST_2D` command to push the framebuffer into the host-managed resource, followed by a `RESOURCE_FLUSH` command to present it to the virtual display. A fence waits for device completion before the next frame begins. This pipeline is identical whether the application is _kmscube_ or _glmark2_; only the pixel production step differs.

@fig:frame-pipeline shows the six-step per-frame data flow. Steps 3–5 (TRANSFER, FLUSH, fence) repeat every frame at ~4 MB per frame for 1280×800 BGRA output.

#include "../figures/frame-pipeline.typ"

== Accelerated Path Boundary

The accelerated path is intentionally separated from the working software path. VOGUE now has transport-level commands for context creation, context attach/detach, `SUBMIT_3D`, blob allocation, UUID assignment, and map/unmap, plus Mesa-compatible Venus command serialization and ring-buffer protocol support. Those commands are necessary for virgl/Venus acceleration, but they are not sufficient: guest-side appliance integration, a complete QEMU/Venus probe, and frame-proof work are still required. The paper therefore treats the accelerated path as a readiness-and-blocker result until `xport.qemu-vgpu` and the K1 rows pass with same-run evidence.
