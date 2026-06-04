#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Motivation

== Unikernels: tiny, instant, secure — and headless

A #bred[unikernel] fuses a single application with just-enough OS into one
bootable image: one address space, no processes, booted straight by the
hypervisor.

#pause

#grid(
  columns: (1fr, 1fr, 1fr),
  gutter: 0.8em,
  [
    #textbox([*Size*

      image in #bred[hundreds of KB], not hundreds of MB])
  ],
  [
    #textbox([*Boot*

      ready in #bred[milliseconds], not seconds])
  ],
  [
    #textbox([*Security*

      small attack surface — only the code linked])
  ],
)

#pause

#v(0.3em)

- every one of these wins comes from leaving things #red[out]
  - no shell, no user/kernel separation, no multi-process...
  - the ecosystem is still young; many drivers and features arn not ported yet

== GPU workloads are real — Unikraft can't run them yet

Plenty of in-demand workloads need a GPU:

#v(0.3em)

- *Graphics / rendering* — desktop & GUI apps, 3D visualisation, games
- *LLM inference* — e.g. `llama.cpp`, a popular open-source inference engine
- *…and more* — ML training, video transcode, scientific compute

#pause

#v(0.4em)

But Unikraft has #red[no GPU support] today

- Those workloads either can't run at all, or have to run on the CPU
- Solution: #emph[add GPU support to Unikraft]

== The dilemma — and the question we ask

#textbox(
  [The standard way to give a guest GPU access (`virtio-gpu` +
    Mesa Venus) requires the #bred[entire Linux graphics stack] in the
    guest: the kernel DRM / KMS subsystem, `libdrm`, Mesa, and more.

    That is exactly the bulk a unikernel exists to avoid. Getting the GPU looks
    like it means #red["become Linux again"].],
)

#pause

#v(0.4em)

#align(center)[#bred[Can a _minimal_ unikernel guest reach a GPU without importing Linux's graphics stack?]]

#pause

#v(0.3em)

- Concretely: run GPU-accelerated workloads (graphics, LLM inference) #bred[without giving up] the unikernel's tiny image and instant boot
