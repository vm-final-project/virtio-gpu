# Current-stage completeness report

Status: `pass`

| Check | Status | Evidence | Required property |
|---|---|---|---|
| `make_targets` | `pass` | present=['benchmark-check', 'claim-check', 'eval-check', 'governance-check', 'lib-readme-check', 'native-tests', 'stage-check', 'test-fast', 'test-gpu', 'test-native', 'test-qemu', 'venus-check', 'verify', 'vulkan-check', 'vulkan-tests'] missing=[] | Makefile exposes all top-level test/evaluation/benchmark gates |
| `script_surface` | `pass` | present=['app_perf_eval.py', 'benchmark_summary.py', 'eval_matrix.py', 'governance_check.py', 'lib_readme_check.py', 'llama_env_matrix.py', 'llama_vulkan_api_coverage.py', 'llama_vulkan_eval.py', 'real_driver_static_check.py', 'real_virtio_gpu_path_check.py', 'stage_audit.py', 'unikraft_alignment_check.py', 'venus_perf_eval.py', 'venus_qemu_probe.py', 'vulkan_perf_eval.py'] missing=[] | All expected evaluation and guardrail scripts exist |
| `unikraft_alignment` | `pass` | results/stage/unikraft_alignment.json | Unikraft design-rule alignment gate passes |
| `stage_audit` | `pass` | results/stage/stage_audit.json | Current-stage audit passes |
| `benchmark_summary` | `pass` | rows=8 results/benchmarks/benchmark_summary.json | Benchmark summary exists with native app and Venus readiness rows |
| `evaluation_matrix` | `pass` | rows=26 missing=[] | Evidence matrix contains every supported pass/blocked claim row |
| `pass_rows` | `pass` | results/vogue_evaluation_matrix.json | Core supported rows pass; upstream llama.cpp rows accept structured blockers for missing QEMU/Venus images |
| `blocked_rows_are_explicit` | `pass` | gfx.kmscube.submit+gfx.kmscube.frame present in matrix; STK porting out of scope | K1 transport/frame rows are present; STK porting explicitly dropped |
| `venus_blocker_recorded` | `pass` | results/venus/qemu_2d_probe.json; results/vogue_evaluation_matrix.json | QEMU Venus probe artifact recorded with a structured status |
| `real_path_selected` | `pass` | results/venus/real_path_check.json | Production Kraft/config/build artifacts use the real VirtIO-GPU backend |
| `stk_out_of_scope` | `pass` | design/unikraft-virtio-gpu-spec-v1.md | STK porting is documented as out of scope (plan.md §0.5) |
| `library_readmes` | `pass` | libs=4 missing=[] | Every local library has Unikraft-style README docs |
| `readme_current_stage` | `pass` | README.md | README exposes current-stage/evaluation commands and fix plan |
| `governance_metadata` | `pass` | docs/GOVERNANCE.md; config/governance.json; ../manifest/manifests/vogue-main.yaml | Research-artifact governance metadata and manifest/VM ownership split are documented |

## Claim boundary

Current supported rows pass on the evaluation host, including K1/xport.qemu-vgpu and llama.cpp Vulkan runtime rows. The same rows must not be promoted on other hosts without same-run pass artifacts. Governance metadata links release claims to ../manifest specs and marks unlinked claims non-release; STK porting is out of scope (plan.md §0.5).
