# VOGUE Fix Plan — status

**All release-gating blockers are resolved.** The evaluation matrix is
**27/27 PASS, 0 blocked** and `make current-stage-check` passes. The items that
were previously tracked here as blockers have all landed; their details now live
in the code and docs they touched (README, `apps/*/PORTING.md`, the scripts and
Kraftfiles), not in this file. What remains below are **deliberate scope
decisions**, not open problems.

## Resolved (no longer tracked)

| Former blocker | How it was closed | Verification |
|---|---|---|
| Current-stage real-path gate | `scripts/real_virtio_gpu_path_check.py` treats a non-GPU latest build as not-applicable while still requiring real-backend evidence for the graphics/Vulkan production paths. | `make real-path-check && make current-stage-check` |
| `app-multi-env` / `multi_env_bench` ENV10 / paper generated tables | Regenerate cleanly from same-run artifacts; the PDF builds from the standalone Typst binary (`TYPST=…`). | `python3 scripts/app_multi_env_bench.py`; `make paper-check`; `make eval` → 27/27 |
| **HTTP llama-server serving (`llm.server.vk`)** | The Vulkan server appliance wires a real in-guest TCP/IP stack (`virtio-net → libuknetdev → lwIP`, DHCP; `/dev/urandom` via `ukrandom` devfs) and hands control to the upstream `llama_server()` listener (`--host 0.0.0.0 --port 8080 --no-mmap`). A same-run host probe records `/health → 200`, `/v1/models → 200`, `/completion → 200`. | `make llama-upstream-vk-server-build` → `python3 scripts/llama_server_vk_capture.py` → `make llm-server-vk-check` (`runtime=pass http=pass`). See README "Running the production llama.cpp Vulkan HTTP server". |
| **HTTP throughput / TTFT numbers** | `make llm-server-vk-throughput-check` boots the server and drives a bounded completion burst, recording *measured* requests/s, tokens/s, and TTFT (no synthetic estimate). Latest same-run artifact: **2.61 req/s, 83.51 tokens/s, TTFT 0.338 s** (8/8 OK) on a Tesla V100 over Venus. | `make llm-server-vk-throughput-check` → `results/llama/server_vk_throughput.json` (`status:"pass"`). |

Reproduction notes for the lwIP path (sibling `../lib-lwip`, pinned to
`RELEASE-0.21.0` to match `../unikraft`) are retained for maintainers:

- `../lib-lwip` ships libc-overlay headers (`sys/socket.h`, `netinet/in.h`,
  `netinet/tcp.h`, `netdb.h`, `net/if.h`, `arpa/inet.h`) that shadow musl's.
  This workspace's musl owns the complete socket headers, so those overlays were
  made to `#include_next` musl under `CONFIG_HAVE_LIBC` (lwIP keeps the nolibc
  copies behind `#else`).
- `../unikraft/lib/ukpod/{anon,eager}.c` were missing `#include <uk/assert.h>`
  (an upstream bug surfaced only when `LIBUKRANDOM_DEVFS` pulls in the new
  `ukfs-ramfs`/`ukpod` stack); a one-line include was added to each.

## Deliberate scope decisions (not blockers)

These are intentional boundaries, each consistent with VOGUE's thesis (real GPU
acceleration over Venus through a thin Unikraft stack) and the *one image, one
purpose* rule. Each leaves a documented, structured `blocked:*` row where a row
exists — never reported as acceleration/throughput evidence.

| Area | Decision | Rationale | Revisit when |
|---|---|---|---|
| CPU server HTTP (`llm.server.cpu`) | Kept **readiness-only** (loads the model, prints `READY`, idles). | The production HTTP server is the GPU-accelerated `llm.server.vk`, which already proves the full upstream HTTP path. Promoting the CPU image would duplicate the entire upstream server wiring (`server-*.cpp`, `mtmd`, `jinja`, `httplib`, common) for **no new capability** on a non-accelerated backend. | A CPU-only HTTP deployment is explicitly required; then mirror the VK wiring (cmake `LLAMA_BUILD_SERVER=ON`, `llama_server(--no-mmap)`, lwIP/netdev + `ukrandom` devfs Kconfig) and extend `scripts/llama_server_cpu_capture.py`. |
| vkmark / Vulkan-smoke QEMU scene FPS | Out of scope for now; substrate + kmscube frame proof stand. | `vulkan_perf.json` keeps `blocked:frame-proof-missing` as a documented structured blocker; full Vulkan scene FPS in Unikraft is a separate research payload, not a release claim. | Vulkan scene benchmarking enters scope: add a bounded clear/dispatch/copyback payload to `app-vulkan-smoke`, persist a pixel/hash artifact, then reuse it for `app-vkmark` (`make vulkan-smoke-qemu-check`, `make vkmark-qemu-check`). |
| ENV9 llvmpipe llama bench | Out of scope. | VOGUE's evidence is real GPU acceleration over Venus; a software-Vulkan-in-guest (lavapipe) row adds no acceleration evidence. `scripts/multi_env_bench.py` keeps ENV9 as a structured blocker. | A software-Vulkan baseline is explicitly required; then add an ENV9 runner that forces the lvp ICD in the guest and emits `results/llama/env9_llvmpipe.json` with the `env10_real.json` schema. |
| Bounded Mesa EGL/GLES feasibility | Out of scope. | The bounded virgl `CLEAR` path already provides the graphics evidence; importing a static Mesa EGL/GLES/virgl slice into the guest is explicitly not pursued. `results/kmscube_vgpu_gl/run/mesa-feasibility.json` stays `blocked:feasibility-not-yet-implemented`. | A Mesa-in-guest path becomes a project goal. |
