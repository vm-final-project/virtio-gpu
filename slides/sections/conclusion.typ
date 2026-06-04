#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= Conclusion

== Three takeaways

#v(3em)

- We analyzed the GPU path
  - VOGUE breaks down VirtIO-GPU into driver, library, graphics, and compute pieces
- We showed a thin Unikraft design
  - small guest libraries reach display, Vulkan, and Venus without importing Linux
- We proved real workloads
  - we ported apps, ran graphics demos, and served llama.cpp over the real GPU

#v(2em)

#textbox[
  we studied the GPU stack, built the
  Unikraft libraries we needed, and ran real applications plus the llama.cpp server.
]
