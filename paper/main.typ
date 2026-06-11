#import "clean-acmart/0.0.1/clean-acmart.typ": acmart, acmart-ccs, acmart-keywords, acmart-ref, to-string

#let cuhk = super(sym.suit.spade)

#let title = [
  VOGUE: VirtIO-GPU on Unikraft for Graphics Virtualization
]

// ACM-style: each author carries its own affiliation lines. `email` is the
// mailto target on the name; the trailing `mail` line shows it as text.
#let ntu     = [National Taiwan University]
#let country = [Taipei, Taiwan]
#let authors = (
  (
    name: [Chao-Wei Tsai],
    email: "B11902040@csie.ntu.edu.tw",
    institution: ntu,
    country: country,
    mail: [B11902040\@csie.ntu.edu.tw],
  ),
  (
    name: [Min-Yao Chang],
    email: "B11902084@csie.ntu.edu.tw",
    institution: ntu,
    country: country,
    mail: [B11902084\@csie.ntu.edu.tw],
  ),
  (
    name: [Shau-Shun Tsao],
    email: "B11902145@csie.ntu.edu.tw",
    institution: ntu,
    country: country,
    mail: [B11902145\@csie.ntu.edu.tw],
  ),
)
#let affiliations = ()
#let conference = (
  name: [ACM Symposium on Operating Systems Principles],
  short: [SOSP],
  year: [2026],
  date: [September 30-October 3],
  venue: [TBD],
)
#let doi = "https://doi.org/10.1145/0000000000"

#show: acmart.with(
  title: title,
  authors: authors,
  affiliations: affiliations,
  conference: conference,
  doi: doi,
  // ACM copyright/permission block removed: not an ACM submission.
  copyright: none,
  // Set review to submission ID for the review process or to "none" for the final version.
  review: none,
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

// Paper rewritten from the slide deck (slides/). Structure: Abstract,
// Introduction, Background, Design, Implementation, Evaluation, Related Work,
// Conclusion. The previous section set (00–13) is archived in legacy/sections/.
#include "sections/00-abstract.typ"

#include "sections/01-introduction.typ"
#include "sections/02-background.typ"
#include "sections/03-design.typ"
#include "sections/04-implementation.typ"
#include "sections/05-evaluation.typ"
#include "sections/06-related-work.typ"
#include "sections/07-conclusion.typ"

#bibliography("refs.bib", title: "References", style: "association-for-computing-machinery")


