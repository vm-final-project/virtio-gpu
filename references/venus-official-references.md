# Venus / VirGL / QEMU Official Reference Notes

Checked: 2026-06-22

This note records the external references used by
`docs/superpowers/plans/2026-06-21-smp-a3-venus-gpu-hardening.md`.
It is intentionally short: use the upstream pages as the source of truth.

| Topic | Official reference | Checked lines / text to inspect |
| --- | --- | --- |
| Venus ownership | https://docs.mesa3d.org/drivers/venus.html | Lines 7-9 describe Venus as a VirtIO-GPU protocol for Vulkan command serialization, with protocol/codegen in `venus-protocol` and renderer in `virglrenderer`. |
| Required virtio-gpu params | https://docs.mesa3d.org/drivers/venus.html | Lines 51-62 list `VIRTGPU_PARAM_3D_FEATURES`, `CAPSET_QUERY_FIX`, `RESOURCE_BLOB`, `HOST_VISIBLE`, and `CONTEXT_INIT` as required from the virtio-gpu kernel driver unless vtest is used. |
| QEMU Venus example | https://docs.mesa3d.org/drivers/venus.html | Lines 82-99 show `-m 4G` and `-device virtio-gpu-gl,hostmem=4G,blob=true,venus=true`. |
| Host-visible caveat | https://docs.mesa3d.org/drivers/venus.html | Lines 143-148 explain that host-visible Venus memory relies on host-driver behavior and is not guaranteed for all Vulkan drivers. |
| VirGL host rendering boundary | https://docs.mesa3d.org/drivers/virgl.html | Lines 9-15 state that VirGL lets the guest use host GPU capabilities and that implementation of rendering is done on the host side. |
| QEMU rendernode | https://qemu-project.gitlab.io/qemu/system/invocation.html | Lines 1364-1366 define `egl-headless[,rendernode=<file>]` as offloading OpenGL operations to a local DRI device. This is host-side renderer selection. |
| Mesa source organization | https://docs.mesa3d.org/sourcetree.html | Use as the high-level directory-organization reference. The concrete code references in the plan come from the local `../mesa` checkout. |

Implications for VOGUE:

| Question | Plan conclusion |
| --- | --- |
| Can the guest kernel choose llvmpipe/lavapipe/hardware GPU? | No. The guest can validate VirtIO-GPU/Venus features and create a Venus context. Host QEMU/virglrenderer/Mesa choose the actual renderer based on host configuration such as `egl-headless` and `rendernode`. |
| Can llvmpipe pass while a hardware GPU backend fails? | Yes. Mesa documents host-visible memory assumptions as host-driver dependent. A software-renderer pass is useful protocol evidence, but not hardware GPU proof. |
| Does missing `rendernode` force llvmpipe? | Not by guest choice. Omitting `rendernode` leaves host selection to QEMU/virglrenderer/Mesa. The plan requires result JSON to record whether rendernode was explicit or automatic. |
| Should memory/hostmem be audited? | Yes. Mesa's QEMU example uses 4G guest memory and 4G GPU hostmem. The current VOGUE runner still hardcodes smaller values, so Task 8 makes them explicit inputs and records them in JSON. |
