= Introduction

Unikernels — library operating systems compiled together with a single application into a minimal VM image — have demonstrated compelling advantages for server workloads: images under 10 MB, memory footprints of a few tens of megabytes, and boot times of 3-40 ms @unikraft-eurosys. These properties make them attractive for cloud microservices, edge functions, and security-sensitive appliances where specialization reduces both attack surface and resource cost.

Yet an entire class of modern workloads remains out of reach: applications that require accelerated graphics and display output. Interactive edge VMs, cloud gaming streaming, browser isolation containers, graphical test appliances, and ML-adjacent visualization pipelines all need a display substrate and, ideally, GPU-backed rendering. Today, running these workloads on a unikernel requires either a full Linux guest — forfeiting the footprint and boot-time benefits — or accepting that no graphics is available at all.

== The Graphics Stack Problem

The fundamental challenge is that the Linux graphics stack is deeply monolithic. A typical application path involves: the DRM/KMS kernel subsystem, GBM buffer management, EGL platform initialization, Mesa's OpenGL/Vulkan driver stack, virglrenderer or Venus on the host, and a chain of device nodes, ioctls, and file descriptor hand-offs. Each layer assumes the existence of the others. Porting an EGL/GLES2 or Vulkan application to a unikernel naively requires importing gigabytes of Linux subsystem code.

Unikraft's micro-library architecture makes this untenable: every library must declare its dependencies explicitly via its configuration and build files, must not assume POSIX file descriptors for GPU devices, and must interoperate with a single-address-space, cooperative model. Importing Linux DRM wholesale would destroy the very properties that motivate unikernels.

== Thesis

We argue that a unikernel can support practical graphics-capable workloads _without_ importing a full Linux graphics subsystem, by implementing three targeted layers and one evidence gate:

+ A _standards-based VirtIO-GPU frontend_ that speaks the OASIS VirtIO-GPU protocol @virtio-spec directly through Unikraft's existing PCI, virtio, allocator, and synchronization libraries — no Linux DRM driver needed.
+ A _minimal EGL/GLES2/GBM/DRM shim_ that provides the API surface expected by application code without implementing the full Linux graphics stack.
+ A _software rasterizer_ that produces correct pixel output using a CPU path, enabling display pipeline validation independent of virglrenderer availability.
+ A _stage-gated 3D/Venus interface_ that implements and tests the real control-queue ABI separately from the host execution path, so each layer can be validated independently before the end-to-end run.

This thesis leads to a working display pipeline and a falsifiable, now-realised upgrade path. Each stage is evidence-gated: the Unikraft-side protocol surface and evaluation gates, the native Venus ring-buffer protocol, and — on a host with a Venus-capable QEMU and an accessible GPU — the real `virtio-gpu-gl` Venus transport (`xport.qemu-vgpu`), the K1 virgl `SUBMIT_3D` path with a same-run pixel-frame proof, and end-to-end upstream llama.cpp Vulkan inference on the GPU. On a host lacking that stack those rows report a structured `blocked:*` status instead of an unverified claim, so "Vulkan acceleration works" is asserted only where a same-run guest artifact proves it.

== Contributions

This paper makes the following contributions:

+ *VOGUE*, a complete VirtIO-GPU 2D command path for Unikraft, including resource management, DMA backing, scanout, fence synchronization, and display configuration. The implementation covers all commands required to present frames: two-dimensional resource creation, backing attachment, host transfer, scanout binding, resource flush, and capability set query.

+ *A real VirtIO-GPU 3D/Venus control surface* in `libukvirtio_gpu`, including context create/destroy, context attach/detach, 3D submit, blob create/destroy, resource UUID, map/unmap, and ABI checks against the project protocol header. This is transport-level code, not a Mesa/Venus guest driver, and it is evaluated as readiness evidence rather than as an acceleration claim.

+ *A minimal EGL/GLES2/GBM/DRM shim* (`libukegl`) that routes EGL surface creation and EGL buffer swapping through the VirtIO-GPU 2D path, enables real application code to compile against standard EGL/GLES2 headers, and preserves explicit safe-stub boundaries for unsupported Linux DRM behavior.

+ *A CPU software rasterizer* (`libukswrender`) that produces correct BGRA pixel output for rotating-cube and flat-fill workloads, enabling display pipeline testing without a GPU.

+ *Application ports*: upstream _kmscube_ source compiled against the shim (gfx.kmscube.sw evidence row) and a minimal _glmark2-es2_ scene-clear port (gfx.glmark2.sw).

+ *Single-purpose llama.cpp appliances*: `llm.bench.cpu`, `llm.server.cpu`, and `llm.bench.vk` boot upstream `llama.cpp` directly into one entrypoint with no shell. Each Kraftfile compiles only the source file matching its Kconfig mode (`bench.cpp` vs `server.cpp`), and `-Os -ffunction-sections -fdata-sections -Wl,--gc-sections` lets the linker drop every unreached symbol. This honours Unikraft's *one image, one purpose* principle while staying upstream-unmodified.

+ *A generator-backed Venus encoder*: `libukvenus` consumes Mesa's
  `venus-protocol` generator (`vk.xml` + Mako templates) through
  `scripts/gen_libukvenus.py` rather than vendoring 3,000+ lines of guest
  encoder. Bumping the Venus protocol is one upstream `git pull` + `make
  gen-libukvenus`.

+ *An evidence-gated evaluation framework* that distinguishes software-render evidence (gfx.kmscube.sw), real-driver ABI/readiness evidence (proto.real-driver/vk.readiness), QEMU transport evidence (xport.qemu-vgpu), native Venus protocol evidence (proto.venus-enc/proto.venus-ring), and blocked accelerated-frame evidence (K1), preventing overclaiming while still documenting real progress.

The rest of the paper describes the design (@sec:system-overview, @sec:frontend), implementation (@sec:impl), and evaluation (@sec:eval), followed by discussion (@sec:discussion), related work (@sec:related), and conclusion (@sec:conclusion).
