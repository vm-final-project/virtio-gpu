#import "@preview/fletcher:0.5.8" as fletcher: diagram, edge, node

#let ink    = luma(25)
#let pfill  = rgb("edf3ed")   // PASS
#let sfill  = rgb("f6f2ea")   // PASS-substrate
#let bfill  = rgb("fafafa")   // BLOCKED (dashed)
#let solid  = 0.8pt + ink
#let thin   = 0.65pt + ink
#let dashed = (paint: ink, thickness: 0.65pt, dash: "dashed")

#let lbl(row, status, claim) = stack(
  spacing: 0.35em,
  align(center)[#text(weight: "bold", size: 6.6pt)[#row]],
  align(center)[#text(size: 5.8pt, style: "italic")[#status]],
  align(center)[#text(size: 5.5pt)[#claim]],
)

#let pnode = node.with(width: 31mm, inset: 3pt, corner-radius: 2pt, fill: pfill, stroke: solid)
#let snode = node.with(width: 31mm, inset: 3pt, corner-radius: 2pt, fill: sfill, stroke: thin)
#let bnode = node.with(width: 31mm, inset: 3pt, corner-radius: 2pt, fill: bfill, stroke: dashed)

#let bnd_node = node.with(fill: "none", stroke: 1.1pt + ink, inset: 6pt, corner-radius: 3pt)
#let bnd_title(t) = stack(align(left + top)[#text(weight: "bold", size: 7.5pt)[#t]])

#figure(
  scale(x: 86%, y: 86%, reflow: true)[
  #diagram(
    spacing: (0.85em, 0.6em),
    cell-size: (19mm, 9.5mm),
    edge-stroke: 0.72pt + ink,
    mark-scale: 62%,

    // ── Column 0: Core 2D / display path ──────────────────────────────────
    pnode((0,0), lbl([N2D], [PASS], [DMA alloc · 2D resource lifecycle · 3 frames]),   name: <n2d>),
    pnode((0,1), lbl([G0],  [PASS], [Full VirtIO-GPU API: 5 capsets · SUBMIT\_3D · blobs]), name: <g0>),
    pnode((0,2), lbl([K1sw],[PASS], [kmscube source; swrender + VirtIO-GPU 2D scanout]), name: <k1sw>),
    pnode((0,3), lbl([G1sw],[PASS], [glmark2 app-source compat PASS]), name: <g1sw>),
    pnode((0,4), lbl([VABI],[PASS], [ABI/static: 3D · blob · UUID · map/unmap submit]),  name: <vabi>),
    bnode((0,5), lbl([VQEMU],[blocked],[current probe incomplete · rerun QEMU GL/Venus]),    name: <vqemu>),
    bnode((0,6), lbl([K1],  [blocked], [missing PASS marker · frame proof not current]),       name: <k1>),

    // ── Column 1: Vulkan / Venus path ──────────────────────────────────────
    pnode((1.6,2), lbl([G5],[PASS], [libukdrm\_virtgpu: 44-check DRM ioctl replay]),    name: <g5>),
    pnode((1.6,3), lbl([G6],[PASS], [libukvolkan\_icd: 27-check Vulkan ICD bootstrap]), name: <g6>),
    pnode((1.6,4), lbl([VSMOKE/VKMARK],[PASS], [Venus capset · 10 vkmark scenes]),      name: <vkmark>),
    pnode((1.6,5), lbl([VENUS-ENC],[PASS], [PACKED wire fmt · array\_size · 3 cmds]),   name: <venc>),
    pnode((1.6,6), lbl([VENUS-RING],[PASS], [ring\_proto 24 · head/tail/status · wrap]), name: <vring>),
    pnode((1.6,7), lbl([VIRGL-ENC],[PASS], [Gallium virgl encoder · 47 checks PASS]),   name: <virglenc>),
    bnode((1.6,8), lbl([K1 (same)],[blocked],[same blocked row: rerun QEMU frame proof]),      name: <k1b>),

    // ── Column 2: LLAMA compute chain ─────────────────────────────────────
    pnode((3.2,0), lbl([LLAMA0],[PASS], [GGUF parse · 70 chk · virtgpu path documented]),name: <l0>),
    pnode((3.2,1), lbl([LLAMA1],[PASS], [ggml CPU matmul · ~27 GFLOPS real backend]),    name: <l1>),
    snode((3.2,2), lbl([LLAMA2],[PASS-sub],[transformer fwd pass · ~16 GFLOPS · ~41k tok equiv]),name: <l2>),
    bnode((3.2,4), lbl([LLAMA-GPU],[blocked],[ggml-vulkan over Mesa Venus · GPU tensor offload]),  name: <lgpu>),

    // ── Edges: column 0 ───────────────────────────────────────────────────
    edge(<n2d>,  <g0>,   "->"),
    edge(<g0>,   <k1sw>, "->"),
    edge(<k1sw>, <g1sw>, "->"),
    edge(<g1sw>, <vabi>, "->"),
    edge(<vabi>, <vqemu>,"->"),
    edge(<vqemu>,<k1>,   "-->", stroke: dashed),

    // ── Edges: column 1 ───────────────────────────────────────────────────
    edge(<g0>,   <g5>,    "->"),
    edge(<g5>,   <g6>,    "->"),
    edge(<g6>,   <vkmark>,"->"),
    edge(<vkmark>,<venc>, "->"),
    edge(<venc>, <vring>, "->"),
    edge(<vring>,<virglenc>, "->"),
    edge(<virglenc>, <k1b>, "-->", stroke: dashed),
    // K1 from both columns is the same row
    edge(<k1.east>, <k1b.west>, "<->", stroke: thin,
         label: text(size: 5pt)[same K1 row], label-side: center),

    // ── Edges: column 2 ───────────────────────────────────────────────────
    edge(<l0>,   <l1>,   "->"),
    edge(<l1>,   <l2>,   "->"),
    edge(<l2>,   <lgpu>, "-->", stroke: dashed),

    // Cross-column: VQEMU enables both K1 paths
    edge(<vqemu.east>, <vkmark.west>, "->", stroke: thin),

    // ── Boundary boxes ────────────────────────────────────────────────────
    bnd_node(
      enclose: (<n2d>, <g0>, <k1sw>, <g1sw>, <vabi>, <vqemu>, <k1>),
      fill: rgb("f4f6fa"), name: <col0-box>,
      bnd_title([2D / display path]),
    ),
    bnd_node(
      enclose: (<g5>, <g6>, <vkmark>, <venc>, <vring>, <virglenc>, <k1b>),
      fill: rgb("f4f4fa"), name: <col1-box>,
      bnd_title([Vulkan / Venus path]),
    ),
    bnd_node(
      enclose: (<l0>, <l1>, <l2>, <lgpu>),
      fill: rgb("faf6f0"), name: <col2-box>,
      bnd_title([LLAMA compute chain]),
    ),
  )
  ],
  caption: [
    Evidence dependency graph. *Green-tinted* rows are fully PASS; *warm-tinted* rows are pass-substrate; *near-white dashed* rows are BLOCKED. Arrows show prerequisite relationships: no row can be claimed until all upstream rows pass. The three columns represent independent evidence chains that converge at K1 (virgl frame proof): (left) the 2D display path built from N2D→K1sw, (centre) the Vulkan/Venus substrate built from G5/G6 through VENUS-RING and VIRGL-ENC, and (right) the LLAMA compute chain from LLAMA0–2. K1 is the shared blocked row for the first two chains: it requires a current same-run QEMU PASS marker and pixel-frame proof before promotion. Remaining blocked work also includes LLAMA GPU offload and broader accelerated benchmark coverage.
  ],
) <fig:evidence-ladder>
