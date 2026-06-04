= Related Work <sec:related>

== Unikernels and Library OSes

VOGUE builds directly on the unikernel and library-OS tradition. MirageOS argued that single-purpose cloud services can be built as type-safe library OSes with small images and strong specialization @mirage-sosp. Unikraft later generalized the idea with a micro-library architecture, Kconfig composition, and evidence that specialization can improve footprint and performance for server workloads @unikraft-eurosys. Drawbridge explored library OS compatibility from the opposite direction: preserve rich Windows application compatibility by moving a large OS personality into an isolated library OS @drawbridge. VOGUE takes the Unikraft/Mirage side of this tradeoff: it does not import a rich OS personality, but instead implements the smallest graphics personality needed by selected workloads.

The Exokernel line of work exposed low-level resources to applications for application-specific management @exokernel. Arrakis and IX similarly split control-plane policy from data-plane I/O, allowing applications to bypass kernel mediation on the fast path @arrakis @ix. VOGUE is narrower: it does not redesign the whole OS I/O model, but it applies the same principle to graphics in a unikernel. The VirtIO-GPU data path becomes a library-level fast path, while the hypervisor and device protocol remain the protection boundary.

== Lightweight Virtualization

Firecracker demonstrates that microVMs can provide fast startup and strong isolation for serverless workloads @firecracker. VOGUE is complementary: it targets what runs inside a minimal VM when the appliance needs graphics. The key question is not whether a VM can be lightweight, but whether the guest graphics stack can be made lightweight enough to preserve unikernel advantages.

== GPU Virtualization

VirtIO-GPU is the device target. The protocol is specified by OASIS @virtio-spec and implemented by QEMU @qemu-vgpu. VirGL translates guest Gallium-style command streams to host OpenGL @virglrenderer. Venus provides a Vulkan-over-VirtIO path, and rutabaga/gfxstream supports ChromeOS/Android graphics virtualization @venus @rutabaga. The vhost-user-gpu mechanism externalizes the GPU backend for process isolation @vhost-user-gpu.

These systems primarily define host/device mechanisms or full guest-stack integration. VOGUE's contribution is guest-side: a small Unikraft library stack that can speak the device protocol and host selected graphics applications without Linux DRM or Mesa. The current artifact advances beyond transport-only bring-up by showing same-run guest Vulkan execution for upstream _llama.cpp_, while still leaving broader graphics compatibility and richer scene coverage outside its scope.

== Minimal Graphics Stacks

Linux DRM/KMS @drm-kms, Mesa @mesa, GBM, EGL, and GLES form the commodity path for direct-rendered applications. VOGUE replaces this stack with a compatibility boundary, not a complete reimplementation. Headless software renderers such as llvmpipe or OSMesa provide software pixels through Mesa; VOGUE's rasterizer is intentionally smaller and less general. The tradeoff is explicit: VOGUE gains auditability and small footprint, but loses shader and compiler coverage until virgl is implemented.

== Application Porting to Unikernels

Prior Unikraft application work focuses on network and storage services. VOGUE extends the porting pattern to graphics by factoring the port into reusable shims rather than application-specific rewrites. The closest analogy is POSIX compatibility work: a narrow API surface enables unmodified source to compile, but every stub must be accounted for. VOGUE makes that accounting visible through claim rows and forbidden claims.
