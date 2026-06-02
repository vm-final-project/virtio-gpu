// #import "@preview/touying:0.7.3": *
#import "lib/xwysyy-typst/xwysyy.typ": *
#import "lib/xwysyy-typst/xwysyy-extras.typ": *

// #show: simple-theme.with(aspect-ratio: "16-9")

#show: xwysyy-pre.with(
  aspect-ratio: "16-9",
  theme: "sky",
  footer: [Group 8],
  config-info(
    title: [VOGUE: VirtIO-GPU on Unikraft],
    subtitle: [Current-stage implementation and evaluation],
    author: "",
    date: datetime.today(),
    institution: "Graphics and llama.cpp inside Unikraft",
  ),
)

#title-slide()
#outline-slide()

#include "sections/motivation.typ"
#include "sections/background.typ"
#include "sections/design.typ"
#include "sections/implement.typ"
#include "sections/evaluation.typ"
#include "sections/future-work.typ"
#include "sections/conclusion.typ"

#end-slide(
  title: [The End!],
  body: [Thank you!],
)


