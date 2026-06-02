#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Future work

== The work shifted: from "does it run?" to "how fast?"

#textbox(
  [*Graphics side*

  - matched Linux + Mesa baselines
  - full `glmark2` scenes + frame hashes
  - broaden `vkmark` to real fps],
  [*LLM side*

  - close the Linux-guest gap
  - server throughput (TTFT, req/s)
  - faster model loading],
)

#pause

#v(0.3em)

  - #red[Insight]: the gap we measured is #bred[in the guest stack], so the
    roadmap targets the dispatch/encoder layer and guest SMP — not Venus

== Prioritized roadmap

#table(
  columns: (auto, 1fr, 1fr),
  [Pri.], [Next step], [Why it matters],
  table.hline(),
  [P0], [guest SMP + thread split], [single vCPU caps decode today],
  [P0], [HTTP throughput evidence], [serves now; needs req/s + TTFT],
  [P1], [replace `use_mmap=false`], [cut the 4.7--7.1 s model load],
  [P1], [matched Linux baselines], [turn ratios into fair claims],
  [P2], [security / fuzz parser], [quantify attack-surface win],
  table.hline(),
)

#pause

  - each step keeps the #bred[same evidence discipline]: a number only counts
    when a same-run artifact backs it

== Upstreaming and long-term direction

- upstream the reusable guest-side fixes (PCI ID, Venus encoder, ring protocol)
- make the artifact easier for others to reproduce
- push from #red[bounded demos] toward broader accelerated Vulkan on Unikraft

#pause

#v(0.4em)

#textbox(
  [#bred[Long-term thesis]

  The staged evidence ladder is reusable: it is a *method* for extending any
  unikernel into graphics- and GPU-adjacent domains without overclaiming.],
)
