#import "clean-acmart/0.0.1/clean-acmart.typ": acmart, acmart-ccs, acmart-keywords, acmart-ref, to-string

#let cuhk = super(sym.suit.spade)

#let title = [
  VOGUE: VirtIO-GPU on Unikraft for Graphics Virtualization
]

#let authors = (
  (
    name: "Paper #001",
    email: "",
    mark: super(sym.suit.diamond),
  ),
)
#let affiliations = (
  (
    name: [Anonymous Institution],
    mark: super(sym.suit.diamond),
    department: [Department of Computer Systems],
  ),
)
#let conference = (
  name:  [ACM Symposium on Operating Systems Principles],
  short: [SOSP],
  year:  [2026],
  date:  [September 30-October 3],
  venue: [TBD],
)
#let doi = "https://doi.org/10.1145/0000000000"
#let ccs = (
  (
    generic: [Software and its engineering],
    specific: ([Virtual machines], [Virtual memory], ),
  ),
  (
    generic: [Computer systems organization],
    specific: ([Heterogeneous (hybrid) systems], ),
  ),
)
#let keywords = ("unikernels", "VirtIO-GPU", "graphics virtualization", "library operating systems", )

#show: acmart.with(
  title: title,
  authors: authors,
  affiliations: affiliations,
  conference: conference,
  doi: doi,
  copyright: "cc",
  // Set review to submission ID for the review process or to "none" for the final version.
  review: [\#001],
  font-size: 10pt,
  leading: 0.2em,
)

#show heading.where(level: 1): set block(
  above: 0.5em,
  below: 0.5em,
)

#show table.cell.where(x: 0): set text(style: "italic")
#show table.cell.where(y: 0): set text(style: "normal", weight: "bold")
#set table(
  stroke: (_, y) => if y > 0 { (top: 0.8pt) },
  inset: (x: 6pt, y: 4pt),
)

#include "sections/00-abstract.typ"

#acmart-ccs(ccs)
#acmart-keywords(keywords)
#acmart-ref(to-string(title), authors, conference, doi)

#include "sections/01-introduction.typ"
#include "sections/02-background-motivation.typ"
#include "sections/03-design-goals-challenges.typ"
#include "sections/04-system-overview.typ"
#include "sections/05-virtio-gpu-frontend-design.typ"
#include "sections/06-graphics-runtime-application-support.typ"
#include "sections/07-implementation.typ"
#include "sections/08-evaluation.typ"
#include "sections/09-discussion-limitations.typ"
#include "sections/10-related-work.typ"
#include "sections/11-conclusion.typ"

#bibliography("refs.bib", title: "References", style: "association-for-computing-machinery")

#pagebreak()
#include "sections/12-artifact-appendix.typ"

#pagebreak()
#include "sections/13-gpu-ecosystem-appendix.typ"


