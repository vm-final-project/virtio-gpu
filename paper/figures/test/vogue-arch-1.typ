#import "@preview/fletcher:0.5.8" as fletcher: diagram, edge, node

#let ink = luma(25)
#let guest-fill = rgb("f7f7f7")
#let host-fill = rgb("f2f2f2")
#let box-fill = rgb("ffffff")
#let impl-fill = rgb("eeeeee")
#let ready-fill = rgb("f4f4f4")
#let blocked-fill = rgb("fbfbfb")

#let solid = 0.9pt + ink
#let boundary = 1pt + ink
#let dashed = (
  paint: ink,
  thickness: 0.9pt,
  dash: "dashed",
)

#let label2(title, subtitle, bold: false) = stack(
  spacing: 0.5em,
  align(center)[#text(weight: "bold", size: 8pt)[#title]],
  align(center)[#text(size: 7pt)[#subtitle]],
)

#let sysnode = node.with(width: 43mm, fill: box-fill, stroke: solid, corner-radius: 2pt)
#let boundary_title(title) = stack(align(left + top)[#text(weight: "bold", size: 11pt)[#title]])
#let boundary_node = node.with(fill: "none", stroke: boundary, inset: 10pt, corner-radius: 3pt)

#figure(
  scale(x: 88%, y: 88%, reflow: true)[
  #diagram(
    spacing: 1em,
    cell-size: (21mm, 11mm),
    edge-stroke: 0.9pt,
    edge-corner-radius: 4pt,
    mark-scale: 70%,
    node-corner-radius: 2pt,

    sysnode((0, 0), align(center, text(weight: "bold")[Graphics applications]), name: <graph-app>),
    edge("->"),
    sysnode((0, 1), label2([libukgfx / libukegl], [EGL, GLES2, GBM, DRM shim]), name: <libukgfx>),
    edge("->"),
    sysnode((0, 2), label2([libukswrender + libukdma], [software pixels + DMA buffers]), fill: impl-fill, name: <libuksw>),
    edge("->"),
    sysnode((0, 3), label2([libukvirtio_gpu 2D], [resources, transfers, scanout, fences]), fill: impl-fill, name: <libukvirtio_gpu>),

    sysnode((-1, 2), label2([3D / Venus controlq], [ctx, submit, blobs, UUID, map]), fill: ready-fill, stroke: dashed, name: <venus-ready>),
    edge(<libukgfx.west>, <venus-ready.north>, "->", stroke: dashed, label: text(size: 8pt, weight: "bold")[accelerated API], label-side: left),
    edge(<venus-ready.south>, <libukvirtio_gpu.west>, "->", stroke: dashed),

    sysnode((0, 4.5), label2([Unikraft virtio/PCI], [reused transport libraries]), name: <uk-virtio>),
    edge(<libukvirtio_gpu.south>, <uk-virtio.north>, "<|-|>"),

    sysnode((0, 5.5), label2([QEMU VirtIO-GPU 2D], [software display]), fill: impl-fill, name: <qemu2d>),
    edge(<uk-virtio.south>, <qemu2d.north>, "<|-|>"),

    sysnode((-1, 5.5), label2([QEMU 11.0 Venus], [blocked at modern PCI 0x1050]), fill: blocked-fill, stroke: dashed, name: <qemuvenus>),
    edge(<uk-virtio.west>, <qemuvenus.north>, "-/-", stroke: dashed),

    sysnode((0, 6.5), label2([2D scanout], [validated software frames]), fill: impl-fill, name: <scanout>),
    edge(<qemu2d.south>, <scanout.north>, "->"),

    sysnode((-1, 6.5), label2([virglrenderer / Venus], [host GL/Vulkan backend]), fill: blocked-fill, stroke: dashed, name: <renderer>),
    edge(<qemuvenus.south>, <renderer.north>, "->", stroke: dashed),

    boundary_node(enclose: (<graph-app>, <libukgfx>, <libuksw>, <libukvirtio_gpu>, <venus-ready>, <uk-virtio>), fill: guest-fill, name: <guest-box>, boundary_title([Unikraft Guest VM])),
    boundary_node(enclose: (<qemu2d>, <qemuvenus>, <scanout>, <renderer>), fill: host-fill, name: <host-box>, boundary_title([Host / QEMU])),
  ),
  ],
  caption: [VOGUE system architecture. Solid nodes and edges are implemented and evaluated for the 2D/software-render path. Dashed nodes are real protocol-readiness or blocked host-transport components: `libukvirtio_gpu` contains 3D/blob commands, but QEMU 11.0 Venus execution is blocked by modern VirtIO-PCI device discovery before acceleration can be claimed.],
) <fig:vogue-arch>
