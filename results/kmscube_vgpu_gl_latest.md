# kmscube VirtIO-GPU-GL K1 Evidence

- status: `pass`
- evidence: `results/kmscube_vgpu_gl/latest/build.log;results/kmscube_vgpu_gl/latest/run.log;results/kmscube_vgpu_gl/latest/frame-proof.json;results/kmscube_vgpu_gl/latest/mesa-feasibility.json`
- allowed: Unikraft-native kmscube EGL/GLES scanout proof over a non-software VirtIO-GPU-GL/virgl path.
- forbidden: GPU tensor acceleration, LLM offload, generalized Mesa compatibility, glmark2 success, or performance speedup.
- next step: Plan glmark2-es2-drm quantitative benchmark only after preserving K1 evidence.
