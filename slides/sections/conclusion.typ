#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Conclusion

== Core insight: graphics is not all-or-nothing

Supporting graphics in a unikernel is #red[not] one giant "port all of Linux"
job. It splits into four #bred[independent layers] we can build and prove one at a time:

#pause

#table(
  columns: (auto, 1fr),
  [Layer], [Its job],
  table.hline(),
  [Device], [talk to the virtual GPU correctly],
  [Application], [let unchanged apps still compile and run],
  [Rendering], [start with the CPU, then hand work to the GPU],
  [Transport], [carry real GPU commands — without pretending that *is* acceleration],
  table.hline(),
)

#pause

  - splitting it this way lets us show #bred[honest progress] before a full GPU
    renderer is even finished

== Takeaways

#item-by-item[
  - #bred[graphics and `llama.cpp` really run inside Unikraft] on a real GPU
  - we reach the same GPU as Linux, through a #bred[far smaller] guest stack
  - that smaller stack is *why* the image is tiny and boots in milliseconds
  - reporting only what we can reproduce is part of the contribution, not just caution
]

#pause

#v(0.3em)

  - what is left is #red[breadth and speed] — the hard "can it work at all?" part is done

== One slide to remember

#textbox(
  [#bred[What we built]

  a proven path from "the GPU command channel works" to "real apps run on it" —
  with every claim backed by a live run],
  [#red[What we kept]

  a thin guest, no Linux graphics stack inside it, and strict honesty about
  what each result means],
)

#pause

#focus-slide[A thin, honest guest stack can reach the real GPU — \ without becoming Linux.]
