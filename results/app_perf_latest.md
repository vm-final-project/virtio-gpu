# VOGUE Application Performance

Generated: `2026-05-30T08:12:16.595290Z`

> Scope: native software/substrate benchmark on fake VirtIO-GPU backend; not a virgl/GPU result.

| Row | App | Frames | Resolution | Avg frame (ms) | FPS | MiB/s copied | Transfers | Flushes | Fences |
|-----|-----|--------|------------|----------------|-----|--------------|-----------|---------|--------|
| `gfx.kmscube.sw` | kmscube | 60 | 640x480 | 2.037 | 490.81 | 575.17 | 60 | 60 | 120 |
| `gfx.glmark2.sw` | glmark2 scene clear | 120 | 1280x800 | 5.304 | 188.55 | 736.52 | 120 | 120 | 240 |

## Claim boundaries

These rows measure local native performance of supported software/substrate paths. They do not establish QEMU, Linux/Mesa, or virgl/GPU performance.

Timing rows use best-of-N selection only as a noisy-host smoke gate; the JSON artifact persists medians and every sample for reviewer/performance analysis.
