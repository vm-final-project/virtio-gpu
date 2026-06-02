# VOGUE Blocked-Gate Fix Plan

This file records the current blockers from the live worktree and the exact
repair path for each one. The release evidence matrix is currently 27/27 PASS on
the evaluation host, and `make current-stage-check` now passes. Remaining items
are the last reporting/documentation gaps plus future non-release gates.

## Resolved Current-Stage Blocker

| Gate | Status | Reason | Fix | Verification |
|---|---|---|---|---|
| `make current-stage-check` / `real_path_selected` | `pass` | `.unikraft/build/config` currently belongs to a CPU-only image, but production Kraftfiles, `config/.config.vogue_qemu-x86_64`, `virtio_gpu_real.o`, the compile database, and QEMU Venus probe are pass. | Implemented: `scripts/real_virtio_gpu_path_check.py` treats non-GPU latest builds as not applicable while still requiring real-backend evidence for graphics/Vulkan production paths. | `make real-path-check && make current-stage-check` |

## Reporting And Regeneration Gaps

| Artifact | Current problem | Fix | Verification |
|---|---|---|---|
| `results/app-multi-env/app_multi_env_latest.*` | Resolved: the regenerated matrix now promotes `app-kmscube` QEMU/Venus to `pass`, marks llama CPU/Vulkan server rows from same-run runtime JSON, and leaves only intentional `blocked:not-planned` / `blocked:render-payload-not-implemented` rows. | Implemented in `scripts/app_multi_env_bench.py`: consume `results/venus/qemu_venus-ring_probe_latest.json`, `results/kmscube_vgpu_gl/latest/frame_pixel_proof.json`, and `results/llama/upstream_*_latest.json`; regenerate the report and Typst table. | `python3 scripts/app_multi_env_bench.py`; inspect `results/app-multi-env/app_multi_env_latest.{json,md}` and `paper/generated/app-multi-env-table.typ`. |
| `results/llama-bench/multi_env_bench_latest.*` | Partially resolved: ENV10 now regenerates as `pass` from `results/llama/env10_real_latest.json` / `results/llama/upstream_vk_latest.json`; ENV9 remains blocked because there is still no row-compatible same-run Unikraft+Venus llvmpipe artifact. | Implemented in `scripts/multi_env_bench.py`: use the current `vogue-llama-upstream-vk_qemu-x86_64` runtime artifacts for ENV10, map ENV9 to the llvmpipe row defined in `config/bench_env.yaml`, and keep ENV9 blocked until a real llvmpipe artifact exists. Next unblock: add an explicit `ENV9` runner variant that forces lavapipe/lvp ICD in the guest, emits `results/llama/env9_llvmpipe_latest.json` with the same schema as `env10_real_latest.json`, and wire that JSON into `scripts/multi_env_bench.py`. | `python3 scripts/multi_env_bench.py --skip-vm`; then add `make llama-env9-llvmpipe-check` and confirm ENV9 reports row-compatible `pp512` / `tg128` or a structured llvmpipe-specific blocker. |
| Paper generated-table source paths | Resolved: the generated Typst tables regenerate from current matrix data and the PDF build no longer depends on the broken local snap runtime. | Use the official standalone Typst binary under `.tools/typst/typst-x86_64-unknown-linux-musl/typst` and keep `make paper` overrideable through `TYPST=/path/to/typst`. | `make paper-check`; then `TYPST=.tools/typst/typst-x86_64-unknown-linux-musl/typst make paper`. |

## Non-Release Future Blockers

| Area | Current reason | Fix path | Gate to add or rerun |
|---|---|---|---|
| Full HTTP llama-server semantics | `llm.server.cpu` and `llm.server.vk` prove direct entrypoint and model-loaded readiness only; no TCP/IP serving claim is made. | Add the Unikraft lwIP/netdev path, expose the upstream server listener, and add a minimal request harness that checks `/health` or a one-prompt completion before any throughput numbers are quoted. After that, measure request/response, TTFT, and aggregate throughput with the same model as the readiness artifact. | Extend `make llm-server-vk-check` with an HTTP probe phase and add a new `make llm-server-vk-throughput-check` for requests/s, TTFT, and slot utilisation. |
| vkmark and Vulkan-smoke QEMU rendering | `vulkan_perf_latest.json` still marks generic rendering as `blocked:frame-proof-missing`; current pass rows prove substrate and kmscube frame proof, not full vkmark scene FPS in Unikraft. | Implement non-empty render payloads for `app-vulkan-smoke` and `app-vkmark`: first make `app-vulkan-smoke` submit one bounded clear/dispatch/copyback workload and persist a pixel/hash artifact, then reuse the same image-capture path for `app-vkmark` scene screenshots and per-scene FPS capture. Promote rows only when the artifact contains both same-run image proof and scene/runtime metadata. | Add `make vulkan-smoke-qemu-check` for the bounded payload path, then `make vkmark-qemu-check`; rerun `make vulkan-check` and `make app-multi-env-bench`. |
| Bounded Mesa EGL/GLES feasibility | `results/kmscube_vgpu_gl/latest/mesa-feasibility.json` is `blocked:feasibility-not-yet-implemented`. | Define the smallest static Mesa EGL/GLES/virgl slice compatible with Unikraft, or explicitly keep this out of scope and rely on the bounded virgl CLEAR path. | `make kmscube-check` plus a new feasibility gate if Mesa is in scope. |

## Execution Order

1. Keep generated summaries aligned with the current matrix and add the missing
   ENV9 llvmpipe artifact path.
2. Replace the broken snap-based Typst runtime, then rebuild the PDF from the
   already-aligned generated tables.
3. Land the bounded QEMU render-payload path for `app-vulkan-smoke`, then reuse
   it for `app-vkmark`.
4. Add the HTTP serving gate and only then quote llama-server throughput.
5. Treat Mesa EGL/GLES feasibility as optional scope until it has either a
   bounded implementation plan or an explicit out-of-scope decision.
