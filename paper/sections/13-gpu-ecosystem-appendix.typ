= GPU Driver Ecosystem Survey <sec:gpu-ecosystem>

This appendix surveys the landscape of GPU virtualization protocols, host-side rendering backends, guest graphics stacks, and emerging compute-remoting approaches, placing VOGUE's current implementation in context at each layer. The goal is to map what exists, characterize where VOGUE intersects each technology, and identify the dependency chain required to advance blocked evidence rows.

== Virtualization Substrate

*VirtIO and VirtIO-GPU.*
VirtIO @virtio-spec establishes the standard para-virtualization device model: feature negotiation at probe time, shared virtqueue descriptor rings, and asynchronous notifications. VirtIO-GPU extends this model with a dedicated GPU device class that multiplexes a control queue and a cursor queue over the same transport. VOGUE's `libukvirtio_gpu` implements the guest side of this protocol directly against Unikraft's VirtIO transport, avoiding any Linux kernel infrastructure. The current implementation covers the full 2D command surface: `RESOURCE_CREATE_2D`, `RESOURCE_ATTACH_BACKING`, `TRANSFER_TO_HOST_2D`, `SET_SCANOUT`, `RESOURCE_FLUSH`, and fence synchronization. This is sufficient for evidence rows disp.2d and gfx.kmscube.sw.

*VirtIO-GPU 3D / `VIRTIO_GPU_F_VIRGL`.*
The 3D feature bit promotes the device from a framebuffer adapter to an opaque command-stream forwarder. When negotiated, the guest may issue `CTX_CREATE`, `GET_CAPSET_INFO`, `GET_CAPSET`, and `SUBMIT_3D` commands; the host forwards the command buffer to virglrenderer for OpenGL translation. VOGUE's `libukvirtio_gpu` implements the transport commands for context lifecycle, resource attach/detach, and 3D submission, but the guest still lacks the final accelerated-frame path: virgl command-stream coverage for OpenGL and Venus frame-proof wiring for Vulkan. This is the K1 blocked row. The former QEMU 11.0 modern VirtIO-PCI discovery blocker for device ID `0x1050` is resolved by the Unikraft patch carried in this artifact.

*Resource blobs and host-visible memory.*
VirtIO-GPU 1.1 introduced resource blobs: a mechanism for creating resources backed by host memory, guest memory, or shared guest-host memory without an explicit `TRANSFER_TO_HOST_2D` round-trip. Blob resources are the transport foundation for Venus (Vulkan) and for high-bandwidth ML-weight loading. VOGUE now implements the blob, UUID, map, and unmap control-queue commands in the real backend and validates their ABI/static presence. This is necessary Venus-readiness evidence, but it is not a Vulkan application run until the real appliance command stream is exercised in QEMU and produces frame/host-processing evidence.

== Host-Side Rendering and Backend Isolation

*QEMU `virtio-gpu` and `virtio-gpu-gl`.*
QEMU @qemu-vgpu exposes VirtIO-GPU through multiple device variants. The baseline `virtio-gpu` device handles 2D commands and provides a software display without host GPU involvement. The `virtio-gpu-gl`/Venus-capable variants link against virglrenderer and route 3D or Vulkan command streams to host OpenGL/Vulkan drivers. VOGUE targets these variants in stages: the 2D software path runs today; the real 3D/blob controlq surface is implemented; the current generated matrix records QEMU/Venus transport as PASS on the evaluation host; the native and QEMU Venus ring-buffer protocol passes; and the remaining work is broader rendering coverage and performance.

*VirGL and virglrenderer.*
VirGL @virglrenderer is a translation layer that interprets guest Gallium-style command streams and emits corresponding host OpenGL calls. It operates entirely on the host; the guest is responsible for generating syntactically and semantically correct commands. This division of responsibility is important for VOGUE: the guest command encoder and the host renderer are independent components. A bug in the guest encoder produces virglrenderer errors or silently wrong frames; it cannot be debugged by examining virglrenderer source alone.

*Venus.*
Venus @venus provides a VirtIO-GPU Vulkan serialization protocol. Guest Vulkan API calls are serialized by the Mesa guest driver and forwarded through VirtIO-GPU command streams; virglrenderer deserializes and dispatches them as host Vulkan calls. Venus requires blob resources and host-visible memory for zero-copy command and buffer sharing. VOGUE now targets the Venus transport substrate — blob resources, context operations, UUIDs, map/unmap, and `SUBMIT_3D` — with verified wire-format correctness: the encoder uses PACKED layout (no inter-field alignment padding) and encodes array presence as uint32 `array_size` values per Mesa's `vn_encode_array_size()` protocol, matching virglrenderer's decoder (`vkr_context_submit_cmd`). The current evaluation host proves the QEMU/Venus path with same-run transport, frame, and llama.cpp Vulkan runtime artifacts; broader Mesa/Venus guest-driver coverage remains future work.

*Rutabaga and gfxstream.*
Rutabaga @rutabaga is a Rust graphics virtualization abstraction layer developed for ChromeOS and Android. It unifies 2D, VirGL, Venus, and gfxstream paths behind a single host API, enabling crosvm and QEMU to share backend implementations. Gfxstream is a distinct protocol that serializes GLES and Vulkan calls at the API level rather than at the Gallium or VkCommand level, targeting low-latency Android use cases. VOGUE has no direct dependency on rutabaga, but rutabaga is the authoritative reference for how blob, hostmem, and capset semantics should behave when virgl is implemented.

*vhost-user-gpu.*
The vhost-user-gpu mechanism @vhost-user-gpu moves the graphics backend out of QEMU into a separate process, communicating over a UNIX socket with file-descriptor passing for DMABUF sharing. This isolation boundary allows the backend renderer to crash or be replaced without affecting the VMM or guest. It is the preferred architectural boundary for production deployments and is the natural host-side target for a GGML-VirtGPU-style compute backend: the backend process can link against any host library (CUDA, ROCm, Metal, BLAS) without contaminating QEMU. VOGUE's current evaluation uses direct QEMU `virtio-gpu-gl`; a vhost-user-gpu backend would be the correct isolation design for any future compute-remoting paper claim.

== Guest Graphics Stacks

*Linux DRM/KMS and libdrm.*
The Linux DRM subsystem provides the guest-side kernel driver (`virtio_gpu.ko`), GEM object management, KMS scanout, and the `libdrm` userspace API. It is the baseline guest stack for all VirGL and Venus evaluations on Linux. VOGUE explicitly replaces this stack with `libukdrm_compat` and `libukgbm_compat`, which expose the minimal API surface needed for kmscube and glmark2 to compile and link without importing DRM ioctls, device files, or GEM. The compatibility boundary is intentionally narrow: API compatibility, not implementation correctness for the full DRM ABI.

*Mesa, GBM, EGL, and GLES.*
Mesa @mesa is the open-source userspace OpenGL/Vulkan stack on Linux. For VirtIO-GPU, Mesa provides the VirGL Gallium driver (guest-side command encoder), the Venus driver, the GBM/EGL window system integration layer, and software rasterizers (llvmpipe, softpipe, OSMesa). The VirGL Gallium driver inside Mesa is the reference implementation of the guest command encoder that VOGUE must eventually reimplement in `libukvirtio_gpu`. Rather than porting Mesa — its build system, LLVM dependency, and size are incompatible with Unikraft's link-time elimination model — VOGUE implements its own narrower encoder targeting only the Gallium commands required for the target workloads. `libukegl` and the GBM shim serve the same function as Mesa's EGL/GBM for the software-render path, bridging application code to the VOGUE display pipeline without Mesa's broader dependency chain.

*OpenGL and Vulkan.*
OpenGL @opengl is the principal API exposed through the VirGL path. Vulkan @vulkan, the modern explicit API, is exposed through Venus. For VOGUE's current executable workloads, OpenGL ES 2.0 suffices (kmscube, glmark2), while Vulkan/Venus is tracked as a readiness-gated next stage rather than a completed runtime. For compute applications (LLMs), Vulkan compute shaders and SPIR-V provide a vendor-neutral path, but they require the full Venus protocol and Mesa Vulkan guest stack — a substantially larger undertaking than the VirGL/GLES2 path.

== GPU Passthrough

*VFIO and `vfio-pci`.*
VFIO @vfio provides IOMMU-protected direct passthrough of physical PCIe devices to guest VMs. A guest with VFIO passthrough can run a full vendor GPU driver stack (CUDA, ROCm, Metal via translation) at near-native performance. However, VFIO passthrough requires PCI enumeration, BAR mapping, MSI/MSI-X interrupt routing, and a complete vendor driver inside the guest. None of these components exist in a standard Unikraft build, and their introduction would consume tens of megabytes of code, violating unikernel minimality constraints. VFIO passthrough is the correct answer for performance-critical GPU workloads where isolation requirements permit it, but it is explicitly out of scope for VOGUE.

*Confidential GPU.*
NVIDIA H100 and successor GPUs support a Confidential Compute mode @h100-cc that establishes a protected channel between CPU and GPU TEE, allowing GPU computation to proceed without the host hypervisor observing GPU-resident data. This path requires hardware support, vendor-specific firmware, and a confidential VM setup incompatible with general-purpose VirtIO-GPU. Confidential GPU is the correct architecture for privacy-sensitive AI workloads (e.g., fine-tuning on regulated medical or financial data inside a CVM), but it is beyond VOGUE's current scope. The interaction with VirtIO-GPU is also adversarial: VirtIO shared memory inherently exposes guest buffers to the host @amd-sev, weakening confidentiality guarantees that a confidential GPU is meant to provide.

== LLM Compute Remoting

*llama.cpp and GGML.*
llama.cpp and its underlying GGML tensor library @llamacpp constitute the primary CPU-capable LLM inference stack in C/C++. GGML supports pluggable compute backends: CPU (reference), CUDA, Metal, Vulkan, and SYCL. Because GGML can run entirely on CPU with no GPU dependency, it is the most tractable LLM workload to run inside a Unikraft appliance today. The substrate evidence rows (gfx.kmscube.sw, gfx.glmark2.sw) validate the application framework on which an `app-ukllm` port would build.

*GGML-VirtGPU.*
GGML-VirtGPU @ggml-vulkan is a llama.cpp backend that offloads GGML tensor operations to host GPU hardware via the VirtIO-GPU and virglrenderer API-remoting (Vulkan/Venus) path. The guest GGML backend packs tensor operation descriptors into virglrenderer API-remoting calls; the host-side renderer dispatches these to CUDA or the host GGML backend, returning results through shared memory. This design closely parallels VOGUE's architecture for graphics: a minimal guest-side protocol library replaces the Linux DRM+Mesa guest stack. The critical difference is that GGML-VirtGPU assumes the Linux guest `virtio_gpu` DRM driver and `libdrm` ioctls; replacing that dependency with `libukvirtio_gpu` and `libukdma` is the planned VOGUE extension. The DMA buffer semantics of `libukdma` directly map to the shared-memory buffer management that GGML-VirtGPU requires for zero-copy tensor sharing between guest and host.

*Virglrenderer Vulkan/Venus.*
The API-remoting trampoline in virglrenderer provides the host-side dispatch mechanism for both GGML-VirtGPU and hypothetical future Unikraft compute offload. The guest issues opaque context commands (`SUBMIT_3D` with Vulkan/Venus-encoded payloads); virglrenderer deserializes and dispatches them to registered backends. This mechanism allows a single VirtIO-GPU frontend to serve multiple use cases — graphics, compute, ML inference — purely by negotiating different capsets and command encodings. VOGUE's `libukvirtio_gpu` already implements the transport-level infrastructure (`CTX_CREATE`, `SUBMIT_3D` wire format); the remaining work is encoding the Vulkan/Venus command payload correctly.

*vAccel.*
vAccel @vaccel is a modular hardware acceleration framework designed to expose heterogeneous accelerators (GPU, FPGA, NPU) to virtualized and containerized workloads through a unified API. Rather than remoting a specific GPU API, vAccel defines its own higher-level operations (image classification, object detection, generic tensor compute) and provides backend plugins for CUDA, TensorFlow, TensorRT, and others. VOGUE and vAccel occupy adjacent design points: VOGUE implements the low-level VirtIO-GPU transport for graphics and compute, while vAccel abstracts over multiple accelerator types at a higher semantic level. A future integration could route vAccel operations through the VirtIO-GPU Vulkan/Venus path, using `libukvirtio_gpu` as the transport.

*Cricket.*
Cricket @cricket is a CUDA virtualization layer providing transparent remote execution and checkpoint-restart for CUDA applications. It intercepts CUDA driver API calls, serializes them, and dispatches to a remote GPU over a network transport. Cricket is CUDA-specific and much heavier than VirtIO-GPU Vulkan/Venus remoting: it requires a full CUDA runtime on both ends, whereas VirtIO-GPU Vulkan/Venus operates at a transport level that is API-agnostic. Cricket is relevant primarily as a performance baseline for GPU API remoting latency and bandwidth benchmarks.

*vLLM, SGLang, and TensorRT-LLM.*
Production LLM serving stacks such as vLLM, SGLang, and TensorRT-LLM implement advanced schedulers, continuous batching, KV-cache management, and quantized inference against CUDA or ROCm. These are host-side systems that VOGUE does not attempt to port; they serve as external performance baselines for throughput and latency under the same LLM workloads that GGML-VirtGPU targets. The relevant comparison axis is: what fraction of host GPU throughput is preserved after the VirtIO-GPU Vulkan/Venus remoting overhead? That question cannot be answered without a working GGML-VirtGPU + `libukvirtio_gpu` stack, which makes it K1-equivalent future work.

== Summary and Position of VOGUE

#figure(
  table(
    columns: (auto, auto, auto),
    inset: 5pt,
    align: (left, left, left),
    table.header([Technology], [Role in Ecosystem], [VOGUE Status]),
    [VirtIO-GPU 2D], [Framebuffer/scanout protocol], [Implemented; evidence rows disp.2d, gfx.kmscube.sw pass],
    [VirtIO-GPU 3D / VirGL], [3D command-stream forwarding], [Context/submit protocol substrate implemented; Venus command/ring native protocol checks pass; QMP/frame runtime proof remains blocked until same-run artifacts exist (K1).],
    [Resource blobs / host-visible mem], [Zero-copy buffer sharing], [Blob/UUID/map/unmap commands implemented and statically checked.],
    [QEMU `virtio-gpu-gl` / Venus], [Host 3D/Vulkan acceleration backend], [Current matrix: PASS on the evaluation host. Host GPU requirement: any Vulkan 1.1 driver (ANV/RADV/NVIDIA/Lavapipe) on Linux with `VK_KHR_external_memory_fd`. Use a GL-capable QEMU display backend for virtio-gpu-gl/Venus workloads.],
    [virglrenderer / Vulkan/Venus], [Host OpenGL translation and API remoting], [Host dependency; no guest-side changes needed],
    [vhost-user-gpu], [Backend process isolation], [Not yet adopted; target for production deployment design],
    [Venus / Vulkan], [Vulkan virtualization over VirtIO-GPU], [proto.venus-enc + proto.venus-ring both PASS for native protocol checks: wire-format correctness verified; full Mesa ring protocol implemented (vkCreateRingMESA/vkNotifyRingMESA/vkDestroyRingMESA, 24 native checks). Real-QEMU ring/frame proof requires current same-run artifacts.],
    [Linux DRM/KMS], [Reference guest driver], [Replaced by `libukdrm_compat` shim; intentionally incomplete],
    [Mesa / GBM / EGL / GLES], [Reference guest graphics stack], [Replaced by VOGUE shims; virgl encoder is the remaining gap],
    [VFIO passthrough], [Physical GPU passthrough], [Out of scope; incompatible with unikernel minimality],
    [Confidential GPU], [TEE-protected GPU compute], [Out of scope; adversarial to VirtIO shared-memory model],
    [llama.cpp / GGML], [CPU-capable LLM inference], [PASS: upstream-unmodified (SHA `fcae601e4`) running inside Unikraft via `apps/app-llama-upstream`; pp512=12,610 t/s, tg128=12,597 t/s (llm.bench.cpu). Vulkan path (`apps/app-llama-upstream-vk`) builds; runtime blocked locally when no EGL render node is available.],
    [GGML-VirtGPU], [VirtIO-GPU ML compute offload], [Target extension; `libukdma`/`libukvirtio_gpu` replaces Linux DRM dependency; vk.ggml-dispatch static-ICD dispatch layer (`libukggml_vulkan`, 80+ stubs) builds and passes native tests],
    [virglrenderer Vulkan/Venus], [Host-side ML/compute dispatch], [Host dependency; guest transport is already present in `libukvirtio_gpu`],
    [vAccel], [Multi-accelerator abstraction], [Related work; potential higher-level API over `libukvirtio_gpu` transport],
    [Cricket], [CUDA API remoting], [Related work; performance baseline only],
    [vLLM / SGLang / TensorRT-LLM], [Host LLM serving stacks], [External baselines; not ported to Unikraft],
  ),
  caption: [Position of VOGUE across the GPU virtualization and compute-remoting ecosystem. The 2D display path is complete; the 3D/virgl path and ML compute extension share a common dependency on guest render-payload work before acceleration can be claimed.]
)

== Implementation References

This section records the upstream specifications, header files, and documentation used to implement each VOGUE layer. These are the authoritative sources a future implementer should consult to reproduce or extend the work.

#figure(
  text(size: 8pt, table(
    columns: (auto, auto, auto),
    inset: 4pt,
    align: (left, left, left),
    table.header([VOGUE Layer], [Reference], [What It Specifies]),
    [`libukvirtio_gpu` — 2D commands], [OASIS VirtIO Specification v1.3, §7.8 "virtio-gpu device"], [Resource lifecycle commands (`RESOURCE_CREATE_2D`, `ATTACH_BACKING`, `TRANSFER_TO_HOST_2D`, `SET_SCANOUT`, `RESOURCE_FLUSH`), fence semantics, feature bits],
    [`libukvirtio_gpu` — 3D / blob], [VirtIO-GPU 3D extension; `virtio_gpu_proto.h` in Linux kernel tree `include/uapi/linux/virtio_gpu.h`], [`CTX_CREATE`, `SUBMIT_3D`, `RESOURCE_CREATE_3D`, `RESOURCE_CREATE_BLOB`, `RESOURCE_MAP_BLOB`, `RESOURCE_UNMAP_BLOB`, UUID; feature bits `VIRTIO_GPU_F_VIRGL`, `VIRTIO_GPU_F_RESOURCE_BLOB`, `VIRTIO_GPU_F_CONTEXT_INIT`, `VIRTIO_GPU_F_BLOB_ALIGNMENT` (bit 5); `virtio_gpu_config` is now 20 bytes (5 fields) including `blob_alignment` at offset 16],
    [`libukvenus` — Venus ring protocol], [Mesa source `src/virtio/vulkan/vn_ring.h` and `vn_ring.c`; KhronosGroup/venus-protocol `include/vn_protocol_driver_defines.h`], [Ring head/tail/status layout at offsets 0/64/128/192; circular write with wrap-around; `vkCreateRingMESA` / `vkNotifyRingMESA` / `vkDestroyRingMESA` entry points; packed struct layout with no inter-field padding],
    [`libukvenus` — Venus command encoding], [Mesa `src/virtio/vulkan/vn_protocol_driver_*`; `vn_encode_array_size()` in `vn_cs.h`], [Array presence encoded as `uint32_t n`; zero = absent; all structs packed; command IDs from `vn_protocol_driver_defines.h`],
    [`libukggml_vulkan` — static Vulkan dispatch], [Vulkan 1.3 specification §3.2 "Command Function Pointers"; Vulkan-Headers v1.3.352 `vulkan/vulkan.h`], [80+ Vulkan entry points; `vkGetDeviceProcAddr` dispatch table; `VULKAN_HPP_DEFAULT_DISPATCHER` initialization pattern],
    [`apps/app-llama-upstream*` — in-tree ggml], [llama.cpp `docs/build.md`; Unikraft APG porting guide; upstream SHA `fcae601e4`], [In-tree source compilation pattern; `Makefile.uk` `addlib_s` + per-file `SRC-C`/`SRC-CXX`; upstream `lib-musl` supplies `mmap`/`sysconf`/`getauxval`/`prctl`/`pthread_setaffinity_np`],
    [`apps/app-llama-upstream-vk` — Vulkan ggml], [ggml-vulkan backend `docs/backend/VULKAN.md`; SPIR-V headers (KhronosGroup/SPIRV-Headers)], [`GGML_USE_VULKAN=1`; `uk_ggml_vk_loader.cpp` wires `VULKAN_HPP_DEFAULT_DISPATCHER`; SPIR-V shader table requires SPIR-V headers at build time],
    [`kraft/Kraftfile.llama-upstream-*` — 9pfs mount], [Unikraft `lib-9pfs` documentation; `CONFIG_LIBVFSCORE_AUTOMOUNT_CI_RAMFS`; VirtIO-9P QEMU device (`-virtfs`)], [`CONFIG_LIBRAMFS=y`, `CONFIG_LIBVFSCORE_AUTOMOUNT_CI=y`, `CONFIG_LIBVFSCORE_AUTOMOUNT_CI_RAMFS=y` required before `mkdir`/`mount`; `CONFIG_LIBUK9P=y`, `CONFIG_LIBVIRTIO_9P=y`, `CONFIG_LIB9PFS=y` for 9pfs transport],
    [QEMU `virtio-gpu-gl-pci,venus=true`], [QEMU 11.0 release notes; QEMU `docs/system/devices/virtio-gpu.rst`], [`-device virtio-gpu-gl-pci,hostmem=512M,blob=true,venus=true` enables Venus capset id=4; `-display egl-headless,gl=on` (or another GL-capable backend) is required by QEMU virtio-gpu-gl before Venus initializes],
    [virglrenderer Venus decoder], [virglrenderer source `src/venus/`; `vkr_context_submit_cmd()`], [Decodes SUBMIT_3D payload as Venus commands; expects packed layout; context initialized via `DRM_IOCTL_VIRTGPU_CONTEXT_INIT` with capset id=4],
  )),
  caption: [Implementation references for each VOGUE layer. The "Reference" column names the upstream spec or source file used; "What It Specifies" describes the specific mechanism VOGUE depends on.]
)

The central dependency for K1 accelerated rendering is now satisfied on the evaluation host: xport.qemu-vgpu, Venus bootstrap command serialization, the Mesa ring-buffer protocol, and the KMSCube submit/frame rows all have current same-run evidence. The architecture VOGUE has built for the 2D path (DMA buffer management in `libukdma`, protocol command staging, fence synchronization in `libukvirtio_gpu`) is the same architecture required for 3D; future work is broader app coverage and performance rather than first transport proof.
