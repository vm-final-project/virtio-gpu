#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Evaluation

== Current-stage evidence

  - host: QEMU 11 + Venus + Tesla V100
  - eval matrix: #bred[27 / 27 PASS], 0 blocked
  - native suite: #bred[164 / 164] checks (no QEMU/GPU)

#pause

#table(
  columns: (auto, 1fr),
  [Row], [Signal],
  table.hline(),
  [`kmscube.frame`], [virgl submit + pixel proof],
  [`glmark2.sw`], [`fps = 186.4`],
  [`vkmark`], [ICD + 10 scenes (substrate)],
  [`llm.bench.vk`], [`tg128 = 160.2`],
  [`llm.server.vk`], [real HTTP + throughput],
  table.hline(),
)

#pause

  - #red[Insight]: every row names its *allowed* and *forbidden* claim —
    the evidence gate is itself an engineering tool

== Footprint and boot: the unikernel payoff

#grid(
  columns: (1.4fr, 1fr),
  gutter: 0.8em,
  [
    #table(
      columns: (auto, auto, auto),
      [Image], [Size], [Boot],
      table.hline(),
      [VOGUE 2D], [#bred[292 KB]], [#bred[10--11 ms]],
      [VOGUE virgl], [336 KB], [251 ms],
      [Linux + initramfs], [10.5 MB], [857 ms],
      table.hline(),
    )
  ],
  [
    - #bred[~35×] smaller image
    - #bred[~80×] faster boot
    - cost of carrying the
      graphics substrate is tiny
  ],
)

#pause

#v(0.3em)

  - #red[Insight]: "no Linux DRM/KMS/Mesa in the guest" is not just clean —
    it is the source of the size and startup win

== llama.cpp speed: where is the gap?

#grid(
  columns: (1.5fr, 1fr),
  gutter: 0.8em,
  [
    #table(
      columns: (auto, auto, auto),
      [Env (same V100, same model)], [pp512], [tg128],
      table.hline(),
      [Bare metal], [5587], [239],
      [Linux guest + Venus], [4948], [324],
      [VOGUE Unikraft], [2232], [160],
      table.hline(),
    )
  ],
  [
    - server decode `140.6` =
      #bred[88%] of its own bench
    - so the HTTP path is fine
  ],
)

#pause

#v(0.3em)

#textbox(
  [#bred[Key insight]

  A stock Linux guest rides the *same* Venus transport at \~89% of bare metal.
  So Venus is #red[not] the bottleneck — the remaining gap is VOGUE's own
  guest-side stack (`libukvenus` + `libukggml_vk` vs Mesa's mature ICD, single vCPU).
  That is #bred[optimisation headroom], not an unavoidable virtualisation tax.],
)
