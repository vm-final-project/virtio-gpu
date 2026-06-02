#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Evaluation

== Current-stage evidence

{
  show: components.item-by-item

  - host: QEMU 11 + Venus + V100
  - matrix: #bred[27 / 27 PASS]
  - stage checks: #bred[16 / 16 PASS]
}

#pause

#table(
  columns: (auto, 1fr),
  [Row], [Signal],
  table.hline(),
  [`kmscube.frame`], [frame proof],
  [`glmark2.sw`], [`fps=186.4`],
  [`vkmark`], [substrate PASS],
  [`llm.bench.vk`], [`tg128=160.2`],
  [`llm.server.vk`], [HTTP + throughput],
  table.hline(),
)

== `llama.cpp-server` speed comparison

#grid(
  columns: (1.25fr, 1fr),
  gutter: 0.8em,
  [
    #table(
      columns: (auto, auto, auto),
      [Env], [Metric], [tok/s],
      table.hline(),
      [Unikraft], [decode], [140.6],
      [Bare metal], [`tg128`], [239.2],
      [QEMU Linux], [`tg128`], [323.6],
      table.hline(),
    )

    #v(0.4em)

    #table(
      columns: (auto, auto),
      [Ratio], [Value],
      table.hline(),
      [vs bare metal], [0.59×],
      [vs QEMU Linux], [0.43×],
      table.hline(),
    )
  ],
  [
    {
      show: components.item-by-item

      - use decode tok/s vs `tg128`
      - same model, same V100
      - gap is still in the guest stack
      - end-to-end HTTP: `52.77 tok/s`
    }
  ],
)
