#import "@preview/fletcher:0.5.8" as fletcher: diagram, edge, node

#let ink = luma(25)
#let guest-fill = rgb("f4f6f9")
#let host-fill  = rgb("f2f2f2")
#let pass-fill  = rgb("edf3ed")
#let subst-fill = rgb("f7f3ea")
#let blk-fill   = rgb("fafafa")

#let solid      = 0.8pt + ink
#let thin-solid = 0.65pt + ink
#let dashed     = (paint: ink, thickness: 0.65pt, dash: "dashed")
#let bnd-stroke = 1.1pt + ink

#let lbl(a, b) = stack(
  spacing: 0.45em,
  align(center)[#text(weight: "bold", size: 6.8pt)[#a]],
  align(center)[#text(size: 6pt)[#b]],
)

#let gnode = node.with(width: 52mm, inset: 3pt, corner-radius: 2pt)
#let snode = node.with(width: 32mm, inset: 3pt, corner-radius: 2pt)
#let bnd_node = node.with(fill: "none", stroke: bnd-stroke, inset: 7pt, corner-radius: 3pt)
#let bnd_title(t) = stack(align(left + top)[#text(weight: "bold", size: 8.5pt)[#t]])

#figure(
  scale(x: 84%, y: 84%, reflow: true)[
  #diagram(
    spacing: (0.9em, 0.7em),
    cell-size: (23mm, 10mm),
    edge-stroke: 0.75pt + ink,
    mark-scale: 65%,

    // ── Graphics stack (column 0) ──────────────────────────────────────────

    gnode((0,0),
      lbl([Application ports],
          [kmscube · glmark2 · vkmark]),
      fill: pass-fill, stroke: solid, name: <apps>),

    gnode((0,1),
      lbl([libukegl / libuksdl2\_shim],
          [EGL · GLES2 · GBM · DRM shim · SDL2 surface]),
      fill: pass-fill, stroke: solid, name: <shim>),

    gnode((0,2),
      lbl([libukswrender],
          [CPU BGRA rasterizer · 264 LoC]),
      fill: pass-fill, stroke: solid, name: <swrender>),

    gnode((0,3),
      lbl([libukvirtgpu\_drm · libukvk\_icd],
          [DRM ioctl shim → VirtIO-GPU · Vulkan ICD bootstrap]),
      fill: pass-fill, stroke: solid, name: <g5g6>),

    gnode((0,4),
      lbl([libukvenus · virgl\_encoder.c],
          [Venus encoder + ring · minimal virgl encoder]),
      fill: pass-fill, stroke: solid, name: <venus>),

    gnode((0,5),
      lbl([libukvirtio\_gpu],
          [2D · ctx · SUBMIT\_3D · blob · UUID · map/unmap · fence · capset]),
      fill: pass-fill, stroke: solid, name: <core>),

    gnode((0,6),
      lbl([libukdma],
          [DMA alloc · scatter-gather descriptor]),
      fill: pass-fill, stroke: solid, name: <dma>),

    gnode((0,7),
      lbl([Unikraft PCI / virtio],
          [reused transport + modern VirtIO-PCI support]),
      fill: pass-fill, stroke: solid, name: <transport>),

    // ── LLAMA side chain (column 1.75) ────────────────────────────────────

    snode((1.75,0),
      lbl([LLAMA apps],
          [app-llama · bench · full]),
      fill: subst-fill, stroke: thin-solid, name: <llama-apps>),

    snode((1.75,1),
      lbl([libukmodel · libukggml],
          [GGUF model parse + ggml]),
      fill: subst-fill, stroke: thin-solid, name: <llama-libs>),

    snode((1.75,2),
      lbl([libukllama + ggml CPU],
          [CPU inference baseline]),
      fill: subst-fill, stroke: thin-solid, name: <llama-cpu>),

    snode((1.75,4),
      lbl([ggml-vulkan over Venus],
          [SUBMIT\_3D tensor offload → host GPU]),
      fill: pass-fill, stroke: solid, name: <ggml-vgpu>),

    // ── QEMU host ─────────────────────────────────────────────────────────

    gnode((0,9),
      lbl([QEMU 11.0 virtio-gpu-gl-pci],
          [evaluation host]),
      fill: pass-fill, stroke: solid, name: <qemu>),

    gnode((0,10),
      lbl([virglrenderer + Venus host backend],
          [virgl 3D · Vulkan/Venus host backend]),
      fill: pass-fill, stroke: solid, name: <virgl-host>),

    gnode((0,11),
      lbl([accelerated frame output],
          [same-run SUBMIT\_3D + pixel proof]),
      fill: pass-fill, stroke: solid, name: <k1>),

    // ── Edges: graphics stack ─────────────────────────────────────────────
    edge(<apps>,      <shim>,      "->"),
    edge(<shim>,      <swrender>,  "->"),
    edge(<swrender>,  <g5g6>,      "->"),
    edge(<g5g6>,      <venus>,     "->"),
    edge(<venus>,     <core>,      "->"),
    edge(<core>,      <dma>,       "<|-|>"),
    edge(<dma>,       <transport>, "<|-|>"),

    // ── Edges: LLAMA side chain ───────────────────────────────────────────
    edge(<llama-apps>, <llama-libs>, "->"),
    edge(<llama-libs>, <llama-cpu>,  "->"),
    edge(<llama-cpu>,  <ggml-vgpu>,  "->"),
    edge(<ggml-vgpu.west>, <core.east>, "->",
         label: text(size: 5.5pt)[Vulkan / Venus], label-side: right),

    // ── Edges: host ───────────────────────────────────────────────────────
    edge(<transport>,   <qemu>,       "<|-|>"),
    edge(<qemu>,        <virgl-host>, "->"),
    edge(<virgl-host>,  <k1>,         "->"),

    // ── Boundary boxes ────────────────────────────────────────────────────
    bnd_node(
      enclose: (<apps>, <shim>, <swrender>, <g5g6>, <venus>, <core>, <dma>,
                <transport>, <llama-apps>, <llama-libs>, <llama-cpu>, <ggml-vgpu>),
      fill: guest-fill, name: <guest-box>,
      bnd_title([Unikraft Guest VM]),
    ),
    bnd_node(
      enclose: (<qemu>, <virgl-host>, <k1>),
      fill: host-fill, name: <host-box>,
      bnd_title([Host / QEMU]),
    ),
  )
  ],
  caption: [
    The VOGUE guest software stack. The left column is the 2D display path (CPU software render through `libukswrender`) together with the Vulkan/Venus acceleration substrate; the right column is the `llama.cpp` compute side-chain that reaches real GPU inference on the host. Both paths converge on `libukvirtio_gpu` and the Unikraft PCI/virtio transport, then cross to the shared host bridge (QEMU `virtio-gpu-gl-pci` → virglrenderer → host GPU driver).
  ],
) <fig:vogue-arch>
