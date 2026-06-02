#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Implementation II Application

== Graphics application ports

#table(
  columns: (auto, auto, 1fr),
  [App], [Status], [What it proves],
  table.hline(),
  [`kmscube`], [canonical], [real virgl submit + pixel frame proof],
  [`glmark2`], [benchmark], [EGL/GLES2 scene-clear substrate],
  [`vkmark`], [experimental], [Vulkan ICD init + 10 scenes, not fps],
  [`vulkan-smoke`], [demo], [minimal Vulkan substrate (`vk.smoke`)],
  table.hline(),
)

#pause

  - app logic stays #bred[upstream]; shims absorb the platform mismatch

#pause

  - proof types stay separated: #red[software pixels] $!=$ GPU acceleration

== llama.cpp: four single-purpose appliances

#grid(
  columns: (1fr, 1fr),
  gutter: 0.8em,
  [
    #textbox(
      [*What we ported* (unmodified)

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
  [Mode], [Status], [Key number (V100 / Venus)],
  table.hline(),
  [CPU bench], [PASS], [`tg128 = 7.7`],
  [Vulkan bench], [PASS], [`tg128 = 160.2`],
  [Vulkan server], [PASS], [`decode = 140.6`],
  table.hline(),
)

== Insight: upstream stays upstream

#textbox(
  [*VOGUE writes the glue*

  - 6 one-line `#include` shims
  - one image, one purpose:
    only `bench.cpp` #red[*or*] `server.cpp`
  - `--gc-sections` drops the rest],
  [*Upstream stays intact*

  - no GGUF/ggml/llama fork
  - `llama.cpp` sources unpatched
  - engineering lives in the
    guest-side #bred[libraries], not in
    forked application logic],
)

#pause

#v(0.4em)

  - #bred[HTTP server is live]: in-guest `virtio-net -> libuknetdev -> lwIP` (DHCP)
  - same-run probe: `GET /health`, `/v1/models`, `POST /completion` all `200`
