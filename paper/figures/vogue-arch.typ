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
      lbl([App ports — K1sw · G1sw · VKMARK PASS],
          [kmscube · glmark2 · app-vkmark]),
      fill: pass-fill, stroke: solid, name: <apps>),

    gnode((0,1),
      lbl([libukegl / libuksdl2\_shim],
          [EGL · GLES2 · GBM · DRM shim · SDL2 surface — G3 compat PASS]),
      fill: pass-fill, stroke: solid, name: <shim>),

    gnode((0,2),
      lbl([libukswrender],
          [CPU BGRA rasterizer · 264 LoC · K1sw PASS]),
      fill: pass-fill, stroke: solid, name: <swrender>),

    gnode((0,3),
      lbl([G5: libukdrm\_virtgpu · G6: libukvolkan\_icd],
          [DRM ioctls → VirtIO-GPU · Vulkan ICD bootstrap → G5 — PASS]),
      fill: pass-fill, stroke: solid, name: <g5g6>),

    gnode((0,4),
      lbl([libukvenus · virgl\_encoder.c],
          [VENUS-RING PASS (ring\_proto 24) · VIRGL-ENC PASS (47) · VENUS-ENC PASS]),
      fill: pass-fill, stroke: solid, name: <venus>),

    gnode((0,5),
      lbl([libukvirtio\_gpu],
          [2D · ctx · SUBMIT\_3D · blob · UUID · map/unmap · fence · capset]),
      fill: pass-fill, stroke: solid, name: <core>),

    gnode((0,6),
      lbl([uksglist / ukalloc],
          [upstream scatter-gather + uk_posix_memalign backing — N2D PASS]),
      fill: pass-fill, stroke: solid, name: <dma>),

    gnode((0,7),
      lbl([Unikraft PCI / virtio],
          [reused transport + modern VirtIO-PCI support; VQEMU PASS on eval host]),
      fill: pass-fill, stroke: solid, name: <transport>),

    // ── LLAMA side chain (column 1.75) ────────────────────────────────────

    snode((1.75,0),
      lbl([LLAMA apps],
          [app-llama · bench · full]),
      fill: subst-fill, stroke: thin-solid, name: <llama-apps>),

    snode((1.75,1),
      lbl([libukmodel · libukggml],
          [LLAMA0: GGUF parse · 70 chk PASS]),
      fill: subst-fill, stroke: thin-solid, name: <llama-libs>),

    snode((1.75,2),
      lbl([libukllama + ggml CPU],
          [LLAMA1/2: ~16–27 GFLOPS PASS]),
      fill: subst-fill, stroke: thin-solid, name: <llama-cpu>),

    snode((1.75,4),
      lbl([ggml-vulkan over Mesa Venus],
          [SUBMIT\_3D tensor offload → GPU PASS; optimize tg128]),
      fill: pass-fill, stroke: solid, name: <ggml-vgpu>),

    // ── QEMU host ─────────────────────────────────────────────────────────

    gnode((0,9),
      lbl([QEMU 11.0 virtio-gpu-gl-pci],
          [current matrix: VQEMU PASS on eval host]),
      fill: pass-fill, stroke: solid, name: <qemu>),

    gnode((0,10),
      lbl([virglrenderer + Venus host backend],
          [virgl 3D · Vulkan/Venus host backend; ring/frame proof PASS]),
      fill: pass-fill, stroke: solid, name: <virgl-host>),

    gnode((0,11),
      lbl([K1 accelerated frame proof],
          [PASS · same-run SUBMIT\_3D + pixel proof]),
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
    edge(<llama-cpu>,  <ggml-vgpu>,  "-->", stroke: dashed),
    edge(<ggml-vgpu.west>, <core.east>, "-->", stroke: dashed,
         label: text(size: 5.5pt)[future Vulkan/Venus], label-side: right),

    // ── Edges: host ───────────────────────────────────────────────────────
    edge(<transport>,   <qemu>,       "<|-|>"),
    edge(<qemu>,        <virgl-host>, "->"),
    edge(<virgl-host>,  <k1>,         "-->", stroke: dashed),

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
    VOGUE software stack (current state). *Green-tinted* nodes are fully validated (solid border). *Near-white dashed* nodes are future scope. Left column: 2D display path (fully PASS: N2D→K1sw→G1sw) and the Vulkan/Venus acceleration substrate (G5+G6+libukvenus+virgl\_encoder all PASS; K1 frame proof follows the current evidence matrix). Right column: LLAMA compute side-chain, including real QEMU/Venus GPU inference on the evaluation host. Both paths share `libukvirtio_gpu` and the Unikraft PCI/virtio transport.
  ],
) <fig:vogue-arch>
