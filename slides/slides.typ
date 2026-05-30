// #import "@preview/touying:0.7.3": *
#import "lib/xwysyy-typst/xwysyy.typ": *
#import "lib/xwysyy-typst/xwysyy-extras.typ": *

// #show: simple-theme.with(aspect-ratio: "16-9")

#show: xwysyy-pre.with(
  aspect-ratio: "16-9",
  theme: "sky",
  footer: [Group 8],
  config-info(
    title: [VM Final Project: Group 8],
    subtitle: [VOGUE: VirtiO-Gpu on Unikraft Environment],
    author: "",
    date: datetime.today(),
    institution: "Support GPU Acceleration in Unikraft",
  ),
)

#title-slide()
// #title-slide(title: [ ])

#include "sections/motivation.typ"
#include "sections/background.typ"
#include "sections/design.typ"
#include "sections/implement.typ"
#include "sections/future-work.typ"
#include "sections/conclusion.typ"

#end-slide(
  title: [The End!],
  body: [Thank you!],
)



