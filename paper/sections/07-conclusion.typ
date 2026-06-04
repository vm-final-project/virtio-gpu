= Future Work <sec:future>

The bring-up phase is over; what remains is performance and breadth, and the
evaluation already points at where to spend effort. @tbl:future lists the
priorities. The highest-priority items address that we measured the server only
at concurrency 1: adding guest SMP and more vCPUs, and tuning the `llama.cpp`
server's threads and slots, move it from "it works" to "it handles real load."
Next, the gap to a Linux guest on the same Venus bridge directs us to cut time
spent in our own Vulkan call path, and to load the model with less copying (the
current path runs without `mmap` or huge pages). Finally, matched Linux graphics
baselines and small scene-render tests would turn the graphics results into a
fair comparison with real frame numbers.

#figure(
  table(
    columns: (auto, 1.1fr, 1.35fr),
    align: (center, left, left),
    table.header([Priority], [Next step], [Why it matters]),
    [P0], [Add guest SMP and more vCPUs], [the server was only measured at concurrency 1],
    [P0], [Tune `llama.cpp` server threads and slots], [move from "it works" to real load],
    [P1], [Reduce time in our Vulkan call path], [Linux on the same Venus bridge is still faster],
    [P1], [Load the model with less copying], [the model path still runs without `mmap` or huge pages],
    [P1], [Add matched Linux graphics baselines], [make graphics results a fair comparison],
  ),
  caption: [From a working prototype to performance: the next targets.],
) <tbl:future>

Beyond these immediate items, the longer-term value is a reusable method rather
than any single benchmark number: continue turning the VirtIO-GPU/Venus stack
analysis into reusable guest libraries, keep the one-image-one-purpose ports
reproducible, and extend from today's demos and the `llama.cpp` server to more
graphics and GPU workloads.

= Conclusion <sec:conclusion>

We set out to answer whether a minimal unikernel guest can reach a real GPU
without importing Linux's graphics stack, and VOGUE shows that it can. The key
is that a unikernel has exactly one GPU client, which lets us discard DRM
multiplexing and replace a roughly three-million-line Linux tower with about
five thousand lines of Unikraft libraries that speak the identical VirtIO-GPU and
Venus protocol the host already understands.

In doing so we made three things concrete. We analyzed the GPU path, breaking
VirtIO-GPU down into driver, library, graphics, and compute pieces and mapping
each onto its Linux counterpart. We showed a thin Unikraft design in which small
guest libraries reach display, Vulkan, and Venus without importing Linux, in a
292 KB image that boots in 10–11 ms. And we proved real workloads: we ported
`kmscube`, `glmark2`, `vkmark`, and upstream `llama.cpp` unmodified, ran graphics
demos through our path, and served `llama.cpp` GPU inference over the real GPU on
a Tesla V100. A performance gap to a Linux guest on the same Venus bridge
remains — but because the host bridge is shared, that gap lives in our own code,
not in any missing piece of the GPU path. Reaching the GPU, it turns out, need
not mean becoming Linux again.
