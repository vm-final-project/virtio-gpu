#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Future work

== From working prototype to performance

#table(
  columns: (auto, 1.1fr, 1.35fr),
  [Priority], [Next step], [Why it matters],
  table.hline(),
  [P0], [Add guest SMP and more vCPUs], [today we only measured the server at concurrency 1],
  [P0], [Tune llama server threads and slots], [move from "it works" to "it can handle real load"],
  [P1], [Reduce time spent in our Vulkan call path], [Linux VM on the same Venus bridge is still faster],
  [P1], [Load the model with less copying], [today the model path still runs without `mmap` or huge pages],
  [P1], [Add matched Linux graphics baselines], [make graphics results a fair comparison],
  [P2], [Run more `glmark2` and `vkmark` scenes], [move from one small check to broader benchmarks],
  [P2], [Fuzz device and parser inputs], [test the safety benefit of the smaller guest codebase],
  table.hline(),
)

- These are not old bring-up blockers; they are the next performance and breadth targets.

== Where the next speedup should come from

#table(
  columns: (1.2fr, 1.25fr, 1.15fr),
  [Bottleneck signal], [Current measurement], [Planned attack],
  table.hline(),
  [Linux VM + Venus beats VOGUE], [323.64 vs 139.3 `tg128` decode t/s], [reduce time spent in our Vulkan call path],
  [server throughput is low], [25.53 decode t/s in the 8-request burst], [add SMP and split server threads],
  [model loading copies too much],
  [Vulkan server load is 5935.26 ms with `use_mmap=false`],
  [add an mmap-capable model path],
  [graphics lacks broad scene FPS],
  [vkmark loads 10 scenes but has no Unikraft FPS],
  [add small scene-render tests and frame checks],
  table.hline(),
)

#textbox[
  We now know where to look: in our own Vulkan and server code, not in a missing
  GPU bridge.
]

== Long-term: reusable GPU unikernel method

- keep turning the VirtIO-GPU / Venus stack analysis into reusable guest libraries
- keep the one-image-one-purpose app ports reproducible
- extend from today's demos and llama.cpp server to more graphics and GPU workloads

#textbox[
  The long-term value is not one benchmark number; it is a reusable way to
  understand the GPU stack, port applications, and keep the guest small.
]
