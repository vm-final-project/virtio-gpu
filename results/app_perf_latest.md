# VOGUE Application Performance

Generated: `2026-06-01T14:24:53.388922Z`

> Scope: native software/substrate benchmark on fake VirtIO-GPU backend; not a virgl/GPU result.

| Row | App | Frames | Resolution | Avg frame (ms) | FPS | MiB/s copied | Transfers | Flushes | Fences |
|-----|-----|--------|------------|----------------|-----|--------------|-----------|---------|--------|
| `gfx.kmscube.sw` | kmscube | 60 | 640x480 | 2.189 | 456.89 | 535.42 | 60 | 60 | 120 |
| `gfx.glmark2.sw` | glmark2 scene clear | 120 | 1280x800 | 5.732 | 174.46 | 681.47 | 120 | 120 | 240 |

## Claim boundaries

These rows measure local native performance of supported software/substrate paths. They do not establish QEMU, Linux/Mesa, or virgl/GPU performance.

Timing rows use best-of-N selection only as a noisy-host smoke gate; the JSON artifact persists medians and every sample for reviewer/performance analysis.
