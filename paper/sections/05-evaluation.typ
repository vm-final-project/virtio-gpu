= Evaluation <sec:eval>

Our evaluation follows the evidence-gated discipline of DG5: we report what a
same-run artifact actually shows, and no more. The guiding rule is "same-run
artifact or no claim." We first describe the host, then summarize the current
stage of each path, then give graphics and `llama.cpp` numbers, and finally the
server.

== Evaluation host

We evaluate on the same host bridge a Linux guest would use, but with a much
thinner Unikraft guest. @tbl:eval-host lists the setup.

#figure(
  table(
    columns: (auto, 1fr),
    align: (left, left),
    table.header([Layer], [Setup]),
    [VM monitor], [QEMU 11.0.1],
    [GPU device], [`virtio-gpu-gl-pci, blob=true, venus=true`],
    [Host bridge], [Venus-enabled virglrenderer],
    [Host GPU], [Tesla V100-SXM2-16GB],
    [Guest], [Unikraft appliance, one image per purpose],
    [Rule], [same-run artifact or no claim],
  ),
  caption: [Evaluation host. The host bridge is identical to Linux's; only the
    guest is replaced.],
) <tbl:eval-host>

== Current stage

@tbl:current-stage states, for each path, the goal and what runs today. The
phrasing is deliberately literal because the rows mean different things: some
prove software rendering, some prove transport, and some prove real GPU compute.

#figure(
  table(
    columns: (auto, 1.25fr, 1.55fr),
    align: (left, left, left),
    table.header([What we checked], [Goal], [Status today]),
    [Graphics basics], [graphics apps can open buffers and draw],
      [virgl sends 3 real 3D submits in our small test],
    [Vulkan basics], [create devices, talk to Venus, start a real app],
      [DRM shim, ICD setup, Venus command path, and `vkmark` scene loading all run],
    [`llama.cpp` GPU], [upstream compute uses the real GPU through Venus],
      [`ggml-vulkan` inference runs inside Unikraft on the Tesla V100],
    [`llama.cpp` HTTP], [GPU workload is usable as a service],
      [`/health` endpoint served over lwIP],
  ),
  caption: [Current stage by path.],
) <tbl:current-stage>

== Graphics results

@tbl:graphics-results gives the graphics numbers we can stand behind. We keep
software rendering, application bring-up, and full-benchmark claims separate:
`glmark2.sw` is a subset benchmark, the `kmscube` virgl result is a single small
clear-frame proof, and `vkmark` only demonstrates that the Vulkan app starts and
loads scenes.

#figure(
  table(
    columns: (auto, 1.3fr, 1fr),
    align: (left, left, left),
    table.header([Workload], [Current number / result], [What we can say]),
    [`glmark2.sw`], [142.82 fps in the 120-frame scene-clear run],
      [subset benchmark, not full suite],
    [`kmscube` virgl], [3 virgl-submitted frames, 3 `SUBMIT_3D` commands],
      [one small clear-frame proof],
    [`vkmark`], [10 startup scenes load through our Vulkan app path],
      [no Unikraft FPS yet],
  ),
  caption: [Graphics results. Software rendering, app bring-up, and benchmark
    claims are reported separately.],
) <tbl:graphics-results>

== `llama.cpp`: compute works, a performance gap remains

The headline systems result is that upstream `llama.cpp` runs GPU-accelerated
inference inside a unikernel over Venus. @tbl:llama-perf compares the same model
and GPU path across four environments, in tokens/s; `pp512` is 512-token prompt
ingestion (prefill) and `tg128` is 128-token decode.

#figure(
  table(
    columns: (1.4fr, auto, auto),
    align: (left, right, right),
    table.header([Same model / GPU path], [Prefill pp512], [Generate tg128]),
    [CPU Unikraft], [9.4], [7.4],
    [VOGUE Unikraft + Venus], [2045.8], [139.3],
    [Linux VM + Venus], [4948.17], [323.64],
    [Bare-metal Vulkan], [5586.91], [239.16],
  ),
  caption: [`llama.cpp` throughput (tokens/s). `pp512` = prompt ingestion,
    `tg128` = decode.],
) <tbl:llama-perf>

Three things stand out. GPU offload is a large jump over CPU-only Unikraft —
prefill rises from 9.4 to 2045.8 tokens/s. A Linux guest over the _same_ Venus
bridge is still faster, which tells us the gap is not in the host bridge.
Because the host side is shared and the Linux guest beats us on it, the remaining
difference must live in our own Vulkan library and driver code — and that is
exactly where we direct future tuning.

== The server works

`llama.cpp` also runs as an HTTP service inside the unikernel, over lwIP.
@tbl:server gives the current server facts. The endpoints are live and the
service handles real requests; the numbers come from a small burst and are an
honest "it works," not a tuned throughput claim.

#figure(
  table(
    columns: (auto, 1fr),
    align: (left, left),
    table.header([Server fact], [Current result]),
    [HTTP liveness], [`/health`, `/v1/models`, `/completion` return 200],
    [Slots configured], [4],
    [Throughput run], [8 requests at concurrency 1],
    [Prompt throughput], [146.14 tokens/s during prompt ingestion],
    [Decode throughput], [25.53 tokens/s during token generation],
    [Time to first token], [4.98 s],
    [Request rate], [0.159 requests/s in this small burst],
  ),
  caption: [`llama.cpp` server over lwIP. The service works; tuning it under
    real load is future work.],
) <tbl:server>

== Where the next speedup should come from

The evaluation also tells us where to look next, and notably the answer is _our
code_, not a missing GPU bridge. The Linux VM beats VOGUE on the same Venus
bridge (323.64 vs 139.3 tg128 decode tokens/s), which points at time spent in our
Vulkan call path; server throughput is low (25.53 decode tokens/s in an
8-request burst measured at concurrency 1), which calls for guest SMP and split
server threads; model loading copies too much (Vulkan server load takes
5935.26 ms with `use_mmap=false`), which calls for an `mmap`-capable model path;
and graphics still lacks broad scene FPS, since `vkmark` loads ten scenes but
yields no Unikraft FPS yet. These are next-step performance and breadth targets,
not bring-up blockers — the prototype reaches the GPU and runs real workloads;
the work that remains is making it fast and broad.
