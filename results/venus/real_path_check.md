# Real VirtIO-GPU path check

Status: `pass`
QEMU status: `pass`

| Check | Status | Evidence | Required property |
|---|---|---|---|
| `kraft_real_backend:Kraftfile` | `pass` | Kraftfile | Production Kraftfile selects libukvirtio_gpu and the REAL backend |
| `config_real_backend:.config.vogue_qemu-x86_64` | `pass` | .config.vogue_qemu-x86_64 | Generated Unikraft config selects REAL backend and not FAKE |
| `latest_build_real_config` | `pass` | .unikraft/build/config; .unikraft/build/include/uk/bits/config.h | Latest graphics/Vulkan build artifacts select the real backend |
| `build_compiles_real_object` | `pass` | results/kmscube_vgpu_gl/run/build.log; .unikraft/build/libukvirtio_gpu/virtio_gpu_real.o | Build logs or build artifacts include virtio_gpu_real.o (or kraft build blocked) |
| `compile_database_real_source` | `pass` | .unikraft/build/compile_commands.json; .unikraft/build/libukvirtio_gpu/virtio_gpu_real.o.cmd | Latest compile database records the real backend source |
| `real_source_controlq_tokens` | `pass` | missing=[] | Real backend contains Unikraft virtio-bus registration and 3D/blob controlq commands |
| `real_protocol_tokens` | `pass` | missing=[] | Protocol header contains virgl/blob/Venus-relevant feature and command tokens |
| `qemu_real_probe_artifact` | `pass` | status=pass; results/venus/qemu_2d_probe.json | QEMU probe records a real-device pass or a structured real-path blocker |
| `modern_pci_blocker_truthful` | `pass` | results/venus/qemu_2d_probe.json | If modern PCI blocks the run, artifact names QEMU VirtIO-GPU PCI ID 0x1050 |

## Claim boundary

Production builds use the real libukvirtio_gpu backend. Accelerated Vulkan/Venus runtime may only be claimed when QEMU/Vulkan rows pass; blocked modern PCI remains a non-claim.
