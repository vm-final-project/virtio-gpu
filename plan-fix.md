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
| `results/app-multi-env/app_multi_env.*` | Resolved: the regenerated matrix now promotes `app-kmscube` QEMU/Venus to `pass`, marks llama CPU/Vulkan server rows from same-run runtime JSON, and leaves only intentional `blocked:not-planned` / `blocked:render-payload-not-implemented` rows. | Implemented in `scripts/app_multi_env_bench.py`: consume `results/venus/qemu_venus-ring_probe.json`, `results/kmscube_vgpu_gl/run/frame_pixel_proof.json`, and `results/llama/upstream_*.json`; regenerate the report and Typst table. | `python3 scripts/app_multi_env_bench.py`; inspect `results/app-multi-env/app_multi_env.{json,md}` and `paper/generated/app-multi-env-table.typ`. |
| `results/llama-bench/multi_env_bench.*` | Partially resolved: ENV10 now regenerates as `pass` from `results/llama/env10_real.json` / `results/llama/upstream_vk.json`; ENV9 remains blocked because there is still no row-compatible same-run Unikraft+Venus llvmpipe artifact. | Implemented in `scripts/multi_env_bench.py`: use the current `vogue-llama-upstream-vk_qemu-x86_64` runtime artifacts for ENV10, map ENV9 to the llvmpipe row defined in `config/bench_env.yaml`, and keep ENV9 blocked until a real llvmpipe artifact exists. Next unblock: add an explicit `ENV9` runner variant that forces lavapipe/lvp ICD in the guest, emits `results/llama/env9_llvmpipe.json` with the same schema as `env10_real.json`, and wire that JSON into `scripts/multi_env_bench.py`. | `python3 scripts/multi_env_bench.py --skip-vm`; then add `make llama-env9-llvmpipe-check` and confirm ENV9 reports row-compatible `pp512` / `tg128` or a structured llvmpipe-specific blocker. |
| Paper generated-table source paths | Resolved: the generated Typst tables regenerate from current matrix data and the PDF build no longer depends on the broken local snap runtime. | Use the official standalone Typst binary under `.tools/typst/typst-x86_64-unknown-linux-musl/typst` and keep `make paper` overrideable through `TYPST=/path/to/typst`. | `make paper-check`; then `TYPST=.tools/typst/typst-x86_64-unknown-linux-musl/typst make paper`. |

## Resolved: HTTP llama-server serving (was a future blocker)

| Area | Status | What landed | Verification |
|---|---|---|---|
| Full HTTP llama-server semantics (`llm.server.vk`) | `pass` | The Vulkan server appliance now wires a real in-guest TCP/IP stack: `virtio-net-pci -> libuknetdev -> lwIP` (DHCP from QEMU user-mode networking, `/dev/urandom` via `ukrandom` devfs). `apps/app-llama-upstream-vk/server.cpp` hands control to the upstream `llama_server()` listener (`--host 0.0.0.0 --port 8080 --no-mmap`), which binds and serves. `scripts/llama_server_vk_capture.py` boots the image over real Venus on the V100, then issues same-run host→guest HTTP requests through a `hostfwd` port: `GET /health` → `200 {"status":"ok"}`, `GET /v1/models` → `200`, `POST /completion` → `200` with `tokens_predicted=16`. `scripts/llm_server_vk_check.py` records the HTTP phase. | `make llama-upstream-vk-server-build` then `python3 scripts/llama_server_vk_capture.py` (writes `results/llama/upstream_server_vk.json` with `status:"pass"` and the `http` proof); `make llm-server-vk-check` prints `runtime=pass http=pass`. |

Integration notes for reproducing the lwIP path (sibling `../lib-lwip`, pinned to
`RELEASE-0.21.0` to match `../unikraft`):
- `../lib-lwip` ships libc-overlay headers (`sys/socket.h`, `netinet/in.h`,
  `netinet/tcp.h`, `netdb.h`, `net/if.h`, `arpa/inet.h`) that shadow musl's. This
  workspace's musl owns the complete socket headers, so those overlays were made
  to `#include_next` musl under `CONFIG_HAVE_LIBC` (lwIP keeps the nolibc copies
  behind `#else`). Without this they redefine/short-circuit musl's types.
- `../unikraft/lib/ukpod/{anon,eager}.c` were missing `#include <uk/assert.h>`
  (an upstream bug surfaced only when `LIBUKRANDOM_DEVFS` pulls in the new
  `ukfs-ramfs`/`ukpod` stack); a one-line include was added to each.

## Remaining HTTP follow-up (not blocking the serving claim)

| Area | Current reason | Fix path | Gate to add or rerun |
|---|---|---|---|
| HTTP throughput / TTFT numbers | The gate proves same-run HTTP liveness + one bounded completion, not aggregate requests/s, TTFT, or slot utilisation — those are still **not** claimed. | Drive a fixed request workload against the running appliance with the same model and record per-request latency, TTFT, and slot occupancy. | Add `make llm-server-vk-throughput-check` (requests/s, TTFT, slot utilisation). |
| CPU server HTTP (`llm.server.cpu`) | `apps/app-llama-upstream/server.cpp` is still readiness-only (loads the model, prints READY, idles) and its cmake builds with `-DLLAMA_BUILD_SERVER=OFF`. | Reuse the VK server wiring: build `llama-server-impl` for the CPU toolchain, call `llama_server()` with `--no-mmap`, and add the same lwIP/netdev + `ukrandom` devfs Kconfig to `kraft/Kraftfile.llama-upstream-server`. | Extend `scripts/llama_server_cpu_capture.py` with the same HTTP probe. |
| vkmark and Vulkan-smoke QEMU rendering | `vulkan_perf.json` still marks generic rendering as `blocked:frame-proof-missing`; current pass rows prove substrate and kmscube frame proof, not full vkmark scene FPS in Unikraft. | Implement non-empty render payloads for `app-vulkan-smoke` and `app-vkmark`: first make `app-vulkan-smoke` submit one bounded clear/dispatch/copyback workload and persist a pixel/hash artifact, then reuse the same image-capture path for `app-vkmark` scene screenshots and per-scene FPS capture. Promote rows only when the artifact contains both same-run image proof and scene/runtime metadata. | Add `make vulkan-smoke-qemu-check` for the bounded payload path, then `make vkmark-qemu-check`; rerun `make vulkan-check` and `make app-multi-env-bench`. |
| Bounded Mesa EGL/GLES feasibility | `results/kmscube_vgpu_gl/run/mesa-feasibility.json` is `blocked:feasibility-not-yet-implemented`. | Define the smallest static Mesa EGL/GLES/virgl slice compatible with Unikraft, or explicitly keep this out of scope and rely on the bounded virgl CLEAR path. | `make kmscube-check` plus a new feasibility gate if Mesa is in scope. |

## Execution Order

1. Keep generated summaries aligned with the current matrix and add the missing
   ENV9 llvmpipe artifact path.
2. Replace the broken snap-based Typst runtime, then rebuild the PDF from the
   already-aligned generated tables.
3. Land the bounded QEMU render-payload path for `app-vulkan-smoke`, then reuse
   it for `app-vkmark`.
4. Done: the HTTP serving path (`llm.server.vk`) lands lwIP/netdev + the
   upstream listener with a same-run `/health` + `/completion` probe. Only
   quote llama-server *throughput* once `llm-server-vk-throughput-check` exists.
5. Treat Mesa EGL/GLES feasibility as optional scope until it has either a
   bounded implementation plan or an explicit out-of-scope decision.
