#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Implementation II

== Applications: small ports, real workloads

#table(
  columns: (auto, auto, auto, 1.4fr),
  [App], [Role], [VOGUE glue], [What it proves],
  table.hline(),
  [`kmscube`], [minimal graphics demo], [593 LOC / 3 ABI shims], [unchanged app draws inside our Unikraft system],
  [`glmark2`], [OpenGL app bring-up], [155 LOC / 3 ABI shims], [a larger GL app can start and run on our path],
  [`vkmark`], [Vulkan app bring-up], [62 LOC / 2 Vulkan shims], [a Vulkan app can start and load its scenes],
  [`llama.cpp`], [real compute workload], [upstream bridge + dispatch], [LLM inference runs inside Unikraft],
  table.hline(),
)

- The application code stays upstream; VOGUE provides the missing system pieces.

- LOC here is VOGUE-owned glue, not the vendored application body.

== Issue: Graphics has more moving parts

#table(
  columns: (1fr, 1.25fr),
  [Compute pipeline], [Graphics pipeline],
  table.hline(),
  [one main object: `VkComputePipelineCreateInfo`], [many fixed-function state blocks],
  [one shader stage: compute], [at least vertex + fragment stages],
  [data in, data out], [vertices, viewport, rasterization, depth/stencil, blending, presentation],
  [fits one-purpose appliances], [expects a window/display stack],
  table.hline(),
)

#textbox[
  Compute is a shorter path. Graphics needs many more pieces around the shader
  work before anything appears on screen.
]

== Compute was the practical first target

#table(
  columns: (auto, 1fr, 1.5fr),
  [What we compare], [Compute pipeline], [Graphics pipeline],
  table.hline(),
  [Pipeline setup], [one create struct], [`VkGraphicsPipelineCreateInfo` plus 10+ state structs],
  [Shader stages], [one compute shader], [vertex + fragment shaders at minimum],
  [Work flow], [one short execution path], [many linked stages must all match],
  [Screen output], [no screen output needed], [must manage render targets and show images on screen],
  table.hline(),
)

#textbox[
  In Vulkan, compute gave us one smaller path to get working first. Graphics
  required a larger setup before we could show real images.
]

- After proving the graphics basics, compute was the faster path to a real
  GPU workload.

== Current Stage for Graphics Applications

#table(
  columns: (auto, 1fr),
  [App / row], [Current ],
  table.hline(),
  [`kmscube.sw`], [software-rendered graphics works],
  [`kmscube.submit/frame`], [virgl sends `SUBMIT_3D` and we captured the clear frame],
  [`glmark2.sw`], [the scene-clear run works on our software path],
  [`vkmark`], [the Vulkan app starts and loads scenes],
  // [`llama.cpp-vk`], [real Vulkan compute over Venus],
  // [`llama.cpp-vk-server`], [HTTP serving over lwIP + Venus],
  // table.hline(),
)

#textbox[
  Each row here means a different thing. Some rows prove software
  rendering, some prove transport, and some prove real GPU compute.
]

== The compute path reaches the real GPU

#grid(
  columns: (1.1fr, 1fr),
  gutter: 0.8em,
  [
    ```text
    upstream llama.cpp bench/server
      -> ggml-vulkan
      -> libukggml_vk static dispatch
      -> libukvenus encoder + Venus protocol
      -> libukvirtgpu_drm + libukvirtio_gpu
      -> QEMU virtio-gpu-gl + virglrenderer
      -> host Vulkan driver / Tesla V100
    ```
  ],
  [
    #table(
      columns: (auto, 1fr),
      [Appliance], [Current status],
      table.hline(),
      [CPU bench], [baseline inference passes],
      [CPU server], [readiness appliance passes],
      [Vulkan bench], [GPU inference over Venus passes],
      [Vulkan HTTP], [real requests over lwIP pass],
      table.hline(),
    )
  ],
)

#table(
  columns: (auto, 1fr, auto),
  [Library], [Role], [Source LOC],
  table.hline(),
  [`libukggml_vk`], [static ggml-vulkan dispatch], [2,146],
  [`libukvenus`], [mostly generated Venus encoder/protocol + ring glue], [96,690],
  [`libukvirtgpu_drm`], [Linux virtgpu DRM ioctl shim], [638],
  [`libukvirtio_gpu`], [VirtIO-GPU frontend + virgl/controlq], [2,246],
  [`libukvk_icd`], [Vulkan ICD bootstrap shim], [235],
  table.hline(),
)

- `libukvenus` is mostly *generated* from upstream Venus protocol and Vulkan API metadata.
- The hand-written part is the Unikraft-side ring and glue code.
