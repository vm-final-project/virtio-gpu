#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Evaluation

== What we can demonstrate today

On a real GPU host (QEMU + Venus + Tesla V100), every capability below was
#bred[reproduced live in the same run] — no numbers carried over from before.

#pause

#table(
  columns: (auto, 1fr),
  [Capability], [Result],
  table.hline(),
  [2D display (kmscube)], [draws and flips real frames],
  [OpenGL benchmark (glmark2)], [renders at 186 fps (software path)],
  [Vulkan benchmark (vkmark)], [starts and loads 10 scenes],
  [llama.cpp on the GPU], [generates text over Vulkan],
  [llama.cpp HTTP server], [answers real web requests],
  table.hline(),
)

#pause

  - #red[Insight]: a result only counts when it reproduces #bred[in that run];
    this rule is what stops a weak demo from being sold as a strong one

== Footprint and boot: the unikernel payoff

#grid(
  columns: (1.4fr, 1fr),
  gutter: 0.8em,
  [
    #table(
      columns: (auto, auto, auto),
      [Image], [Size], [Boot time],
      table.hline(),
      [VOGUE graphics], [#bred[292 KB]], [#bred[~10 ms]],
      [VOGUE GPU probe], [336 KB], [~250 ms],
      [Linux VM (reference)], [10.5 MB], [~857 ms],
      table.hline(),
    )
  ],
  [
    - #bred[~35×] smaller image
    - #bred[~80×] faster to boot
  ],
)

#pause

#v(0.3em)

  - #red[Insight]: leaving Linux's graphics stack (DRM / KMS / Mesa) *out of the
    guest* is not just tidy — it is #bred[why] the image is tiny and boots instantly

== llama.cpp speed: where does the time go?

#grid(
  columns: (1.5fr, 1fr),
  gutter: 0.8em,
  [
    #table(
      columns: (auto, auto, auto),
      [Same GPU, same model], [prefill], [generate],
      table.hline(),
      [Bare metal (no VM)], [5587], [239],
      [Linux VM over Venus], [4948], [324],
      [VOGUE unikernel], [2232], [160],
      table.hline(),
    )

    #text(size: 0.8em)[(tokens / second)]
  ],
  [
    - same GPU path for all three
    - so we can isolate
      *unikernel vs Linux*
  ],
)

#pause

#v(0.3em)

#textbox(
  [#bred[Key insight]

  A normal Linux VM reaches \~89% of bare-metal over the *same* GPU bridge — so
  the bridge (Venus) is #red[not] the bottleneck. Our remaining gap is VOGUE's own
  young guest-side driver vs Linux's mature one, plus using only one guest CPU.
  That is #bred[room to optimise], not an unavoidable cost of virtualisation.],
)
