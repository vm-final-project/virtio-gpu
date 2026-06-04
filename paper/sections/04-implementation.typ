= Implementation <sec:impl>

The implementation follows the narrative arc of the design: we built the core
driver, validated the simplest path first, then enabled the GPU, and along the
way confronted how exacting the protocol specifications are. This section
covers the driver stack (#ref(<sec:impl-driver>)) and the application ports
(#ref(<sec:impl-apps>)).

== The driver stack <sec:impl-driver>

=== VirtIO-GPU: the core driver

`libukvirtio_gpu` implements the full VirtIO-GPU protocol in about 1,650 lines
of core driver logic, owning the device exclusively. The simplest thing it does
is the 2D display pipeline that brings up `kmscube` and `glmark2`, and it does
so without touching the GPU at all: the CPU draws pixels, the driver pushes them
to QEMU, binds the resource to the display, and waits.

Setup happens once. `RESOURCE_CREATE_2D` asks QEMU to allocate a 2D resource of
a given resolution and pixel format, and `RESOURCE_ATTACH_BACKING` hands QEMU
the physical page list of the guest's DMA buffer; from then on the guest and
host share that memory. Each frame then repeats five steps, shown in
@fig:frame-pipeline: the CPU rasterizer fills the DMA buffer,
`TRANSFER_TO_HOST_2D` tells QEMU to read guest memory into the host resource,
`SET_SCANOUT` binds the resource to a virtual display, `RESOURCE_FLUSH` asks
QEMU to present it, and a fence wait blocks until QEMU has finished the frame so
the CPU does not overwrite a buffer QEMU is still reading.

#include "../figures/frame-pipeline.typ"

Fences are how the guest learns a command finished: it submits a fence command
carrying a unique ID, QEMU completes commands in submission order and writes the
fence ID back to a guest-readable location, and the guest spin-polls until it
sees that ID. This in-order guarantee is precisely what enables an optimization.
The VirtIO-GPU specification mandates in-order processing, so:

#align(center)[
  ```
  Naive:     TRANSFER → fence₁ → wait → FLUSH → fence₂ → wait
  Optimized: TRANSFER → FLUSH → fence → wait
  ```
]

Because `TRANSFER` is submitted before `FLUSH`, waiting on the `FLUSH` fence
already implies the `TRANSFER` completed; the transfer needs no fence of its
own, saving one round-trip wait per frame. With this path, `kmscube` reaches
about 484 fps in software and `glmark2`'s scene-clear run works.

=== Venus: GPU acceleration

Venus is the GPU-acceleration extension of VirtIO-GPU. `libukvenus` serializes
each Vulkan call into a PACKED binary stream and ships it through `SUBMIT_3D` to
the host's virglrenderer, which decodes and executes it on the real GPU. Two
mechanisms work together to make this efficient, illustrated in
@fig:venus-ring.

*Shared memory (L4).* A blob resource gives the guest a pointer into
host-visible memory. `RESOURCE_CREATE_BLOB` allocates a host-visible region of
type `HOST3D_GUEST`, `RESOURCE_MAP_BLOB` maps it into the guest address space
through the PCI SHM BAR, and `RESOURCE_UNMAP_BLOB` releases it. Where an ordinary
transfer copies (guest writes → DMA copy → host reads), a blob lets the guest
write where the host reads directly — zero copy. This shared region is the
backing store for the Venus ring buffer.

*Ring buffer (L3).* The ring is built on the blob resource with a
cache-line-separated layout: `head` at offset 0 (advanced by the host),
`tail` at offset 64 (advanced by the guest), `status` at offset 128, and the
circular, power-of-two data region at offset 192. Separating the control fields
by 64 bytes — one CPU cache line — keeps the guest's `tail` updates and the
host's `head` updates from causing false sharing. The guest registers the ring
with `vkCreateRingMESA` (command 188), batches many serialized Vulkan commands
into the data region, publishes a new `tail` with a store-release so the command
data is visible before the tail, and calls `vkNotifyRingMESA` (command 190) to
trigger a single `SUBMIT_3D`; it then polls `head` (up to 1,000 iterations) until
the host catches up. `vkDestroyRingMESA` (command 189) tears the ring down.
Each `SUBMIT_3D` is a full virtqueue operation — write descriptors, ring the
doorbell, reap the response — so amortizing many commands into one notification
is what makes the path practical.

#include "../figures/venus-ring-protocol.typ"

The Venus PACKED encoding is unforgiving, and getting it exactly right was a
substantial part of the work. Fields carry no inter-field alignment padding, a
64-bit field is not 8-byte aligned in the stream, pointer presence is a 64-bit
word, and array presence is a 32-bit count — a distinction that, when confused,
produces a stream the host silently misparses. We validated the entire protocol
against a software-only fake backend that mimics virglrenderer's responses, so
encoding correctness is tested independently of QEMU integration. The
end-to-end results — virgl pixel-correct frames, and `llama.cpp` GPU inference at
roughly 160 tok/s decode in the same-run appliance — were then obtained on real
QEMU with a real host GPU, keeping the two kinds of claim separate.

== Applications: small ports, real workloads <sec:impl-apps>

VOGUE provides the missing system pieces; the application code stays upstream.
@tbl:app-ports lists the four ports and what each proves. The line counts are
VOGUE-owned glue — ABI shims and bridge code — not the vendored application body.

#figure(
  table(
    columns: (auto, auto, auto, 1.4fr),
    align: (left, left, left, left),
    table.header([App], [Role], [VOGUE glue], [What it proves]),
    [`kmscube`], [minimal graphics demo], [593 LOC / 3 ABI shims],
      [unchanged app draws inside our Unikraft system],
    [`glmark2`], [OpenGL app bring-up], [155 LOC / 3 ABI shims],
      [a larger GL app can start and run on our path],
    [`vkmark`], [Vulkan app bring-up], [62 LOC / 2 Vulkan shims],
      [a Vulkan app can start and load its scenes],
    [`llama.cpp`], [real compute workload], [upstream bridge + dispatch],
      [LLM inference runs inside Unikraft],
  ),
  caption: [Application ports. LOC is VOGUE-owned glue, not the application body.],
) <tbl:app-ports>

=== Why compute came before graphics

Although graphics is the more intuitive GPU workload, compute was the practical
first target to reach the _real_ GPU, because the Vulkan compute pipeline is a
much shorter path than the graphics pipeline. Compute has one main create
structure (`VkComputePipelineCreateInfo`), one shader stage, a simple data-in /
data-out flow, and no screen output. Graphics requires
`VkGraphicsPipelineCreateInfo` plus ten or more fixed-function state blocks, at
least vertex and fragment stages that must all match, and management of render
targets and presentation. Compute fits a one-purpose appliance; graphics expects
a window and display stack. After proving the graphics basics on the 2D and
virgl paths, compute was the faster route to a real GPU workload.

=== The compute path reaches the real GPU

For `llama.cpp` the full chain is: upstream `llama.cpp` bench/server →
`ggml-vulkan` → `libukggml_vk` static dispatch → `libukvenus` encoder and Venus
protocol → `libukvirtgpu_drm` + `libukvirtio_gpu` → QEMU `virtio-gpu-gl` +
virglrenderer → host Vulkan driver on a Tesla V100. @tbl:compute-libs lists the
libraries this path adds and their source sizes.

#figure(
  table(
    columns: (auto, 1fr, auto),
    align: (left, left, right),
    table.header([Library], [Role], [Source LOC]),
    [`libukggml_vk`], [static ggml-vulkan dispatch], [2,146],
    [`libukvenus`], [(mostly generated) Venus encoder/protocol + ring glue], [96,690],
    [`libukvirtgpu_drm`], [Linux virtgpu DRM ioctl shim], [638],
    [`libukvirtio_gpu`], [VirtIO-GPU frontend + virgl/controlq], [2,246],
    [`libukvk_icd`], [Vulkan ICD bootstrap shim], [235],
  ),
  caption: [Libraries on the Vulkan compute path. `libukvenus` is mostly
    _generated_ from the upstream Venus protocol and Vulkan API metadata; the
    hand-written part is the Unikraft-side ring and glue code.],
) <tbl:compute-libs>

The size of `libukvenus` is almost entirely generated code: the encoder and
protocol tables are produced from the upstream Venus protocol and Vulkan API
metadata, and only the Unikraft-side ring and glue code is hand-written. This is
how a single guest library can cover the breadth of the Vulkan API surface while
remaining within the spirit of the small-substrate goal — the trusted,
hand-audited surface is small even though the generated surface is large.
