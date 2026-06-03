#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Background

== Unikraft: the library OS we build on

#bred[Unikraft] is a framework that builds a unikernel from #bred[modular
  libraries]:
- you pick exactly what you need
  - a libc (`musl`), a TCP/IP stack (`lwIP`), a scheduler, device drivers, etc.
- only those are linked with your one app into a single image, booted by QEMU/KVM.

#pause

#textbox(
  [*What you get*

    - one address space, one purpose per image
    - only the libraries you asked for are linked in
  ],
  [*What is #red[not] there*

    - no Linux kernel — no `/dev/dri`, no kernel modules
    - no processes, no `fork`/`exec`, no dynamic loader
  ],
)

#pause

#v(0.3em)

- #red[Consequence]: anything Linux hands you for free — device nodes, mapping
  GPU memory, loading a driver — we must #bred[build ourselves, or do without]

== Why we use virtio-gpu

Two roads exist — GPU passthrough or para-virtual #bred[virtio-gpu]. We take
virtio-gpu, for two reasons that matter to a #bred[tiny unikernel] specifically:

- A fit for tiny VMs:
  - one physical GPU #bred[shared] across many guests, especially for lightweight workloads like GUI apps
  - the guest needs only a #bred[thin frontend], so the image stays unikernel-tiny and migratable

- Cheap and thin to implement:
  - just a #bred[standard virtio device] — Unikraft already has the virtio / PCI transport; we reuse it unchanged

== virtio-gpu and Venus: shipping GPU commands to the host

- #bred[virtio-gpu] — the paravirtual GPU device: virtio queues, GPU resources, fences
- #bred[Venus] (from Mesa) — don't translate graphics; #red[serialize the Vulkan
    API itself] into a command stream, ship it over `virtio-gpu`, and let the host
  replay it on its real Vulkan driver

#pause

#v(1.2em)

#align(center)[
  #fletcher-diagram(
    spacing: (3.4em, 1.4em),
    node-stroke: 0.7pt + sky,
    node-fill: skyll,
    node-inset: 10pt,
    node((0, 0), [guest app\ (Vulkan)]),
    edge((0, 0), (1, 0), "->"),
    node((1, 0), [Venus\ command stream]),
    edge((1, 0), (2, 0), "->"),
    node((2, 0), [QEMU +\ virglrenderer]),
    edge((2, 0), (3, 0), "->"),
    node((3, 0), [host Vulkan\ driver → GPU]),
  )
]

#pause

#v(1.0em)

- thin and close to the metal — the #bred[modern] path, and the one we target
- the guest only #bred[emits commands] — the real GPU work happens host-side

== What this costs a Linux guest: the tower

On Linux, one Vulkan call falls through a #bred[tall stack] before it ever leaves
the guest:

#table(
  columns: (auto, 1fr),
  align: (left, horizon),
  [Layer (top → device)], [Owned by…],
  table.hline(),
  [application], [the app],
  [Vulkan loader],
  table.cell(rowspan: 4, fill: skyll)[
    #bred[Linux graphics stack] \
    #text(size: 0.82em)[four guest-side layers, none of which a unikernel wants]
  ],
  [Mesa Venus ICD (userspace driver)],
  [`libdrm`],
  [kernel DRM `virtgpu` driver (+ DRM / KMS / GEM)],
  [`virtio-gpu` device], [the hypervisor],
  table.hline(),
)

#pause

- but what #bred[crosses to the host] is only a stream of `VIRTIO_GPU_CMD_*` + Venus commands
- #red[Key seam]: the host doesn't care #bred[who] emits them — same stream, interchangeable guests
- so a guest can speak this protocol with a #bred[fraction] of the tower #h(0.3em) ($<-$ that is our Design)
