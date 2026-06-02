#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Implementation II Application

== Graphics applications we run

#table(
  columns: (auto, auto, 1fr),
  [App], [Role], [What it shows works],
  table.hline(),
  [kmscube], [main demo], [draws a real 3D frame on the GPU, checked pixel-by-pixel],
  [glmark2], [benchmark], [an OpenGL benchmark draws through our shim],
  [vkmark], [Vulkan demo], [a Vulkan benchmark starts and loads its scenes],
  table.hline(),
)

#pause

  - the application code is #bred[unchanged]; our shim absorbs the platform difference

#pause

  - we never blur the line: a #red[software-drawn] pixel is not GPU acceleration

== llama.cpp: four ready-to-run appliances

#grid(
  columns: (1fr, 1fr),
  gutter: 0.8em,
  [
    #textbox(
      [*What we run* (upstream, unchanged)

      - CPU: benchmark + server
      - GPU (Vulkan): benchmark
      - GPU (Vulkan): HTTP server],
    )
  ],
  [
    #table(
      columns: (auto, auto),
      [Path], [Generation speed],
      table.hline(),
      [CPU], [7.7 tokens/s],
      [GPU benchmark], [160.2 tokens/s],
      [GPU server], [140.6 tokens/s],
      table.hline(),
    )
  ],
)

#pause

#v(0.4em)

  - moving to the GPU is a #bred[~20×] speed-up over the CPU-only path
  - the HTTP server keeps #bred[88%] of the raw benchmark speed — the web layer is cheap

== Insight: we add glue, not a fork

#textbox(
  [*What VOGUE writes*

  - a few one-line bridge files
  - one image does one job:
    a benchmark image #red[or] a
    server image, never both
  - the linker drops every
    line of unused code],
  [*What stays untouched*

  - we do *not* reimplement the
    model or inference engine
  - upstream `llama.cpp` runs as-is
  - the real work is in the thin
    guest-side #bred[driver libraries],
    not in rewriting the app],
)

#pause

#v(0.4em)

  - the GPU server even answers real web traffic: a client can ask for the
    model list and get a #bred[completion] back, end-to-end inside the unikernel
