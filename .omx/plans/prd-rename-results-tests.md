# PRD: rename canonical tests/results and remove `latest`

## Goal
Make `virtio-gpu` test and result naming concise and clear by removing `latest` from canonical artifact names, updating every reference across the repo, and tightening the `tests/` layout/docs so the native test surface is easier to follow.

## Scope
- Rename canonical result artifacts under `results/` from `*latest*` names to stable names.
- Rename/update references in scripts, Makefiles, docs, plans, paper, and generated markdown/json where paths are embedded.
- Reorganize `tests/` documentation and build grouping so native coverage is expressed as clear suites.
- Update `README.md` and related project docs.

## Non-goals
- Rewriting historical benchmark semantics.
- Changing measured values or claim boundaries beyond path/name updates.
- Reworking runtime logic unrelated to artifact naming.

## Decisions
- Canonical artifacts use stable filenames with no `latest` token.
- Existing historical run-specific artifacts under `post_opt_runs/` remain unchanged.
- Native test suite remains source-compatible; reorganization focuses on clearer grouping and result naming.

## Touchpoints
`tests/**`, `scripts/**`, `results/**`, `README.md`, `Makefile`, `plan-*.md`, `apps/**`, `libs/**`, `paper/**`, `docs/**`, `config/**`.
