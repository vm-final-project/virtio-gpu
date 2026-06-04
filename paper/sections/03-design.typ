= Design <sec:design>

== Key principle: a single-client GPU driver

The design rests on one observation. A unikernel is one process with one
purpose and one address space, so the GPU device has exactly _one_ client. On
Linux the GPU is a shared resource that several processes may use at once, which
is why the DRM (Direct Rendering Manager) subsystem exists: it arbitrates
multiplexed access, switching contexts, locking resources, and managing
permissions. None of that is needed when there is only one client.

VOGUE therefore discards DRM multiplexing wholesale and replaces it with a
minimal exclusive-client driver. @tbl:linux-vs-vogue quantifies what this buys.

#figure(
  table(
    columns: (1fr, 1.3fr, 1.2fr),
    align: (left, center, center),
    table.header([Dimension], [Linux virtio-gpu], [VOGUE]),
    [Graphics stack], [\~3M LoC], [\~5K LoC],
    [Address space], [Kernel + user], [Single],
    [GPU clients], [Multiple (needs arbiter)], [Exclusive single client],
    [Command path], [app → ioctl → kernel → virtio], [app → direct virtio ring],
  ),
  caption: [Single-client ownership collapses the graphics stack by roughly
    600× and shortens the command path to a direct virtqueue write.],
) <tbl:linux-vs-vogue>

Two consequences follow immediately. With no DRM multiplexing logic to carry,
the core driver is about 1,650 lines and stays auditable. With no user/kernel
boundary, submitting a command is a direct function call onto the virtqueue
rather than an `ioctl` that crosses into the kernel, removing a context switch
from the hot path.

== System architecture

VOGUE is organized into four layers, summarized in @tbl:vogue-layers and
sketched against the host in @fig:vogue-arch. Each layer has a precise role.

#figure(
  table(
    columns: (auto, 1fr),
    align: (center + horizon, left),
    table.header([Layer], [Components]),
    [L1 Application], [`kmscube` / `glmark2` / `llama.cpp` (_unmodified_)],
    [L2 Compat], [`libukegl` + `libukswrender` (2D display) · `libukggml_vk` (Vulkan dispatch)],
    [L3 Venus], [`libukvenus` (Vulkan → Venus binary)],
    [L4 VirtIO], [`libukvirtio_gpu` + `libukdma` (→ Unikraft virtqueue → QEMU)],
  ),
  caption: [The four guest layers of VOGUE.],
) <tbl:vogue-layers>

*L1 — Application.* Upstream application source, not a line modified, compiled
directly into the unikernel image. Depending on the workload it uses EGL/GLES2
(2D display), Vulkan (compute), or the virgl encoder API (3D) of the layer
below.

*L2 — Compat.* An API-compatibility layer that presents the surface an
application expects on Linux and routes those calls into VOGUE. `libukegl`
provides an EGL/GLES2/GBM/DRM stub whose `eglSwapBuffers` drives the software
renderer and then issues 2D VirtIO commands; `libukswrender` is a 264-line CPU
rasterizer that fills geometry into a DMA buffer; and `libukggml_vk` is a static
Vulkan dispatch table that replaces the Linux Vulkan loader's run-time `dlopen`
of an ICD, letting `llama.cpp` route straight to L3. The components differ
entirely by path, and the 3D virgl path does not use this layer at all.

*L3 — Venus.* The Vulkan-command serialization layer. `libukvenus` turns Vulkan
API calls into a Venus PACKED binary stream and is logically equivalent to
Mesa's Venus guest driver, without the rest of Mesa. It exists only on the
Vulkan compute path; the 2D and virgl paths skip it.

*L4 — VirtIO.* The guest-side implementation of the VirtIO-GPU protocol and the
single point where all three paths converge. `libukvirtio_gpu` packs each path's
commands into VirtIO-GPU wire format — 2D commands (`RESOURCE_CREATE_2D`,
`TRANSFER_TO_HOST_2D`, `SET_SCANOUT`, `RESOURCE_FLUSH`), 3D/Venus commands
(`CTX_CREATE`, `SUBMIT_3D`, `RESOURCE_CREATE_BLOB`, `RESOURCE_MAP_BLOB`), and the
minimal virgl encoding in `virgl_encoder.c` — and `libukdma` provides the
DMA-capable backing memory. The virtqueue itself (descriptor rings, doorbell,
PCI BAR mapping) is internal transport pulled in through Unikraft's virtio bus,
not a separate layer.

#include "../figures/vogue-arch.typ"

On the host, the guest's VirtIO-GPU commands pass through QEMU's
`virtio-gpu-gl-pci` device to virglrenderer, which calls the host's real Vulkan
or OpenGL driver to do the actual GPU work. Because that host bridge is left
untouched, VOGUE is compatible with existing hypervisors out of the box. The
thinness shows up in the artifact: the guest image is *292 KB* (a Linux kernel
is 9.2 MB — 31× larger) and boot takes *10–11 ms* (Linux plus initramfs takes
857–861 ms — 80× slower), because there is no kernel module system, no DRM
subsystem, no init process, and no dynamic loading to wait on.

== VOGUE versus Linux, layer by layer

Every VOGUE layer has a Linux counterpart; some are logically equivalent (≡) and
some are replaced by a leaner equivalent (→), as shown in @tbl:layer-by-layer.
At L1 the application is unchanged. At L2, Linux ships real `libEGL`, `libgbm`,
and `libdrm` plus a Vulkan loader that `dlopen`s an ICD at run time; VOGUE
substitutes stubs (`libukegl`, `libukswrender`) and a static dispatch table
(`libukggml_vk`), eliminating dynamic loading. At L3, Mesa's Venus driver
serializes Vulkan into Venus binary, and `libukvenus` does the same encoding
without depending on the rest of Mesa. At L4, Linux enters the kernel through a
`/dev/dri` `ioctl` before reaching the virtqueue, whereas `libukvirtio_gpu`
drives the Unikraft virtqueue directly, with no `ioctl` path. The host side —
QEMU, virglrenderer, and the host GPU driver — is shared and unchanged.

#figure(
  table(
    columns: (auto, 1fr, auto, 1fr),
    align: (left, left, center, left),
    table.header([Layer], [Linux VM], [], [VOGUE Unikraft]),
    [L1 App], [app (`kmscube` / `llama.cpp`)], [≡], [app (unmodified upstream)],
    [L2 Compat], [`libEGL` + `libgbm` + `libdrm` + Vulkan loader], [→],
      [`libukegl` · `libukswrender` · `libukggml_vk`],
    [L3 Venus], [Mesa Venus driver], [→], [`libukvenus` (same encoding, no Mesa)],
    [L4 VirtIO], [virtio-gpu + kernel virtio/PCI (`/dev/dri` ioctl)], [→],
      [`libukvirtio_gpu` (direct virtqueue, no ioctl)],
    [Host], [QEMU virtio-gpu → virglrenderer → host GPU driver], [shared],
      [QEMU virtio-gpu → virglrenderer → host GPU driver],
  ),
  caption: [≡ same · → replaced with a leaner VOGUE equivalent. The host bridge
    is shared, which is why VOGUE drops into existing hypervisors.],
) <tbl:layer-by-layer>

== Three execution paths

The four layers compose differently for the three workloads VOGUE supports;
@tbl:three-paths places each path against the layers. The differences are the
point: the paths reach the GPU (or not) by different routes, and L2 and L3 are
absent on some of them by design.

#figure(
  table(
    columns: (auto, 1fr, 1fr, 1fr),
    align: (left, left, left, left),
    table.header([], [2D Display], [3D Rendering], [Vulkan Compute]),
    [L1 App], [`kmscube` SW · `glmark2`], [`kmscube` virgl], [`llama.cpp`],
    [L2 Compat], [`libukswrender` (CPU)], [— (direct encoder)], [`libukggml_vk` (Vk dispatch)],
    [L3 Venus], [—], [—], [`libukvenus` (→ Venus)],
    [L4 VirtIO], [`TRANSFER`/`SET_SCANOUT`/`FLUSH`], [`SUBMIT_3D` (virgl)], [`SUBMIT_3D` (Venus)],
    [GPU], [none (CPU only)], [host GPU → buffer], [host GPU → buffer (no display)],
  ),
  caption: [Three execution paths across the four layers.],
) <tbl:three-paths>

The *2D display* path never touches the GPU: `libukswrender` draws pixels into
memory on the CPU and `libukvirtio_gpu` pushes them to QEMU with 2D display
commands. The *Vulkan compute* path is the full stack: `libukggml_vk` (L2)
catches `llama.cpp`'s Vulkan calls, `libukvenus` (L3) serializes them, and
`SUBMIT_3D` carries the result to virglrenderer, which runs the matrix work on
the host GPU and writes results back — with no image ever displayed.

The *3D virgl* path is the interesting one: it jumps from L1 directly to L4,
skipping both L2 and L3, and it does so for two independent reasons. It skips L2
because a compatibility shim needs a public API boundary to stand on. Vulkan has
one (`vkCreateDevice`, `vkQueueSubmit`), so `libukggml_vk` can intercept there;
but the Gallium command stream is a Mesa-_internal_ concept, not a public API.
The virgl application produces Gallium binary itself, calling
`uk_virgl_encode_create_surface()` and friends from the L4 encoder header
directly, so there is no seam at which a compat layer could insert itself. It
skips L3 because virgl speaks the Gallium protocol, not Venus, so `libukvenus` is
simply inapplicable; and because VOGUE's virgl encoder is deliberately tiny —
three commands (create-surface, set-framebuffer, clear), about eighty lines,
enough to clear the screen to a color. A complete virgl encoder would have to
cover the entire OpenGL state machine (Mesa's runs to thousands of lines), which
would violate the small-substrate goal below. At this size the encoder belongs
inside L4, and the corresponding evidence goals are transport-level proofs, not
a claim of full OpenGL acceleration. A full virgl serializer at L3 is left to
future work.

== Design goals

The design follows five goals, the last of which is as much a methodological
commitment as a technical one.

- *DG1 — Standards-based.* Follow the OASIS VirtIO-GPU specification so the guest
  is compatible with QEMU, crosvm, and cloud-hypervisor; invent no private
  protocol.

- *DG2 — Small trusted substrate.* Keep the guest graphics stack around 5,000
  lines so the trusted computing base is fully auditable, which matters for
  security-sensitive unikernel deployments.

- *DG3 — Application source compatibility.* Run `kmscube`, `llama.cpp`, and the
  rest from _unmodified_ upstream source, which forces the compat shims to
  faithfully emulate the EGL, GLES2, GBM, and DRM surfaces these apps expect.

- *DG4 — Progressive validation.* Validate bottom-up — native tests (no QEMU) →
  software render → ABI checks → QEMU transport → accelerated render — so a
  failure can be localized to a specific layer.

- *DG5 — Evidence-gated claims.* Strictly distinguish claims at different
  abstraction levels. A `gfx.kmscube.sw` pass (software rendering) does _not_
  imply virgl or GPU acceleration; a `proto.real-driver` pass (static ABI check)
  does _not_ imply that QEMU executes the stream; a host Linux baseline is _not_
  evidence of Unikraft runtime performance. This discipline is what keeps the
  evaluation honest, and we carry it through #ref(<sec:eval>).
