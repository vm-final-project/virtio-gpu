# VOGUE Application Performance

Generated: `2026-06-04T15:35:48.400231Z`

> Scope: native software/substrate benchmark on fake VirtIO-GPU backend; not a virgl/GPU result.

| Row | App | Frames | Resolution | Avg frame (ms) | FPS | MiB/s copied | Transfers | Flushes | Fences |
|-----|-----|--------|------------|----------------|-----|--------------|-----------|---------|--------|
| `gfx.kmscube.submit` | kmscube | 60 | 640x480 | 1.782 | 561.23 | 657.69 | 60 | 60 | 120 |

## Claim boundaries

These rows measure local native performance of supported software/substrate paths. They do not establish QEMU, Linux/Mesa, or virgl/GPU performance.

Timing rows use best-of-N selection only as a noisy-host smoke gate; the JSON artifact persists medians and every sample for reviewer/performance analysis.
