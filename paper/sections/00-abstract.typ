= Abstract

Unikernels promise tiny images, millisecond boots, and a small attack surface,
but every one of those wins comes from leaving things out: no shell, no
user/kernel separation, no multi-process runtime, and — until now — no GPU.
Meanwhile the workloads that most want a GPU, from graphics to LLM inference
engines such as `llama.cpp`, are exactly the ones a unikernel cannot run today.
The standard way to give a guest GPU access, VirtIO-GPU together with Mesa's
Venus driver, drags the entire Linux graphics stack into the guest: the kernel
DRM/KMS subsystem, `libdrm`, and Mesa. That is precisely the bulk a unikernel
exists to avoid.

This paper asks whether a _minimal_ unikernel guest can reach a real GPU
without importing Linux's graphics stack. We present VOGUE, a VirtIO-GPU stack
for the Unikraft unikernel. Our central observation is that a unikernel has
exactly one address space and one purpose, so the GPU has exactly one client;
the DRM multiplexing machinery that dominates the Linux path can therefore be
removed entirely and replaced by a minimal exclusive-client driver. VOGUE is
organized as four thin guest libraries that mirror, layer for layer, the much
larger Linux tower, collapsing a roughly three-million-line graphics stack to
about five thousand lines while speaking the identical VirtIO-GPU and Venus
protocol that the host already understands.

We build three execution paths on this substrate — software-rendered 2D
display, a minimal virgl 3D path, and Vulkan compute — and port `kmscube`,
`glmark2`, `vkmark`, and upstream `llama.cpp` without modifying their sources.
Following a strict evidence-gated methodology that never lets a passing check at
one abstraction level stand in for a claim at another, we show that the guest
image is 292 KB and boots in 10–11 ms, that graphics workloads render through
our path, and that upstream `llama.cpp` runs GPU-accelerated inference over
Venus on a Tesla V100, reaching 2045.8 tokens/s prefill and 139.3 tokens/s
decode and serving real HTTP requests over lwIP. A performance gap to a Linux
guest on the same Venus bridge remains, and we locate it precisely in our own
Vulkan and driver code rather than in any missing GPU bridge. VOGUE thus shows
that reaching the GPU need not mean "become Linux again."
