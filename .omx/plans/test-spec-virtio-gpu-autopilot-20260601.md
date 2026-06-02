# Test Spec: virtio-gpu blocked-gate cleanup + throughput optimization

Date: 2026-06-01
Workflow: autopilot / ralplan
PRD: `.omx/plans/prd-virtio-gpu-autopilot-20260601.md`
Online evidence note: `.omx/specs/virtio-gpu-online-evidence-20260601.md`

## Validation strategy
Use the smallest validation that can prove each claim, then expand to repo-wide gates only where necessary.

## Pre-implementation source gate
Before choosing implementation changes, verify the relevant upstream/official sources and record them in `.omx/specs/virtio-gpu-online-evidence-20260601.md` with the candidate decision they support or reject. Do not defer source verification to final reporting only.

## Fresh evidence required before completion
- `python3 scripts/current_stage_report.py --check`
- `python3 scripts/eval_matrix.py --check` (or `make eval-check` if dependencies are already built)
- `python3 scripts/multi_env_bench.py --skip-vm`
- `python3 scripts/app_multi_env_bench.py`
- `python3 scripts/vulkan_perf_eval.py --repetitions 3 --allow-blocked`
- `python3 scripts/model_load_time_check.py --check`
- `python3 scripts/llm_server_vk_check.py --check`
- `python3 scripts/paper_consistency_check.py`
- `make paper` (using a non-snap Typst binary/path if needed)

## Targeted validation by workstream
### A. Throughput/runtime changes
If Vulkan bench/server code or Kraftfiles change:
- rebuild the affected images (`make llama-upstream-vk-build` and/or `make llama-upstream-vk-server-build`);
- rerun the matching runtime capture(s):
  - `python3 scripts/llama_vk_real_run.py` or `make llama-upstream-vk-run`
  - `python3 scripts/llama_server_vk_capture.py` (there is no `make llama-upstream-vk-server-run` target today)
- regenerate downstream summaries:
  - `python3 scripts/model_load_time_check.py --check`
  - `python3 scripts/llm_server_vk_check.py --check`
  - `python3 scripts/multi_env_bench.py --skip-vm`
- compare updated `pp512`, `tg128`, READY-line configuration fields, and any new throughput metadata against the pre-change Vulkan baseline dated 2026-06-01: `pp512=247.4`, `tg128=3.4`.
- when the host permits runtime measurement, run at least 3 post-change repetitions and compare medians; accept only if `tg128` median improves by at least 10%, there is no material `pp512` regression, and results are stable.
- rollback or narrow the optimization claim when the median regresses by >=5%, shows instability, or causes a material `pp512` regression.
- prove active optimization knobs in fresh evidence:
  - `UK_GGML_VK_DISPATCH_BATCH=1` must be observable in runtime/summary output or a new artifact field,
  - any new `batch-size`, `ubatch-size`, `parallel`, `ctx`, and `threads` settings must be recorded in generated JSON or READY lines.

### B. ENV9 llvmpipe gate
If an ENV9 runner is added:
- produce a same-run llvmpipe-targeted artifact with the same schema family as `results/llama/env10_real_latest.json`;
- rerun `python3 scripts/multi_env_bench.py --skip-vm`;
- confirm ENV9 reaches an explicit terminal outcome: either it is no longer `blocked:no-row-compatible-same-run-artifact` because a same-run llvmpipe artifact exists, or the generated result/summary contains narrowed blocker wording explaining why same-run capture remains blocked for this host/session.

### C. Vulkan-smoke / vkmark / glmark2 render payloads
If bounded render payload/frame-proof work lands:
- run the new/updated gate commands for smoke/vkmark;
- regenerate `results/vulkan/vulkan_perf_latest.{json,md}` and `results/app-multi-env/app_multi_env_latest.{json,md}`;
- confirm the relevant rows no longer use stale `blocked:render-payload-not-implemented` / `blocked:not-planned` wording.
- classify app-glmark2 QEMU Vulkan as an intentional bounded blocker/current non-goal unless it is trivially implementable from existing in-tree pieces during this cleanup; regenerated rows must say this explicitly instead of leaving stale generic blocker wording.

### D. Stale-content cleanup
For every removed or rewritten stale doc/result/section:
- verify the replacement matches current generated evidence and no checked script still references the removed content.
- run `python3 scripts/paper_consistency_check.py`, `python3 scripts/lib_readme_check.py --check`, and `python3 scripts/naming_check.py` if touched.

### E. Paper
- `python3 scripts/paper_consistency_check.py`
- `make paper`
- verify generated tables under `paper/generated/` reflect the updated artifacts.

### F. README (must be last edit)
After all code/results/paper changes are complete:
- update `README.md` last;
- rerun at minimum:
  - `python3 scripts/lib_readme_check.py --check`
  - `python3 scripts/paper_consistency_check.py`
  - any targeted scripts whose claims are mentioned in README.

### G. Online evidence note
Before implementation selection and again before final completion if sources changed, update `.omx/specs/virtio-gpu-online-evidence-20260601.md` with:
- source title,
- URL,
- why it mattered to an implementation/tuning decision,
- verification date (`2026-06-01` or later).

## Completion bar
The story is ready to hand off to execution only if the plan supports these terminal checks:
1. No stale claim remains in README/paper/results for the touched areas.
2. Throughput diagnosis and at least one measurable optimization are implemented and evidenced.
3. The updated repo state is reproducible through the listed gates.
4. `README.md` is the final edited file in the execution sequence.
