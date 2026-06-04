#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Evaluation

== Evaluation host: one real GPU path

#table(
  columns: (auto, 1fr),
  [Layer], [Setup],
  table.hline(),
  [VM monitor], [QEMU 11.0.1],
  [GPU device], [`virtio-gpu-gl-pci, blob=true, venus=true`],
  [Host bridge], [Venus-enabled virglrenderer],
  [Host GPU], [Tesla V100-SXM2-16GB],
  [Guest], [Unikraft appliance, one image per purpose],
  [Rule], [same-run artifact or no claim],
  table.hline(),
)

#textbox[
  We evaluate the same bridge Linux uses, but with a much thinner Unikraft guest.
]

== Current stage

#table(
  columns: (auto, 1.25fr, 1.55fr),
  [What we checked], [Goal], [Status today],
  table.hline(),
  [Graphics basics],
  [graphics apps can open buffers and draw],
  [virgl sends 3 real 3D submits in our small test],
  [Vulkan basics],
  [create devices, talk to Venus, and start a real app],
  [DRM shim, ICD setup, Venus command path, and vkmark scene loading all run],
  [llama.cpp GPU],
  [upstream compute uses the real GPU through Venus],
  [ggml-vulkan inference runs inside Unikraft on the Tesla V100],
  [llama.cpp HTTP],
  [GPU workload is usable as a service],
  [endpoint `/health` lwIP],
  table.hline(),
)

== Graphics results today

#table(
  columns: (auto, 1.3fr, 1fr),
  [Workload], [Current number / result], [What we can say],
  table.hline(),
  [`glmark2.sw`], [142.82 fps in the 120-frame scene-clear run], [subset benchmark, not full suite],
  [`kmscube` virgl], [3 virgl-submitted frames and 3 `SUBMIT_3D` commands], [one small clear-frame proof],
  [`vkmark`], [10 startup scenes load through our Vulkan app path], [no Unikraft FPS yet],
  table.hline(),
)

#textbox[
  Graphics already runs in our system, but we still separate software rendering,
  app bring-up, and full benchmark claims.
]

== llama.cpp: compute works, performance gap remains

#table(
  columns: (1.4fr, auto, auto),
  [Same model / GPU path], [Prefill pp512], [Generate tg128],
  table.hline(),
  [CPU Unikraft], [9.4], [7.4],
  [VOGUE Unikraft + Venus], [2045.8], [139.3],
  [Linux VM + Venus], [4948.17], [323.64],
  [Bare-metal Vulkan], [5586.91], [239.16],
  table.hline(),
)

#v(0.2em)
#text(size: 0.78em)[`pp512` = 512-token prompt ingestion speed. `tg128` = 128-token decode speed. Units: tokens/s.]

- GPU offload is a large jump over CPU-only Unikraft.
- Linux VM over the same Venus bridge is still faster.
- The remaining performance difference is in our Vulkan library and driver code.

== The server works

#table(
  columns: (auto, 1fr),
  [Server fact], [Current result],
  table.hline(),
  [HTTP liveness], [`/health`, `/v1/models`, `/completion` return 200],
  [Slots configured], [4],
  [Throughput run], [8 requests at concurrency 1],
  [Prompt throughput], [146.14 tokens/s during prompt ingestion],
  [Decode throughput], [25.53 tokens/s during token generation],
  [TTFT], [4.98 s to first token],
  [Request rate], [0.159 requests/s in this small burst test],
  table.hline(),
)

#textbox[
  The server now works. The next step is making it faster under real load.
]
