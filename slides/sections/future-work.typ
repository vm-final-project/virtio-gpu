#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Future work

== Optimization roadmap

#textbox(
  [*Graphics side*

  - more apps than `kmscube`
  - stronger `glmark2` / `vkmark`
  - broader Vulkan coverage],
  [*LLM side*

  - close Linux-guest gap
  - better model loading
  - better batching / sync],
)

#pause

#table(
  columns: (auto, 1fr),
  [Priority], [Next step],
  table.hline(),
  [P0], [SMP + thread split],
  [P1], [replace `use_mmap=false` path],
  [P2], [build-target tuning],
  table.hline(),
)

== Upstreaming and long-term direction

{
  show: components.item-by-item

  - upstream reusable fixes
  - discuss with maintainers
  - make artifacts easier to reproduce
  - push toward broader Vulkan on Unikraft
}
