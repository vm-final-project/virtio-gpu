# VOGUE stage audit

Status: `pass`
Stage: `pass`

## Required artifacts

- `design/unikraft-virtio-gpu-spec-v1.md`: present
- `design/virtio-gpu-vulken-v1.md`: present
- `README.md`: present
- `docs/ARCHITECTURE.md`: present

## Key statuses

- QEMU Venus probe: `pass`
- Venus perf gate: `pass`
- Acceleration status: `ready-for-venus-smoke`

## Claim boundary
Native/static/design gates pass. Real QEMU Venus acceleration remains blocked unless qemu_probe_status is pass.
