#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Conclusion

== Core insight: graphics is not all-or-nothing

Unikernel graphics is #red[not] a monolithic "port all of Linux" problem.
It decomposes into four #bred[separable planes]:

#pause

#table(
  columns: (auto, 1fr),
  [Plane], [What it owns],
  table.hline(),
  [Device], [speak VirtIO-GPU correctly],
  [API], [keep upstream source compatible],
  [Rendering], [software pixels #sym.arrow.r virgl / Venus],
  [Transport], [test real 3D/blob without faking acceleration],
  table.hline(),
)

#pause

  - this is what lets us produce #bred[honest evidence before] a full GPU renderer exists

== Takeaways

#item-by-item[
  - #bred[graphics + `llama.cpp` run inside Unikraft] over real Venus / V100
  - #bred[dependency collapse]: same host contract, a much thinner guest stack
  - smaller, faster image — *because* we kept Linux DRM/Mesa out of the guest
  - evidence discipline is part of the contribution, not just caution
]

#pause

#v(0.3em)

  - what is left is #red[breadth and performance], no longer first transport proof

== One slide to remember

#textbox(
  [#bred[Takeaway]

  transport proof #sym.arrow.r usable workloads, with every claim bounded by a same-run artifact],
  [#red[Constraint]

  keep the unikernel boundary thin and the claim discipline strict],
)

#pause

#focus-slide[A thin, evidence-gated guest stack can reach the real GPU — \ without becoming Linux.]
