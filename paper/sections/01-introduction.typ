= Introduction

A unikernel fuses a single application with just-enough operating system into
one bootable image: one address space, no processes, booted straight by the
hypervisor @unikraft-eurosys. The payoff is concrete. Images are measured in
hundreds of kilobytes rather than hundreds of megabytes; the system is ready in
milliseconds rather than seconds; and the attack surface is only the code that
was actually linked in. Every one of these wins, however, comes from leaving
things out. There is no shell, no user/kernel separation, and no multi-process
runtime, and the ecosystem is young enough that many drivers and features have
simply not been ported yet.

GPU support is one of those missing features, and the gap matters. Plenty of
in-demand workloads need a GPU: graphics and rendering for desktop, GUI, and 3D
visualization; LLM inference with engines such as `llama.cpp`; and a long tail
of ML training, video transcoding, and scientific compute. On a unikernel
today these workloads either cannot run at all or are forced onto the CPU. The
natural response is to add GPU support — but the obvious way of doing so
reintroduces exactly what the unikernel was built to avoid.

== The dilemma

The standard recipe for giving a guest GPU access is VirtIO-GPU paired with
Mesa's Venus driver @virtio-spec @venus. On Linux that recipe requires the
entire guest-side graphics stack: the kernel DRM/KMS subsystem, the GEM memory
manager, `libdrm`, the Vulkan loader, and Mesa itself. A single Vulkan call
falls through this tall tower before it ever leaves the guest. Importing that
tower into a unikernel would mean re-importing the bulk a unikernel exists to
shed — in effect, "becoming Linux again" just to reach the device.

Yet there is a seam in this picture. What actually crosses from guest to host is
not the tower; it is a stream of `VIRTIO_GPU_CMD_*` commands and Venus-encoded
Vulkan calls. The host — QEMU plus virglrenderer plus a real GPU driver — does
not care _who_ emits that stream. The same bytes work regardless of which guest
produced them. This makes the guests interchangeable, and it raises the
question this paper sets out to answer:

#align(center)[_Can a minimal unikernel guest reach a GPU without importing
Linux's graphics stack?_]

Concretely: can we run GPU-accelerated graphics and LLM inference inside a
unikernel without giving up its tiny image and instant boot?

== VOGUE

We answer in the affirmative with VOGUE, a VirtIO-GPU stack built on the
Unikraft library OS. The design turns a property of the unikernel into a
simplification of the driver. Because a unikernel is one process with one
purpose, the GPU device has exactly one client, so the DRM multiplexing,
context arbitration, and resource locking that dominate the Linux path are
unnecessary. We discard them and keep a minimal exclusive-client driver.

The result is a graphics substrate of roughly five thousand lines — against a
Linux stack on the order of three million — arranged as four thin guest
libraries that map directly onto the Linux layers they replace, while emitting
the same VirtIO-GPU and Venus protocol the host already speaks. On top of this
substrate we build three execution paths (2D software display, a minimal virgl
3D path, and Vulkan compute) and port four upstream applications unmodified.

This paper makes the following contributions:

- *A dependency-collapse design.* We show that single-client ownership lets a
  unikernel replace the Linux DRM/Mesa tower, layer by layer, with a small chain
  of Unikraft libraries that reach the identical protocol seam (#ref(<sec:design>)).

- *Three execution paths and unmodified ports.* We implement software 2D
  display, a deliberately minimal virgl 3D path, and a Vulkan compute path, and
  run `kmscube`, `glmark2`, `vkmark`, and upstream `llama.cpp` over them without
  source changes (#ref(<sec:impl>)).

- *An evidence-gated evaluation.* Following a methodology that refuses to let a
  pass at one layer imply success at another, we report a 292 KB image,
  10–11 ms boot, working graphics paths, and `llama.cpp` GPU inference over
  Venus on a Tesla V100, while keeping the remaining performance gap explicit
  and located in our own code (#ref(<sec:eval>)).
