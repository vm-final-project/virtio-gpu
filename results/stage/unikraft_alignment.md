# Unikraft alignment gate

Status: `pass`

| Check | Status | Evidence |
|---|---|---|
| `modular_libukvirtio_gpu` | `pass` | VirtIO-GPU is a selectable micro-library; fake backend lives in tests/ only. (`libs/libukvirtio_gpu/Config.uk`) |
| `reuse_unikraft_virtio` | `pass` | Real backend uses Unikraft virtio bus/virtqueue instead of project-local PCI/queue reimplementation. (`libs/libukvirtio_gpu/virtio_gpu_real.c`) |
| `reuse_unikraft_alloc` | `pass` | Real backend uses Unikraft allocator APIs. (`libs/libukvirtio_gpu/virtio_gpu_real.c`) |
| `fail_closed_host_visible` | `pass` | Host-visible blob mapping is truthfully blocked when the Unikraft transport lacks SHM BAR exposure. (`libs/libukvirtio_gpu/virtio_gpu_real.c`) |
| `stk_out_of_scope` | `pass` | STK porting is explicitly documented as out of scope in the spec (plan.md §0.5). (`design/unikraft-virtio-gpu-spec-v1.md`) |
| `source_lineage` | `pass` | Spec records source lineage and non-reimplementation boundaries. (`design/unikraft-virtio-gpu-spec-v1.md`) |
| `readme_truthful_stage` | `pass` | README reports the current repo-facing stage and keeps claims tied to same-run evidence. (`README.md`) |
| `forbidden_project_pci_driver` | `pass` | Project must not fork Unikraft PCI discovery. (`libs/libukvirtio_gpu`) |
