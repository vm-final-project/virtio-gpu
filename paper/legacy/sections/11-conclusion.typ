= Conclusion <sec:conclusion>

This paper demonstrates that a unikernel can support practical graphics display workloads without importing a full Linux graphics subsystem. VOGUE implements a standards-based VirtIO-GPU 2D pipeline, a minimal EGL/GLES2/GBM/DRM shim, and a CPU software rasterizer as Unikraft micro-libraries. It also implements the real VirtIO-GPU 3D/blob control-queue surface needed for the next accelerated stage while preserving Unikraft's design principle of reusing existing PCI, virtio, allocator, and POSIX/mmap support instead of reimplementing them.

Our evaluation validates progressive milestones: (disp.2d) a correct 2D display pipeline verified by native evidence-gated tests; (gfx.kmscube.sw) upstream _kmscube_ source compiled and running against the shim with software rendering; (gfx.glmark2.sw) an additional application framework demonstrating substrate generality; (proto.real-driver, vk.readiness) real-driver ABI/readiness and benchmark-design gates; (vk.drm-shim, gfx.vkmark, vk.ggml-dispatch) Venus/Vulkan and ggml substrate gates; and, on the evaluation host, real QEMU/Venus transport, KMSCube SUBMIT_3D/pixel proof, and upstream llama.cpp Vulkan runtime with post-fix same-run `pp512=2232.1` and `tg128=160.2` over Venus. The remaining gaps are HTTP serving, broader vkmark scene rendering, the llvmpipe-specific ENV9 artifact, and model-load optimization rather than decode-path rescue.

The remaining work is no longer first transport proof; it is breadth,
comparison, and performance characterization. The infrastructure for the guest
graphics/Vulkan substrate — context management, blobs, map/unmap, Venus
encoding, ring protocol, static Vulkan dispatch, and bounded application shims
— is now in place and backed by current evidence. The next concrete step is to
expand from bounded demonstrations to broader accelerated workloads, matched
Linux baselines, and request-level LLM serving experiments, while preserving
the same evidence discipline that kept weaker artifacts from being mistaken for
stronger claims. That discipline is itself part of the paper's contribution:
it offers a practical methodology for extending unikernels into graphics- and
GPU-adjacent domains without overclaiming along the way.
