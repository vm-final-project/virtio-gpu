#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Conclusion

== Takeaways

{
  show: components.item-by-item

  - #bred[graphics + `llama.cpp` can run in Unikraft]
  - not full Linux graphics
  - current artifact is bounded + evidence-gated
  - next: optimize, upstream, broaden Vulkan
}

#pause

#textbox(
  [#bred[Takeaway]

  transport proof -> usable workloads],
  [#red[Constraint]

  keep the unikernel boundary + claim discipline],
)
