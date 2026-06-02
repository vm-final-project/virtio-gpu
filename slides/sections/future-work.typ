#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Future work

== The question changed: from "does it run?" to "how fast?"

#textbox(
  [*Graphics side*

  - compare fairly against Linux + Mesa
  - run full `glmark2` scenes, not just one
  - get real frame rates from `vkmark`],
  [*LLM side*

  - close the gap to the Linux VM
  - measure the server under load
  - load the model faster],
)

#pause

#v(0.3em)

  - #red[Insight]: we showed the slowdown lives in #bred[our own guest driver],
    so the work targets that layer and the single-CPU limit — not the GPU bridge

== What we will do next, and why

#table(
  columns: (auto, 1fr, 1fr),
  [Priority], [Next step], [Why it matters],
  table.hline(),
  [P0], [use more than one guest CPU], [one CPU caps generation speed today],
  [P0], [measure the server under load], [it answers requests; we lack the numbers],
  [P1], [memory-map the model file], [today it is copied first → slow start (~5 s)],
  [P1], [run matched Linux baselines], [turns our ratios into fair comparisons],
  [P2], [security: fuzz the device parser], [put a number on the attack-surface win],
  table.hline(),
)

#pause

  - every step keeps the #bred[same rule]: a number is only reported once it has
    been reproduced in a real run

== Upstreaming and long-term direction

- give our reusable guest-side fixes back to the upstream projects
- make the whole artifact easy for others to rebuild and reproduce
- grow from #red[small, bounded demos] to broader GPU graphics on Unikraft

#pause

#v(0.4em)

#textbox(
  [#bred[The bigger idea]

  Our step-by-step evidence ladder is reusable: it is a *method* for bringing any
  unikernel into graphics and GPU territory without overclaiming along the way.],
)
