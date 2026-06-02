# Canonical Autopilot Context Snapshot

Date: 2026-06-01
Task: check `virtio-gpu/plan-fix.md`, remove stale/unrelated `virtio-gpu` content, diagnose and optimize low Unikraft performance using online resources, update `paper/`, update `README.md` last.

## Desired outcome
- Clear the actionable blocked gates still tracked by `plan-fix.md` and current generated artifacts.
- Preserve historical evidence only when still needed by current gates, paper, or README.
- Improve the Unikraft Vulkan throughput path with measured, evidence-backed changes.
- Update paper and finish with README last.

## Known facts
- `current_stage_report.py --check` passes on 2026-06-01.
- Secondary/planned blockers remain in ENV9, Vulkan app render-payload rows, Mesa feasibility, and Typst runtime.
- Real throughput snapshot on 2026-06-01: Vulkan `pp512=247.4`, `tg128=3.4`; CPU `pp512=9.1`, `tg128=7.7`.
- Current code leaves the `UK_GGML_VK_DISPATCH_BATCH=1` fast path disabled in real bench/server runs.
- Current Vulkan server path exposes `--parallel`/`--ctx-size`/`--threads`/`--cache-prompt`, but not `--batch-size` or `--ubatch-size`.
- Current model-load logs show `use_mmap=false`, `huge_pages=false`.

## Cleanup boundary
Preserve:
- current generated evidence used by the evaluation matrix,
- same-run artifacts cited by paper/README,
- host-conditional blockers that are still honest and current.

Target stale candidates:
- outdated resolved-blocker prose,
- obsolete legacy-llama/legacy-runtime references that no longer match the current 27/27 PASS matrix,
- generated summaries whose blocker wording no longer matches current implementation.

## Online evidence note requirement
Execution must leave a dated note with the sources actually used for throughput/QEMU/Mesa/llama.cpp decisions.
