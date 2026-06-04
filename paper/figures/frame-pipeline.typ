#import "@preview/fletcher:0.5.8" as fletcher: diagram, edge, node

#let ink   = luma(25)
#let sfill = rgb("edf3ed")  // pass/green tint
#let hfill = rgb("f2f2f2")  // host-side
#let solid = 0.8pt + ink

#let step(a, b) = stack(
  spacing: 0.45em,
  align(center)[#text(weight: "bold", size: 6.8pt)[#a]],
  align(center)[#text(size: 6pt)[#b]],
)

#let pnode = node.with(width: 24mm, inset: 3pt, corner-radius: 2pt)
#let bnd_node = node.with(fill: "none", stroke: 1.1pt + ink, inset: 6pt, corner-radius: 3pt)
#let bnd_title(t) = stack(align(left + top)[#text(weight: "bold", size: 8pt)[#t]])

#figure(
  scale(x: 90%, y: 90%, reflow: true)[
  #diagram(
    spacing: (0.5em, 0.6em),
    cell-size: (15mm, 10mm),
    edge-stroke: 0.75pt + ink,
    mark-scale: 65%,

    // Guest-side steps (row 0, columns 0-4)
    pnode((0,0), step([EGL SwapBuffers],   [app calls eglSwapBuffers()]),
          fill: sfill, stroke: solid, name: <sw0>),
    pnode((1,0), step([libukswrender],     [CPU rasterise pixels into SW framebuffer]),
          fill: sfill, stroke: solid, name: <sw1>),
    pnode((2,0), step([DMA copy],          [memcpy into DMA-backed buffer (libukdma)]),
          fill: sfill, stroke: solid, name: <sw2>),
    pnode((3,0), step([TRANSFER\_TO\_HOST\_2D], [push framebuffer to host resource]),
          fill: sfill, stroke: solid, name: <sw3>),
    pnode((4,0), step([RESOURCE\_FLUSH + fence], [flush to scanout; poll fence completion]),
          fill: sfill, stroke: solid, name: <sw4>),

    // Host side (row 0, column 5)
    pnode((5,0), step([QEMU scanout],      [present resource to virtual display]),
          fill: hfill, stroke: solid, name: <sw5>),

    // Edges
    edge(<sw0>, <sw1>, "->"),
    edge(<sw1>, <sw2>, "->"),
    edge(<sw2>, <sw3>, "->"),
    edge(<sw3>, <sw4>, "->"),
    edge(<sw4>, <sw5>, "->"),

    // Boundary boxes
    bnd_node(
      enclose: (<sw0>, <sw1>, <sw2>, <sw3>, <sw4>),
      fill: rgb("f4f6f9"), name: <guest-box>,
      bnd_title([Guest VM]),
    ),
    bnd_node(
      enclose: (<sw5>,),
      fill: hfill, name: <host-box>,
      bnd_title([Host]),
    ),
  )
  ],
  caption: [
    The per-frame 2D display pipeline. `libukswrender` rasterises the frame into a DMA-backed buffer on the CPU; `TRANSFER_TO_HOST_2D` pushes it into the host resource, `SET_SCANOUT` binds the resource to the virtual display, and `RESOURCE_FLUSH` (carrying a completion fence) presents it. The guest waits on that one fence before starting the next frame, so it never overwrites a buffer QEMU is still reading. No GPU is involved on this path.
  ],
) <fig:frame-pipeline>
