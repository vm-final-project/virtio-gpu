#import "@preview/fletcher:0.5.8" as fletcher: diagram, edge, node

#let ink = luma(25)
#let guest-fill = rgb("f7f7f7")
#let host-fill = rgb("f2f2f2")
#let box-fill = rgb("ffffff")
#let impl-fill = rgb("eeeeee")
#let future-fill = rgb("fbfbfb")

#let box2(title, subtitle, bold: false) = stack(
  spacing: 0.20em,
  align(center)[
    #text(size: 8pt, weight: if bold { "bold" } else { "regular" })[#title]
  ],
  align(center)[
    #text(size: 7pt)[#subtitle]
  ],
)

#figure(
  diagram(
    node-stroke: 0.9pt,
    edge-stroke: 0.9pt,
    node-corner-radius: 2pt,
    node-fill: box-fill,
    mark-scale: 70%,

    // ============================================================
    // Boundary boxes
    // ============================================================

    // Wider guest box: includes the future virgl node.
    node(
      enclose: ((-0.6, 0.0), (5.2, 5.9)),
      fill: guest-fill,
      stroke: 1pt + ink,
      corner-radius: 3pt,
      name: <guest-box>,
    ),

    node(
      (2.5, 0.25),
      text(size: 9pt, weight: "bold")[Unikraft Guest VM],
      fill: none,
      stroke: none,
    ),

    // Host box starts lower to leave a clean gap between guest and host.
    node(
      enclose: ((-0.6, 6.6), (5.2, 9.3)),
      fill: host-fill,
      stroke: 1pt + ink,
      corner-radius: 3pt,
      name: <host-box>,
    ),

    // Put host title on the left, not in the center arrow path.
    node(
      (0.15, 6.9),
      text(size: 9pt, weight: "bold")[Host / QEMU],
      fill: none,
      stroke: none,
    ),

    // ============================================================
    // Main implemented path
    // ============================================================

    node(
      (2.5, 1.10),
      text(size: 8.5pt, weight: "bold")[Graphics applications],
      name: <app>,
      width: 36mm,
      fill: box-fill,
      stroke: 1pt + ink,
    ),

    node(
      (2.5, 2.25),
      box2([`libukgfx`], [SDL/EGL compatibility layer]),
      name: <gfx>,
      width: 44mm,
      fill: box-fill,
    ),

    node(
      (2.5, 3.55),
      box2([`libukswrender` + `libukdma`], [software rendering + frame buffers]),
      name: <sw>,
      width: 52mm,
      fill: impl-fill,
    ),

    node(
      (2.5, 4.95),
      box2([`libukvirtio_gpu`], [resources, transfers, fences], bold: true),
      name: <vgpu>,
      width: 44mm,
      fill: box-fill,
      stroke: 1pt + ink,
    ),

    // ============================================================
    // Planned GPU path
    // ============================================================

    node(
      (0.75, 3.55),
      box2([`virgl` encoder], [future]),
      name: <virgl>,
      width: 25mm,
      fill: future-fill,
      stroke: (
        paint: ink,
        thickness: 0.9pt,
        dash: "dashed",
      ),
    ),

    // ============================================================
    // Host backend
    // ============================================================

    node(
      (2.5, 7.75),
      box2([QEMU VirtIO-GPU], [PCI transport + virtqueues], bold: true),
      name: <qemu>,
      width: 44mm,
      fill: box-fill,
      stroke: 1pt + ink,
    ),

    node(
      (1.55, 8.80),
      text(size: 8pt)[2D scanout],
      name: <scanout>,
      width: 22mm,
      fill: impl-fill,
    ),

    node(
      (3.55, 8.80),
      text(size: 8pt)[virglrenderer],
      name: <renderer>,
      width: 25mm,
      fill: future-fill,
      stroke: (
        paint: ink,
        thickness: 0.9pt,
        dash: "dashed",
      ),
    ),

    // ============================================================
    // Edges: implemented path
    // ============================================================

    edge(<app.south>, <gfx.north>, "->"),
    edge(<gfx.south>, <sw.north>, "->"),
    edge(<sw.south>, <vgpu.north>, "->"),

    edge(
      <vgpu.south>,
      <qemu.north>,
      "<|-|>",
    ),

    edge(<qemu.south>, <scanout.north>, "->"),

    // ============================================================
    // Edges: planned path
    // ============================================================

    edge(
      <gfx.west>,
      <virgl.north>,
      "->",
      stroke: (
        paint: ink,
        thickness: 0.9pt,
        dash: "dashed",
      ),
      label: text(size: 7pt)[3D],
      label-side: left,
    ),

    edge(
      <virgl.south>,
      <vgpu.west>,
      "->",
      stroke: (
        paint: ink,
        thickness: 0.9pt,
        dash: "dashed",
      ),
    ),

    edge(
      <qemu.south>,
      <renderer.north>,
      "->",
      stroke: (
        paint: ink,
        thickness: 0.9pt,
        dash: "dashed",
      ),
    ),
  ),

  caption: [
    VOGUE system architecture. The solid path implements VirtIO-GPU 2D scanout from a Unikraft guest; dashed edges show the planned virgl path for GPU rendering.
  ],
) <fig:vogue-arch>