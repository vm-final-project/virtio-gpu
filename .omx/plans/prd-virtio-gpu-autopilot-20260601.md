# PRD: virtio-gpu blocked-gate cleanup + throughput optimization

Date: 2026-06-01
Workflow: autopilot / ralplan
Context snapshot: `.omx/context/1-check-the-virtio-gpu-plan-fix-md-to-fix-all-th-20260601T145021Z.md`
Online evidence note: `.omx/specs/virtio-gpu-online-evidence-20260601.md`

## Goal
Bring `virtio-gpu` to a cleaner, current, evidence-backed state by:
1. clearing the remaining actionable blocked gates called out in `plan-fix.md` and current generated artifacts,
2. removing stale or unrelated content that no longer matches the current project state,
3. diagnosing and improving the current low Unikraft Vulkan throughput with online-verified upstream guidance,
4. updating `paper/` to match the implementation and evidence,
5. updating `README.md` last so it reflects the final repo state.

## Ground truth at planning time
- `python3 scripts/current_stage_report.py --check` passes on 2026-06-01 (`pass checks=16 eval_rows=27`).
- The remaining blocker work is therefore in secondary/planned rows and stale artifacts, not the top-level current-stage gate.
- Measured throughput evidence:
  - `results/llama/upstream_vk_latest.json` (generated `2026-06-01T12:13:26.661974Z`): `pp512=247.4`, `tg128=3.4`.
  - `results/llama/upstream_cpu_latest.json` (generated `2026-06-01T12:19:47.846486Z`): `pp512=9.1`, `tg128=7.7`.
  - Interpretation: Vulkan prefill is strong, but token generation is slower than CPU and is likely dominated by submit/sync/transport overhead plus conservative runtime tuning.
- Current model-load evidence:
  - `results/model-load/latest.json` (generated `2026-06-01T12:19:21.186032Z`) reports `use_mmap=false`, `huge_pages=false` for all CPU/Vulkan bench/server appliances.
- Current server readiness evidence:
  - `results/llama/upstream_server_vk_latest.json` (generated `2026-06-01T12:13:47.386886Z`) reports `slots=1`, `ctx_per_slot=512`, `prompt_cache=true`, `hostmem_fixed=false`, and READY shows `threads=1`.
- Current repo-level bottlenecks visible in code:
  - `apps/app-llama-upstream-vk/{bench.cpp,server.cpp}` do **not** enable `UK_GGML_VK_DISPATCH_BATCH=1`, despite the fast path already existing in `libs/libukggml_vk/uk_vulkan_dispatch.c`.
  - Vulkan server exposes `--parallel`, `--ctx-size`, `--threads`, `--cache-prompt`, but not `--batch-size` / `--ubatch-size`.
  - Vulkan-related Kraftfiles pin helper threads to `1`.
- Current blocked/stale items requiring action:
  - `results/llama-bench/multi_env_bench_latest.*`: ENV9 remains `blocked:no-row-compatible-same-run-artifact`.
  - `results/app-multi-env/app_multi_env_latest.*`: QEMU Vulkan app rows still include `blocked:not-planned` / `blocked:render-payload-not-implemented`; app-glmark2 QEMU Vulkan must be classified as an explicit intentional bounded blocker/current non-goal unless it is trivially implementable within this cleanup.
  - `results/kmscube_vgpu_gl/latest/mesa-feasibility.json`: `blocked:feasibility-not-yet-implemented`.
  - `make paper` is still blocked by the snap-based Typst runtime on this host (`/snap/bin/typst` fails because snap resolves the wrong home directory).

## User constraints
- Must check online resources and use current best-practice/state-of-the-art methods where applicable.
- Must perform online/source verification before selecting implementation changes, not only during final reporting; record the dated source basis before committing to a code/config/result strategy.
- Must remove unrelated/stale content, but use the conservative cleanup rule: preserve historical evidence unless it is clearly stale and not needed by current gates, paper, or README.
- The final editing step must be `virtio-gpu/README.md`.

## Non-goals
- Do not invent unsupported acceleration claims.
- Do not remove historical evidence that is still referenced by the current evaluation matrix, paper, or README.
- Do not broaden into unrelated Unikraft/llama.cpp migrations outside `virtio-gpu`.

## Explicit stale-content inventory / preservation boundary
### Preserve
- generated evidence still cited by `results/vogue_latest_evaluation_matrix.*`, `paper/sections/08-evaluation.typ`, or `README.md`;
- host-conditional blocker wording that is still honest and current;
- the current 27/27 PASS story and supporting same-run artifacts.

### Candidate stale targets
- resolved-blocker prose that still implies the old pre-Venus or pre-27/27 state;
- legacy llama-runtime or discovery-only references that contradict the current upstream Vulkan/runtime path;
- generated summaries whose blocker wording no longer matches implemented gates after this work;
- stale paper/README sections that mention blockers removed by fresh evidence in this execution.

## Scope
### In scope
1. **Blocked-gate repair**
   - Clear the actionable blockers that can be repaired from this repo/workspace:
     - ENV9 terminal outcome: either produce a same-run llvmpipe artifact path or regenerate narrowed blocker wording that explicitly states why same-run capture remains blocked in this host/session.
     - QEMU `app-vulkan-smoke` bounded render-payload/frame-proof gate.
     - QEMU `app-vkmark` bounded render-payload/frame-proof gate, or if full scene execution remains too broad, narrow the row/claim language to the implemented bounded proof and remove stale wording.
     - QEMU `app-glmark2` Vulkan row: treat as an intentional bounded blocker/current non-goal unless the missing render payload/frame-proof path is trivially implementable from existing in-tree pieces during this cleanup.
     - Typst runtime path so `make paper` works without the broken snap environment.
     - Mesa feasibility result updated to either implemented bounded evidence or an explicit out-of-scope/current-state decision that matches the rest of the repo.
2. **Throughput optimization**
   - Apply low-risk, measurable throughput fixes first:
     - enable the existing dispatch batching fast path in real Vulkan bench/server runs;
     - expose and tune `--batch-size` / `--ubatch-size` / `--parallel` / helper-thread knobs for the Vulkan server path;
     - tune Kraftfile defaults where evidence justifies it;
     - improve aggregate or per-token throughput without invalidating current claims.
3. **Stale-content cleanup**
   - Remove or rewrite stale docs/results/sections that still describe already-resolved blockers or legacy paths not representative of the current codebase.
4. **Docs/paper refresh**
   - Update `paper/` to match the repaired gates and tuned implementation.
   - Update `README.md` last.

### Out of scope unless already nearly complete in-tree
- Large architectural rewrites such as paged-KV systems or speculative decoding adoption.
- Host/QEMU changes outside what can be captured, configured, or documented from this workspace.

## Online evidence to honor during execution
Before selecting implementation changes, execution must update or stage `.omx/specs/virtio-gpu-online-evidence-20260601.md` with the exact dated sources used and explain why each source supports or rejects the candidate change. Final reporting alone is insufficient; source verification is an input to implementation selection. At minimum, verify current upstream/official material for:
- QEMU VirtIO-GPU / Venus hostmem/blob requirements,
- Mesa Venus host-visible mapping behavior,
- llama.cpp server throughput knobs and current Vulkan-related tuning guidance.

## Product requirements / acceptance criteria
1. **Gate cleanup**
   - All repo-generated “current state” artifacts affected by this work are regenerated.
   - Planned blocker rows that were previously stale or unimplemented are either:
     - converted to PASS with same-run evidence, or
     - rewritten to a narrower, explicit, non-stale blocker that matches the actual implemented scope and current host constraints.
2. **Throughput optimization**
   - The root cause of low `tg128` is documented with evidence from repo measurements plus dated online sources.
   - At least one concrete throughput optimization lands in code and is validated with fresh evidence.
   - Baseline for acceptance is the 2026-06-01 Vulkan artifact `pp512=247.4`, `tg128=3.4`.
   - When the host permits runtime measurement, collect at least 3 post-change repetitions and compare medians.
   - Target `tg128` median is at least +10% over baseline; rollback or narrow the optimization claim if the median regresses by >=5%, shows instability, or causes a material `pp512` regression.
   - Fresh artifacts must prove which throughput knobs were active (dispatch batching, threads, parallel, ctx, batch/ubatch if added).
3. **Cleanup quality**
   - Removed or updated stale content is limited to the inventory above and does not delete live evidence still needed by current gates/paper/README.
4. **Paper/README**
   - `paper/` and generated artifacts match the implemented state.
   - `README.md` is the final edited file and accurately reflects the final repo state.

## Risks
- Some blocked rows depend on QEMU/host GPU capabilities and may not be fully clearable if the host stack cannot reproduce them in this session.
- `hostmem_fixed=false` is currently an honest transport limitation and may remain unresolved if it depends on host/QEMU behavior not controllable here.
- Full vkmark or glmark2 scene execution may exceed bounded scope; if so, the row language must be narrowed instead of overstated, and app-glmark2 QEMU Vulkan remains an explicit current non-goal unless trivially implementable.

## Execution order
1. Repair planning/reporting mismatches and stale blockers.
2. Implement/measurably tune the Vulkan throughput path.
3. Regenerate evidence and gates.
4. Update `paper/`.
5. Update `README.md` last.
