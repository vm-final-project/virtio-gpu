#import "@preview/fletcher:0.5.8" as fletcher: diagram, edge, node

#let ink     = luma(25)
#let sfill   = rgb("edf3ed")
#let mfill   = rgb("f7f3ea")
#let bfill   = rgb("fafafa")
#let solid   = 0.8pt + ink
#let thin    = 0.65pt + ink
#let dashed  = (paint: ink, thickness: 0.65pt, dash: "dashed")
#let bnd-st  = 1.1pt + ink

#let lbl(a, b) = stack(
  spacing: 0.4em,
  align(center)[#text(weight: "bold", size: 6.8pt)[#a]],
  align(center)[#text(size: 6pt)[#b]],
)
#let lbl1(a) = align(center)[#text(weight: "bold", size: 6.8pt)[#a]]

#let bnd_node = node.with(fill: "none", stroke: bnd-st, inset: 6pt, corner-radius: 3pt)
#let bnd_title(t) = stack(align(left + top)[#text(weight: "bold", size: 8pt)[#t]])

// ── Memory layout sub-diagram (top row of the figure) ─────────────────────
// Rendered as a plain typst table for precise offset labels
#let ring-layout = text(size: 6.5pt,
  table(
    columns: (26mm, 26mm, 26mm, 26mm),
    inset: 4pt,
    align: center,
    stroke: 0.7pt + ink,
    fill: (x, y) => if y == 0 { rgb("e8eef8") } else { rgb("f8f8f8") },
    table.header(
      [*head* (offset 0)], [*tail* (offset 64)], [*status* (offset 128)],
      [*extra* (offset 192)],
    ),
    [host writes head; guest reads to detect consumed commands],
    [guest writes tail to advance ring; host reads to fetch commands],
    [ring state (ready / busy / error); polled by guest spin-wait],
    [reserved / future protocol extension field],
  )
)

#figure(
  stack(
    spacing: 0.8em,

    // ── Part 1: Ring memory layout ────────────────────────────────────────
    align(center)[#text(weight: "bold", size: 7.5pt)[Ring buffer header layout (HOST3D\_GUEST blob, host-visible memory)]],
    ring-layout,

    v(0.3em),

    // ── Part 2: Command-flow diagram ──────────────────────────────────────
    align(center)[#text(weight: "bold", size: 7.5pt)[Mesa ring-buffer protocol command sequence]],
    scale(x: 88%, y: 88%, reflow: true)[
    #diagram(
      spacing: (0.8em, 0.65em),
      cell-size: (20mm, 9.5mm),
      edge-stroke: 0.75pt + ink,
      mark-scale: 63%,

      // Guest-side protocol steps (columns 0-4)
      node((0,0), lbl([vkCreateRingMESA], [cmd 188: ring\_id, size, type, blob handle]),
           width: 28mm, fill: sfill, stroke: solid, name: <cr>),
      node((1,0), lbl([Encode commands], [write Venus Vk cmds into ring buffer circular region]),
           width: 28mm, fill: sfill, stroke: solid, name: <enc>),
      node((2,0), lbl([Store-release tail], [write new tail offset; memory barrier before notify]),
           width: 28mm, fill: sfill, stroke: solid, name: <flush>),
      node((3,0), lbl([vkNotifyRingMESA], [cmd 190: ring\_id + seqno; triggers SUBMIT\_3D]),
           width: 28mm, fill: sfill, stroke: solid, name: <notify>),
      node((4,0), lbl([Spin-poll head], [busy-wait ≤1000 iters for host head ≥ tail (seqno)]),
           width: 28mm, fill: mfill, stroke: solid, name: <wait>),
      node((5,0), lbl([vkDestroyRingMESA], [cmd 189: ring\_id; unregister + zero protocol state]),
           width: 28mm, fill: sfill, stroke: solid, name: <dr>),

      // Edges
      edge(<cr>,     <enc>,    "->"),
      edge(<enc>,    <flush>,  "->"),
      edge(<flush>,  <notify>, "->"),
      edge(<notify>, <wait>,   "->"),
      edge(<wait>,   <dr>,     "->", label: text(size: 5.5pt)[after processing], label-side: right),

      // Back-edge: repeat for next batch
      edge(<wait.south>, (4, 1.2), <enc.south>, "->",
           stroke: dashed,
           label: text(size: 5.5pt)[next batch (re-encode)],
           label-side: right),

      // Host-side annotation (row 1, column 3)
      node((3,1.8),
           lbl([virglrenderer host], [receives SUBMIT\_3D; processes Venus cmds; advances head]),
           width: 40mm, fill: rgb("f2f2f2"), stroke: thin, name: <host>),

      edge(<notify.south>, <host.north>, "->", stroke: thin,
           label: text(size: 5.5pt)[VirtIO-GPU SUBMIT\_3D], label-side: right),
      edge(<host.north>, <wait.south>, "->", stroke: thin,
           label: text(size: 5.5pt)[head advance], label-side: left),

      // Boundary: guest steps
      bnd_node(
        enclose: (<cr>, <enc>, <flush>, <notify>, <wait>, <dr>),
        fill: rgb("f4f6f9"), name: <gbx>,
        bnd_title([Guest VM (libukvenus)])
      ),
      // Boundary: host
      bnd_node(
        enclose: (<host>,),
        fill: rgb("f2f2f2"), name: <hbx>,
        bnd_title([Host / QEMU])
      ),
    )
    ],
  ),
  caption: [
    The Venus ring-buffer protocol. *Top*: the ring header lives in a `HOST3D_GUEST` blob in host-visible memory; cache-line-separated slots hold head, tail, and status so guest and host can synchronise without false sharing, zero-copy. *Bottom*: the command sequence. The guest registers the ring with `vkCreateRingMESA` (cmd 188), encodes Venus commands into the circular data region, publishes a new tail with a store-release, and calls `vkNotifyRingMESA` (cmd 190) to trigger one `SUBMIT_3D`; the host advances head as it consumes commands while the guest spin-polls, and `vkDestroyRingMESA` (cmd 189) tears the ring down. Batching many commands per notification amortises the `SUBMIT_3D` cost.
  ],
) <fig:venus-ring>
