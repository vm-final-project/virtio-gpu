# app-glmark2 — glmark2 scene-clear Unikraft port

## Upstream provenance

- **Repository**: <https://github.com/glmark2/glmark2.git>
- **Commit**: `22c527cb0556f3a1ac4445aaa52cc532760928d5`
- **License**: GPL-3.0
- **Upstream areas mapped**: `src/scene-clear.cpp` and the benchmark main-loop/reporting shape from `src/main.cpp`

## Evidence

| Row | Status | Claim |
|-----|--------|-------|
| `gfx.glmark2.sw` | `pass` | glmark2 scene-clear substrate initializes EGL/GLES2 and presents frames through VirtIO-GPU 2D/software scanout |

Current stage: this app remains a software-substrate benchmark. The evaluation
matrix passes this row; QEMU/Venus graphics acceleration and full glmark2 scene
FPS are future gates, not claims made by this porting file.

## Porting boundary

This is a bounded Unikraft application port of the `glmark2-es2` `scene-clear` workload. The adapter preserves the workload shape that matters for VOGUE evidence: initialize EGL/GLES2, clear the framebuffer for a fixed frame count, present every frame, and emit a glmark2-style score marker.

The full upstream suite is intentionally not imported because it brings C++ runtime, libpng/image assets, multiple scene classes, and platform/window-system backends outside the current VOGUE compatibility surface. Those are future work, not hidden pass criteria.

## Unikraft build system

- `Config.uk` — declares `CONFIG_APP_GLMARK2` and selects required VOGUE libraries
- `Makefile.uk` — registers with `addlib`, uses `APPGLMARK2_*` variables, lists `main.c`, records upstream provenance
- `exportsyms.uk` — exports only `main`
- `Kraftfile.glmark2` — standalone unikernel image for this application

## Claim boundaries

**Allowed**: `gfx.glmark2.sw` — glmark2 scene-clear substrate through VirtIO-GPU 2D/software scanout; native frame cost measured.

**Forbidden**: full glmark2 suite score, asset scenes, QEMU performance, Mesa compatibility, GPU acceleration.

## Verification

```sh
make app-port-check
make app-perf-check
make verify
```

`app-perf-check` samples this deterministic software-render benchmark best-of-N
(default 5; override with `VOGUE_APP_PERF_REPS`) and keeps the lowest
`avg_frame_ms`, so the ±5–10% wall-clock variance on a non-isolated host does
not produce a false `gfx.glmark2.sw` `frame_ms` regression in `make perf-check`.
