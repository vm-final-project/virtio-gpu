# PRD: virtio-gpu paper SOSP refresh

## Objective
Refresh `paper/` into a conference-quality, current-stage paper that accurately reflects the repo's 2026-06-02 state, improves narrative flow, removes stale claims, adds a dependency-analysis section grounded in `docs/ARCHITECTURE.md` and `results/depgraph/`, and leaves `README.md` aligned with the rewritten paper.

## Scope
- Rewrite paper sections for clarity, concision, and current evidence.
- Remove stale low-throughput/stale legacy-substrate claims.
- Add an explicit dependency-analysis discussion comparing Linux DRM/Mesa dependency towers against the collapsed VOGUE library chain and the protocol seam shared with QEMU.
- Audit `paper/refs.bib`; remove or stop citing irrelevant entries and ensure cited references exist and match claims.
- Keep claims evidence-gated and consistent with generated artifacts and current-stage checks.
- Update `README.md` after paper edits to reflect the final stage.

## Non-goals
- Adding new runtime features or new measurements beyond already-generated project artifacts.
- Claiming HTTP throughput, full vkmark rendering, or ENV9 llvmpipe success.
- Reworking the entire artifact appendix into a different publication model unless required to remove stale content.

## Required paper outcomes
1. Abstract and introduction emphasize the actual contribution: a bounded graphics/Vulkan substrate on Unikraft with evidence-gated claims and current runtime results.
2. Background/design sections explain why the dependency collapse matters and what is deliberately not imported from Linux/Mesa.
3. System/implementation sections include an explicit dependence analysis tied to `docs/ARCHITECTURE.md` and `results/depgraph` views A/B/C.
4. Evaluation reports current numbers only, including the repaired Vulkan throughput and current PASS matrix.
5. Discussion/limitations honestly frame remaining gaps: HTTP serving, model-load optimization, ENV9, and render-payload work.
6. Artifact appendix contains no stale legacy rows that contradict the current matrix.

## Acceptance criteria
- `paper_consistency_check.py` passes.
- Paper builds with the standalone Typst binary already staged under `.tools/typst/.../typst`.
- Citation keys used in sections exist in `paper/refs.bib` and obviously match the claims they support.
- README reflects the refreshed paper/project stage and is edited last.
