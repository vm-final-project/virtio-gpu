= Background and Motivation <sec:background>

== Unikraft

Unikraft @unikraft-eurosys is a micro-library OS framework that enables construction of minimal, specialized unikernel VMs. Each library — networking stack, memory allocator, filesystem — is an independent module declared via its own configuration and build files. An application selects only the libraries it needs; the result is a single ELF binary running in a VM with no kernel/user separation.

Unikraft applications measured in the EuroSys 2021 evaluation boot in 3-40 ms and consume as little as 1 MB of image space and 10 MB of RAM @unikraft-eurosys. The architecture enforces minimality through build-system accounting: every dependency must be explicit, dead code is eliminated at link time, and the runtime has no facility for dynamic library loading.

This architecture has proven highly successful for server workloads (network services, key-value stores, serverless functions) but creates tension with graphics workloads that assume rich OS services: device nodes, file descriptors, ioctls, dynamic library search paths, and large dependency chains.

== VirtIO-GPU and VirtIO-GPU-GL

VirtIO @virtio-spec is the standard para-virtualization device framework for guest-host I/O. VirtIO-GPU defines a virtual GPU device that supports two operating modes:

- *2D mode*: the guest manages a framebuffer in guest memory, transfers it to a host-managed resource, and presents it to the virtual display.
- *3D/VirGL mode*: enabled by the VIRTIO_GPU_F_VIRGL feature bit; the guest submits GPU command streams that QEMU forwards to virglrenderer, which translates them to host OpenGL calls @virglrenderer.

The VirtIO-GPU protocol is defined in the OASIS VirtIO 1.3 specification @virtio-spec. Key commands include:

- RESOURCE_CREATE_2D: allocate a host-managed 2D resource.
- RESOURCE_ATTACH_BACKING: attach guest physical memory pages to a resource.
- TRANSFER_TO_HOST_2D: push guest framebuffer data into the host resource.
- SET_SCANOUT: bind a resource to a display head.
- RESOURCE_FLUSH: present the resource to the virtual display.
- GET_CAPSET_INFO / GET_CAPSET: query virgl capability sets.
- CTX_CREATE / SUBMIT_3D: create a virgl context and submit command buffers.

QEMU exposes VirtIO-GPU via the two-dimensional software device and the GL-capable variant with virgl acceleration @qemu-vgpu. The GL variant requires a host OpenGL or EGL driver; without it, QEMU falls back to a software renderer.

== Target Applications

_kmscube_ @kmscube is a minimal DRM/GBM/EGL/GLES2 demo that renders a rotating cube to a KMS scanout without a display server. It was specifically designed as a bare-metal display test for embedded Linux systems, making it an ideal first target for a unikernel graphics stack.

_glmark2-es2_ @glmark2 is an OpenGL ES 2.0 benchmark suite with multiple scenes covering geometry, texturing, shading, and fill rate. Its scene-clear benchmark mode provides a simple, reproducible throughput measurement with no asset loading.

== Why Not Linux DRM?

One might ask: why not simply port the Linux DRM/KMS stack to Unikraft? The answer has three parts:

*Size*. The Linux DRM subsystem spans over 800,000 lines of C across more than 2,000 files. Even the minimal GBM + EGL + Mesa subset required to run _kmscube_ is on the order of 2 million lines of code with complex build systems, LLVM dependencies, and runtime shader compilation infrastructure.

*Architecture mismatch*. Linux DRM assumes kernel-mode driver components, GPU device files, file descriptor passing, and ioctl-based control interfaces. None of these exist in a standard Unikraft build.

*Minimality violation*. Importing DRM/Mesa would destroy the image size and boot-time properties that motivate unikernels in the first place. The goal of VOGUE is to show that the full graphics stack can be substituted by a narrow, application-specific shim.
