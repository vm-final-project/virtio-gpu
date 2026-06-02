#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Implementation

== Graphics application ports

#table(
  columns: (auto, auto, 1fr),
  [App], [Status], [What it proves],
  table.hline(),
  [`kmscube`], [canonical], [real submit + frame proof],
  [`glmark2`], [benchmark], [bounded EGL/GLES substrate],
  [`vkmark`], [experimental], [Vulkan init, not fps],
  table.hline(),
)

#pause

  - app logic stays upstream-facing

#pause
  - shims absorb platform mismatch

#pause

  - proof types stay separated

== `llama.cpp`: ports and bounded deltas

#grid(
  columns: (1fr, 1fr),
  gutter: 0.8em,
  [
    #textbox(
      [*What we ported*

      - CPU bench / server
      - Vulkan bench
      - Vulkan HTTP server],
    )
  ],
  [
    #table(
      columns: (auto, auto, 1fr),
      [Path], [LoC], [Local delta],
      table.hline(),
      [`CPU`], [473], [entrypoints + model glue],
      [`VK`], [668], [dispatch + Venus + server],
      table.hline(),
    )
  ],
)

#pause

#table(
  columns: (auto, auto, auto),
  [Mode], [Status], [Key number],
  table.hline(),
  [CPU bench], [PASS], [`tg128=7.7`],
  [Vulkan bench], [PASS], [`tg128=160.2`],
  [Vulkan server], [PASS], [`decode=140.6`],
  table.hline(),
)
