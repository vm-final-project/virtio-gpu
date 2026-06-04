= Introduction

Unikernels are attractive because they collapse an application and its minimal
OS support into a single specialized image. Unikraft demonstrated that this
approach can produce small images, low memory footprints, and millisecond-scale
boot times for networked services @unikraft-eurosys. Those benefits matter well
beyond classic microservices: edge appliances, browser-isolation sandboxes,
test harnesses, visualization endpoints, and GPU-adjacent inference services
all benefit from smaller trusted computing bases and faster startup.

Graphics and GPU-backed applications, however, remain an awkward fit for
unikernels. A conventional Linux guest reaches the host renderer through a
deep dependency tower: DRM/KMS in the kernel, GBM/EGL/Mesa in userspace,
virtio-gpu or Venus integration glue, and finally a host renderer such as
virglrenderer. Reusing that path verbatim inside a unikernel would erase the
very specialization that makes unikernels attractive.

This paper studies a narrower question: _what is the minimum guest-side
graphics substrate a Unikraft guest needs in order to present frames and drive
upstream Vulkan workloads through VirtIO-GPU?_ VOGUE answers that question with
a bounded design. It does not import Linux DRM/KMS or Mesa into the guest.
Instead, it introduces a small set of Unikraft libraries that speak the
VirtIO-GPU protocol directly, provide only the application-facing compatibility
surface that the chosen workloads need, and treat stronger claims as
evidence-gated milestones rather than as assumed consequences of partial
implementation.

The key insight is a _dependency collapse_. Linux and VOGUE ultimately speak to
the same host-side protocol seam — VirtIO-GPU control queues, blobs, and Venus
command streams — but they reach that seam through very different guest
dependency structures. Linux relies on a broad DRM/Mesa tower. VOGUE reaches it
through a thin chain of Unikraft libraries plus explicit compatibility
boundaries. That collapse is the paper's architectural contribution: it makes
the graphics substrate small enough to audit, small enough to fit Unikraft's
library model, and still rich enough to support bounded real workloads.

VOGUE validates this claim with an evidence-gated methodology. The system
separates software-render correctness, protocol-readiness checks, QEMU/Venus
transport proof, and end-to-end runtime results so that no weaker artifact can
be misreported as a stronger one. On the evaluation host, this yields a
27-row matrix with 27 PASS rows. The project demonstrates a working 2D display
pipeline, same-run virgl submit/frame proof for `kmscube`, a Mesa-compatible
Venus ring protocol, and upstream `llama.cpp` Vulkan execution through
`virtio-gpu-gl,blob=true,venus=true`. After enabling batched Venus submission
and explicit llama.cpp batch sizing, the latest same-run Vulkan appliance
records `pp512=2232.1 t/s` and `tg128=160.2 t/s` on a Tesla V100, while the
Vulkan server appliance reaches model-loaded readiness with batching enabled.

This paper makes four contributions:

+ A bounded VirtIO-GPU substrate for Unikraft: a real guest-side frontend for
  2D resources, fences, contexts, blobs, UUIDs, and map/unmap, without a Linux
  guest DRM stack.
+ A minimal compatibility layer that lets selected graphics applications keep
  their source-level shape while routing all real work through the narrow
  VOGUE substrate.
+ A dependency analysis showing how VOGUE collapses the Linux DRM/Mesa guest
  tower into a smaller Unikraft library chain while preserving the same
  host-facing VirtIO-GPU/Venus contract.
+ An evidence-gated evaluation discipline that keeps substrate, readiness,
  transport, and runtime claims distinct and therefore reviewable.

The remainder of the paper presents the background and design constraints,
system architecture, implementation, evaluation, limitations, and related work.
