# VOGUE benchmark summary

Status: `pass`

| Row | Benchmark | Status | Metric | Value | Scope |
|---|---|---|---|---|---|
| `gfx.kmscube.sw` | kmscube | `pass` | `avg_frame_ms` | `1.969` | native software/substrate fake-backend |
| `gfx.kmscube.sw` | kmscube | `pass` | `fps` | `507.91` | native software/substrate fake-backend |
| `gfx.glmark2.sw` | glmark2 scene clear | `pass` | `avg_frame_ms` | `5.259` | native software/substrate fake-backend |
| `gfx.glmark2.sw` | glmark2 scene clear | `pass` | `fps` | `190.16` | native software/substrate fake-backend |
| `VSTAT` | real driver static gate | `pass` | `latency_ms_mean` | `37.0669834` | static gate overhead, not GPU performance |
| `VSTAT` | real driver static gate | `pass` | `latency_ms_median` | `36.737527` | static gate overhead, not GPU performance |
| `VSTAT` | real driver static gate | `pass` | `latency_ms_p95` | `38.202398` | static gate overhead, not GPU performance |
| `xport.qemu-vgpu` | QEMU Venus probe | `blocked:probe-incomplete` | `probe_status` | `blocked:probe-incomplete` | real QEMU probe; pass required for acceleration claims |
