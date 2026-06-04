= Background

== Unikraft: the library OS we build on

VOGUE is built on Unikraft, a framework that constructs a unikernel out of
modular libraries @unikraft-eurosys. The developer picks exactly what the
application needs — a libc such as `musl`, a TCP/IP stack such as lwIP, a
scheduler, the device drivers in use — and only those libraries are linked, with
the single application, into one image that QEMU/KVM boots directly.

What you get is one address space and one purpose per image, with nothing linked
in that was not asked for. What is _not_ there is just as important: there is no
Linux kernel, hence no `/dev/dri` and no kernel modules, and there are no
processes, no `fork`/`exec`, and no dynamic loader. The consequence drives the
rest of this paper: anything Linux hands an application for free — device nodes,
mapping GPU memory, loading a driver at run time — VOGUE must either build itself
or do without.

== Why VirtIO-GPU

There are two roads to virtualizing a GPU: passing a physical device through to
one guest (GPU passthrough), or exposing a paravirtual device that the host
backs with its own driver (VirtIO-GPU) @virtio-spec @qemu-vgpu. We take
VirtIO-GPU, for two reasons that matter specifically to a tiny unikernel.

First, it is a natural fit for tiny VMs. A single physical GPU can be _shared_
across many guests — important for the lightweight GUI- and service-style
workloads unikernels target — and the guest needs only a thin frontend, so the
image stays unikernel-small and migratable. Second, it is cheap and thin to
implement: VirtIO-GPU is just a standard virtio device, and Unikraft already has
the virtio and PCI transport, which we reuse unchanged.

== VirtIO-GPU and Venus

VirtIO-GPU is the paravirtual GPU device proper: it provides virtio queues, GPU
resources, and fences for synchronization. On its own it carries 2D display
commands and a 3D command channel. GPU _acceleration_ rides on top of it through
Venus @venus @venus-protocol.

Venus, defined by Mesa and virglrenderer, takes a different approach from
translating graphics APIs: rather than translate, it _serializes the Vulkan API
itself_ into a command stream. The guest encodes each Vulkan call's arguments
into a packed binary stream, ships that stream over VirtIO-GPU's 3D submit
channel, and the host replays it on its real Vulkan driver:

#align(center)[
  guest app (Vulkan) → Venus command stream → QEMU + virglrenderer → host Vulkan driver → GPU
]

The encoding is deliberately compact. Venus uses a PACKED layout with no
inter-field alignment padding; pointer presence is signalled by a 64-bit word
and array presence by a 32-bit count. The guest must serialize every field by
hand and at exactly the right width, because the host decoder will silently
misparse a stream that disagrees, without reporting an error. This is the modern
path for GPU virtualization and the one VOGUE targets; crucially, the guest only
_emits commands_ — the real GPU work happens host-side.

== What this costs a Linux guest

On Linux, that single Vulkan call falls through a tall stack before it leaves
the guest at all. @tbl:linux-tower lists the guest-side layers.

#figure(
  table(
    columns: (auto, 1fr),
    align: (left, left),
    table.header([Layer (top → device)], [Owned by]),
    [application], [the app],
    [Vulkan loader], [Linux graphics stack],
    [Mesa Venus ICD (userspace driver)], [Linux graphics stack],
    [`libdrm`], [Linux graphics stack],
    [kernel DRM `virtgpu` driver (+ DRM/KMS/GEM)], [Linux graphics stack],
    [`virtio-gpu` device], [the hypervisor],
  ),
  caption: [On Linux, four guest-side layers — none of which a unikernel
    wants — sit between the application and the paravirtual device.],
) <tbl:linux-tower>

The key observation is that what crosses to the host is _not_ this tower. It is
only the stream of `VIRTIO_GPU_CMD_*` and Venus commands the bottom of the tower
emits. The host does not care who produced them: the same stream is accepted
from any guest, which makes guests interchangeable at this seam. A unikernel can
therefore speak this protocol with only a fraction of the tower above it — and
that fraction is exactly what VOGUE builds.
