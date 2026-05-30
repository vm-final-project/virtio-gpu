= Design Goals and Challenges <sec:design-goals>

VOGUE is built around one observation: for many cloud graphics appliances the hard requirement is not a complete desktop graphics stack, but a trustworthy path from application pixels to a virtual display with an upgrade path to virgl and Venus. The design therefore optimizes for a narrow data plane, explicit compatibility contracts, and evidence labels that prevent accidental promotion of software-render or static-readiness results into GPU claims.

== Goals
We have five design goals to guide our implementation and evaluation:

*DG1 — Standards-based device interface.* Use the OASIS VirtIO-GPU protocol. Avoid bespoke host protocols; the virtio-gpu wire format is stable and supported by QEMU, crosvm, and cloud-hypervisor.

*DG2 — Small trusted graphics substrate.* Keep the guest-side graphics substrate small enough to audit. VOGUE replaces Linux DRM/KMS, GBM, EGL platform glue, Mesa, and display-server assumptions with a few micro-libraries and explicit stubs.

*DG3 — Application-source compatibility at the boundary.* Preserve the source-level structure of target applications where it matters: _kmscube_ should still look like a DRM/GBM/EGL/GLES2 program, and the porting layer should absorb Unikraft-specific adaptation.

*DG4 — Progressive validation.* Validate the stack bottom-up: native command sequencing, software rasterization, shim-level application compatibility, real control-queue ABI coverage, QEMU device discovery, then virgl/Venus rendering.

*DG5 — Evidence-gated claims.* Separate software-render evidence (gfx.kmscube.sw), real-driver protocol-readiness evidence (proto.real-driver/vk.readiness), QEMU transport blockers (xport.qemu-vgpu), and accelerated rendering evidence (K1). A successful capset or ABI probe is useful, but it is not a rendering result; a software frame is useful, but it is not acceleration.

== Non-Goals

- Full Linux DRM/KMS ioctl compatibility.
- A full Mesa or shader-compiler port.
- A complete Mesa/Venus Vulkan guest-driver port, Wayland/X11, multi-window compositing, or multi-GPU scheduling. VOGUE documents and tests the VirtIO-GPU blob/controlq substrate needed by Venus, but does not claim Vulkan application execution.
- Vendor-native GPU drivers, SR-IOV passthrough, or confidential GPU isolation.
- Claiming graphics acceleration before a real virgl/Venus command stream renders frames with verified virtio-gpu acceleration.

== Design Invariants

@tab:invariants states the invariants we use to keep a small shim honest. The important point is that every compatibility shortcut must have a bounded failure mode: a stub may return a safe sentinel, but it must not silently imply a stronger capability.

#figure(
  table(
    columns: (1fr, 1.5fr, 1.5fr),
    inset: 4pt,
    align: (left, left, left),
    table.header([Invariant], [Enforced by], [Why it matters]),
    [I1: command order],
    [native fake backend rejects invalid lifecycle],
    [Prevents black-screen bugs from passing unit tests.],

    [I2: fenced presentation],
    [TRANSFER and FLUSH carry completion fences],
    [Avoids reading success from asynchronous submission alone.],

    [I3: explicit stubs],
    [DRM/GBM functions document sentinel behavior],
    [Prevents compatibility from becoming an implicit Linux port claim.],

    [I4: row-labeled claims],
    [evaluator emits gfx.kmscube.sw, proto.real-driver, xport.qemu-vgpu, vk.readiness, and K1 separately],
    [Prevents software frames or ABI checks from being reported as GPU rendering.],

    [I5: Unikraft reuse],
    [library dependencies stay on existing PCI, virtio, allocator, mmap/POSIX support],
    [Prevents the project from reimplementing existing Unikraft modules.],
  ),
  caption: [Design invariants used to make a small compatibility layer reviewable.],
) <tab:invariants>

== Challenges

We met six challenges in building VOGUE:

*C1 — API dependency chain.* Applications like _kmscube_ call into the GBM, EGL, and GLES2 API surfaces — device creation, surface setup, window surface construction, buffer swapping, and numerous GL entry points. Each call must either perform real VOGUE work or fail safely without relying on Linux device files.

*C2 — VirtIO-GPU command sequencing.* The 2D protocol requires a strict sequence: resource creation → backing attachment → scanout binding → host transfer → resource flush. Reordering produces no visible frame or a device error.

*C3 — Fence synchronization.* In a cooperative Unikraft environment, fence polling must be bounded: long enough to observe device completion after each transfer and flush, but not an unbounded spin loop that can hide a broken queue.

*C4 — DMA memory model.* VirtIO-GPU resources are host-private. The guest must copy software-rendered pixels into DMA-backed memory and explicitly transfer them to the host. This copy is acceptable for a correctness substrate but becomes the central bottleneck for interactive workloads.

*C5 — Minimality versus compatibility.* Too narrow a shim fails to run real programs; too broad a shim recreates Mesa and DRM. VOGUE chooses the smallest API subset that supports concrete application rows, then names unsupported behavior as future work.

*C6 — Evaluation integrity.* A high-level systems paper must make negative space visible: missing baselines, missing virgl/Venus command streams, QEMU transport blockers, and missing full-benchmark runs must appear as blocked rows rather than disappearing from the evaluation.
