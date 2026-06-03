# Venus QEMU 2d probe

```json
{
  "status": "pass",
  "mode": "2d",
  "returncode": 0,
  "elapsed_s": 0.5740213394165039,
  "qemu_command": [
    "/mydata/JerryT/qemu-src/build/qemu-system-x86_64",
    "-machine",
    "accel=tcg",
    "-cpu",
    "max",
    "-m",
    "512M",
    "-display",
    "egl-headless,gl=on",
    "-serial",
    "mon:stdio",
    "-device",
    "virtio-gpu-gl-pci,hostmem=512M,blob=true,venus=true",
    "-kernel",
    "/mydata/JerryT/virtio-gpu/.unikraft/build/vogue_qemu-x86_64"
  ],
  "first_missing_dependency": null,
  "next_step": null,
  "log_tail": "Powered by\no.   .o       _ _               __ _\nOo   Oo  ___ (_) | __ __  __ _ ' _) :_\noO   oO ' _ `| | |/ /  _)' _` | |_|  _)\noOo oOO| | | | |   (| | | (_) |  _) :_\n OoOoO ._, ._:_:_,\\_._,  .__,_:_, \\___)\n           Ijiraq 0.21.0~7351f8b-custom\nuk-kmscube: booted kmscube_vgpu_gl proof harness\nuk-kmscube: path=swrender+virtio-gpu-2d gles=disabled mesa=disabled\nuk-kmscube: drm_compat mode=1280x800 refresh=60 scope=bounded-kmscube-only-no-generic-dev-dri-ioctl\nuk-kmscube: virtio_gpu capsets=3 virgl=1 blob=1 host_visible=1\nuk-kmscube: capset index=0 id=1 name=virgl\nuk-kmscube: capset index=1 id=2 name=virgl2\nuk-kmscube: capset index=2 id=4 name=venus\nuk-kmscube: virgl_path ctx_id=1 w=1280 h=800 frames=3\nuk-kmscube: virgl_frame=0 submit=1 colour=0.20,0.40,0.80\nuk-kmscube: virgl_frame=1 submit=2 colour=0.80,0.20,0.40\nuk-kmscube: virgl_frame=2 submit=3 colour=0.40,0.80,0.20\nuk-kmscube: PASS kmscube_vgpu_gl frames=3 renderer=virgl submits_3d=3 evidence_id=virgl-clear-proof real_virtio_gpu=1 w=1280 h=800\n",
  "claim_allowed": "Real QEMU probe claim only if status is pass.",
  "written_at": "2026-06-03T08:01:47Z"
}
```
