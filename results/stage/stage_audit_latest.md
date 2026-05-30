# VOGUE stage audit

Status: `pass`
Stage: `blocked:probe-incomplete`

## Required artifacts

- `design/unikraft-virtio-gpu-spec-v1.md`: present
- `design/virtio-gpu-vulken-v1.md`: present
- `README.md`: present
- `paper/sections/08-evaluation.typ`: present

## Key statuses

- QEMU Venus probe: `blocked:probe-incomplete`
- Venus perf gate: `pass`
- Acceleration status: `blocked:host-visible-or-qemu-gate`

## Claim boundary
Native/static/design/paper gates pass. Real QEMU Venus acceleration remains blocked unless qemu_probe_status is pass.

