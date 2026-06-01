= VirtIO-GPU Frontend Design <sec:frontend>

The VirtIO-GPU frontend (`libukvirtio_gpu`) is the lowest-level VOGUE component. It speaks directly to the virtual device with no Linux DRM driver, no Mesa userspace stack, and no private host protocol. Its design follows two rules: reuse Unikraft transport infrastructure where it already exists, and keep each stronger graphics claim behind an explicit evidence row.

== Device Discovery and Initialization

On Unikraft, PCI device discovery uses the existing Unikraft PCI and virtio bus libraries. Legacy and transitional VirtIO devices can be reached through the current `libvirtio_pci` path; QEMU's modern VirtIO-GPU Venus devices present modern PCI IDs such as `0x1050`. This artifact carries a Unikraft `libvirtio_pci` patch for modern VirtIO-PCI discovery, and the current evaluation-host matrix records `xport.qemu-vgpu` as PASS after the QEMU GL/Venus probe reaches the guest driver with row-compatible evidence.

When the transport accepts the device, the driver performs five initialization steps. First, it reads PCI BARs and maps the VirtIO common configuration structure through the Unikraft virtio API. Second, it negotiates features, accepting the virgl 3D mode, EDID display-info, resource-blob, context-init, blob-alignment, and host-visible-memory features only when offered by the device; each feature is enabled only when its preconditions are also met (e.g., `F_BLOB_ALIGNMENT` requires `F_RESOURCE_BLOB`, and `F_CONTEXT_INIT` requires `F_VIRGL`). Third, it allocates the control queue and cursor queue. Fourth, it sends a display-info query to learn the display resolution and scanout count. Finally, it optionally fetches EDID display data if the corresponding feature was negotiated.

The initialization sequence follows the VirtIO specification @virtio-spec Section 5.7. Feature bits that are not understood are not acknowledged, preserving forward compatibility and avoiding false support claims.

== Resource Management

VirtIO-GPU resources are the fundamental unit of host-visible graphics state. A 2D resource represents a rectangular pixel buffer held by the host; the guest populates it via a host-transfer command and controls what is displayed via scanout-binding and resource-flush commands.

@tab:resource-lifecycle describes the VOGUE resource lifecycle for a 2D framebuffer, showing the guest-side operation issued at each step and the expected device response.

#figure(
  table(
    columns: (auto, 1.6fr, 1.4fr),
    inset: 4pt,
    align: (center, left, left),
    table.header([Step], [Guest operation], [Device response]),
    [1], [Allocate host 2D resource (`RESOURCE_CREATE_2D`)], [OK, no data],
    [2], [Attach guest DMA pages (`RESOURCE_ATTACH_BACKING`)], [OK, no data],
    [3], [Application fills the DMA buffer], [—],
    [4], [Push framebuffer to host (`TRANSFER_TO_HOST_2D`)], [OK, no data],
    [5], [Bind resource to display head (`SET_SCANOUT`)], [OK, no data],
    [6], [Present frame (`RESOURCE_FLUSH`)], [OK, no data],
  ),
  caption: [VirtIO-GPU 2D resource lifecycle. Each step maps to one protocol command; steps 4–6 repeat every frame.],
) <tab:resource-lifecycle>

Resource IDs are managed by a simple monotonically increasing counter. Resources are persistent during normal operation; teardown uses explicit unref/detach commands.

== 3D, Blob, and Venus-Readiness Commands

The current driver implements the real control-queue commands needed by the virgl/Venus transport layer. @tab:venus-surface separates what is implemented from what remains blocked.

#figure(
  table(
    columns: (1.1fr, 1.2fr, 1.7fr),
    inset: 4pt,
    align: (left, left, left),
    table.header([Surface], [Status], [Claim boundary]),
    [`CTX_CREATE`, `CTX_DESTROY`], [implemented], [creates/destroys transport contexts; not a rendering proof],
    [`CTX_ATTACH_RESOURCE`, `CTX_DETACH_RESOURCE`], [implemented], [binds resources to contexts; requires accepted PCI transport for QEMU run],
    [`SUBMIT_3D`], [implemented], [submits opaque command buffers; full rendering still needs same-run frame proof],
    [`RESOURCE_CREATE_BLOB`, `RESOURCE_UNREF`], [implemented], [blob resource transport for Venus/hostmem readiness],
    [`RESOURCE_ASSIGN_UUID`], [implemented], [resource identity command for external sharing protocols],
    [`RESOURCE_MAP_BLOB`, `RESOURCE_UNMAP_BLOB`], [implemented], [host-visible mapping surface; Venus ring protocol passes native tests; frame proof still pending],
    [`F_BLOB_ALIGNMENT` (bit 5)], [implemented], [negotiated when host offers it; `blob_alignment` read from config space at offset 16; blob sizes aligned before `RESOURCE_CREATE_BLOB`; falls back to 4096 if value is zero or not a power-of-two],
    [QEMU/Venus execution], [`pass`], [evaluation-host transport PASS requires a complete row-compatible QEMU GL/Venus probe artifact],
  ),
  caption: [Current VirtIO-GPU 3D/Venus surface. Implemented transport commands are necessary but not sufficient for a Vulkan application claim.],
) <tab:venus-surface>

This separation is central to the paper's methodology. The implementation may legitimately pass ABI and static-readiness gates while the system-level QEMU/Venus row remains blocked. A future acceleration pass requires a real appliance to invoke the implemented ring protocol and submit a valid guest command stream that produces same-run frame evidence.

== Scanout and Display Configuration

The virtual display is configured via the scanout-binding command, which binds a resource region to a physical scanout head. VOGUE supports a single scanout (scanout ID 0), matching the default QEMU VirtIO-GPU configuration. The resolution is taken from the display-info query response.

For the 2D path, every frame ends with a resource flush covering the full surface rectangle. No damage tracking or partial-rectangle optimization is implemented, which is sufficient for the correctness goals of this work and keeps the Unikraft path small.

== Capsets and Feature Discovery

When 3D mode is negotiated, VOGUE performs capability discovery by querying capset metadata and retrieving capset payloads. Capset discovery is an availability signal for virgl/Venus host support; it is not itself a rendering result. The evaluator therefore reports capset/ABI readiness separately from K1 accelerated rendering.

== Fence Synchronization

Every host-transfer and resource-flush command may optionally include a fence. VOGUE attaches a fence flag to commands that must complete before the next frame begins. Fence polling uses a cooperative busy-wait limited to 1,000 iterations, after which the wait returns with a timeout indicator. This conservative limit prevents infinite spin loops on misconfigured devices while ensuring frame completion in the normal case.

The VirtIO-GPU specification @virtio-spec states that fences are required for correct ordering of transfers and flushes. Omitting fences risks displaying partially updated frames or corrupted scanout data. Our implementation always fences the resource-flush command.
