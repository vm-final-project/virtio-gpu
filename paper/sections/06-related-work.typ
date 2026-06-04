= Related Work <sec:related>

*Unikernels and library OSes.* VOGUE builds on the unikernel tradition. MirageOS
argued that single-purpose cloud services can be built as type-safe library
OSes with small, specialized images @mirage-sosp, and Unikraft generalized the
idea with a micro-library architecture and Kconfig composition @unikraft-eurosys,
which is the substrate VOGUE extends. Drawbridge explored the opposite
trade-off, preserving a rich Windows personality inside a library OS
@drawbridge; VOGUE instead implements the _smallest_ graphics personality its
workloads need. The exokernel line exposed low-level resources to applications
@exokernel, and Arrakis and IX split control-plane policy from data-plane I/O so
applications can bypass kernel mediation on the fast path @arrakis @ix. VOGUE
applies the same fast-path principle narrowly to graphics: the VirtIO-GPU data
path becomes a library-level call while the hypervisor and device protocol
remain the protection boundary.

*Lightweight virtualization.* Firecracker shows that microVMs deliver fast
startup and strong isolation for serverless workloads @firecracker. VOGUE is
complementary — it concerns what runs _inside_ a minimal VM when the appliance
needs a GPU, asking whether the guest graphics stack can stay light enough to
keep the unikernel's advantages.

*GPU virtualization.* VirtIO-GPU is our device target, specified by OASIS
@virtio-spec and implemented by QEMU @qemu-vgpu. virglrenderer translates guest
Gallium command streams to host OpenGL @virglrenderer, Venus carries Vulkan over
VirtIO-GPU @venus @venus-protocol, and rutabaga/gfxstream serves ChromeOS and
Android graphics virtualization @rutabaga, with vhost-user-gpu externalizing the
backend for isolation @vhost-user-gpu. These define host- and device-side
mechanisms; VOGUE's contribution is the _guest_ side — a small Unikraft library
stack that speaks the same protocol and runs selected applications, including
same-run Vulkan execution of upstream `llama.cpp`, without Linux DRM or Mesa.

*Minimal graphics stacks.* Linux DRM/KMS @drm-kms, Mesa @mesa, GBM, EGL, and
GLES form the commodity direct-rendering path. VOGUE replaces this with a
compatibility boundary rather than a complete reimplementation; its CPU
rasterizer is intentionally smaller and less general than software renderers
such as llvmpipe or OSMesa. The trade-off is explicit: VOGUE gains auditability
and a tiny footprint but defers shader and compiler coverage until a full virgl
path exists. Prior Unikraft porting work has focused on network and storage
services @unikraft-apps; VOGUE extends the pattern to GPU workloads by factoring
each port into reusable shims and accounting for every stub through its evidence
rows.
