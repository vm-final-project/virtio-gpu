# Venus/Vulkan Runtime Enablement: Env, Problem, Root Cause, Fix & Test Plan

2026-06-01 update: the runtime-enablement plan below is now historical. The
current evaluation-host matrix is 27/27 PASS, including `xport.qemu-vgpu`,
`gfx.kmscube.submit`, `gfx.kmscube.frame`, `llm.bench.vk`,
`llm.bench.vk.real`, and `llm.server.vk`. Use `docs/VENUS-BRINGUP.md`,
`plan-fix.md`, and `plan-optimize.md` for the current status, residual
generated-summary cleanup, and performance work.

Status date: 2026-05-30. This document is the doc-grounded plan for moving the
blocked Venus/Vulkan-runtime evaluation rows from `blocked:*` to `pass`. Every
claim below is grounded in official documentation (cited inline) and in
on-host evidence captured this session.

## 1. Build / verify the environment

The host is **fully Venus-capable**; all prerequisites verified present:

| Component | Requirement (official source) | On-host state | Evidence |
|---|---|---|---|
| QEMU with `virtio-gpu-gl-pci.venus` | Venus needs QEMU ≥ 9.1 with `virtio-gpu-gl,hostmem=…,blob=true,venus=true` ([QEMU virtio-gpu docs](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html)) | system `/usr/bin/qemu-system-x86_64` = **8.2.2 (NO venus)**; local build `/mydata/JerryT/qemu-src/build/qemu-system-x86_64` = **11.0.1 (venus=on supported)** | `-device virtio-gpu-gl-pci,help` lists `venus=<bool>` only on 11.0.1 |
| virglrenderer with Venus | Venus translation since virglrenderer ≥ 1.0, built `-Dvenus=true` ([Mesa Venus](https://docs.mesa3d.org/drivers/venus.html)) | **1.3.0** at `/usr/local/lib/x86_64-linux-gnu/libvirglrenderer.so.1`, Venus symbols present (`VK_MESA_venus_protocol`, `vkr_dispatch_vkCreateRingMESA`) | `pkg-config --modversion virglrenderer` = 1.3.0; `strings` shows venus dispatch |
| `/dev/udmabuf` (blob) | Blob resources need `CONFIG_UDMABUF` ([Mesa Venus](https://docs.mesa3d.org/drivers/venus.html)) | **present** | `ls /dev/udmabuf` |
| `memory-backend-memfd` | hostmem window backing for blob | **present** | `qemu … -object help` lists it |
| Host GPU + Mesa EGL/GBM | `egl-headless` render node | NVIDIA 580 + Mesa EGL, `/dev/dri/renderD128–131` | `ls /dev/dri`, `libEGL_nvidia/mesa` present |
| KVM | optional acceleration | `/dev/kvm` present | `ls /dev/kvm` |

**Conclusion:** no host-side dependency is missing. The build env work is to
(a) make the gates use the Venus-capable QEMU and (b) finish the guest driver.

## 2. The problem

These evaluation-matrix rows are `blocked:*` (see
`results/vogue_evaluation_matrix.md`):

- `xport.qemu-vgpu` — QEMU VirtIO-GPU Venus probe
- `gfx.kmscube.submit`, `gfx.kmscube.frame` — virgl SUBMIT_3D / pixel frame
- `llm.bench.vk`, `llm.server.vk`, `llm.bench.vk.real` — upstream llama.cpp Vulkan
- `host.vk.probe`, `host.bench.vk.run`, `host.bench.vk` — Unikraft Vulkan runtime

All of these depend transitively on a real VirtIO-GPU device reaching the guest
driver. None can promote to `pass` until `xport.qemu-vgpu` does.

## 3. Root cause (two layers, both proven this session)

**Layer 1 — wrong QEMU binary (FIXED).** The probe/Makefile resolved
`QEMU=qemu-system-x86_64` via `PATH` to system **8.2.2**, whose
`virtio-gpu-gl-pci` has no `venus` property:

```
qemu-system-x86_64: -device virtio-gpu-gl-pci,…,venus=true: Property 'virtio-gpu-gl-pci.venus' not found
```

This produced the misleading `blocked:probe-incomplete` artifact. Fixed by
`scripts/venus_qemu_probe.py:select_qemu()`, which now prefers a binary whose
`virtio-gpu-gl-pci,help` advertises `venus` (local 11.0.1). Re-running the probe
now boots the guest and surfaces the true blocker below.

**Layer 2 — guest lacks modern VirtIO-1.0 PCI transport (OPEN, the real work).**
With QEMU 11.0.1, the guest boots but rejects the device:

```
ERR: [libvirtio_pci] Invalid Virtio Devices 1050
ERR: [libvirtio_pci] Failed to probe (legacy) pci device: -22
uk-kmscube: BLOCKED kmscube_vgpu_gl status=blocked:no-real-virtio-gpu
```

`unikraft/drivers/virtio/pci/virtio_pci.c` only accepts **legacy** device IDs
`0x1000–0x103f` (`virtio_pci_legacy_add_dev`, with the comment *"the possibility
of supporting the modern PCI device in the future"*). Per the
[virtio 1.x spec](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)
(`transport-pci.tex:24`): *"The PCI Device ID is calculated by adding 0x1040 to
the Virtio Device ID."* virtio-gpu is device 16 → **0x1050**, a **modern,
non-transitional** device. QEMU's `virtio-gpu-gl-pci` is modern-only (GL/Venus
requires virtio-1.0), so the legacy probe rejects it.

The tree contains a **half-finished** modern port (struct fields + a
`virtio_pci_modern_add_dev` *declaration*, ~69 lines) but the dispatcher
`virtio_pci_add_dev` (line 483) still calls only `virtio_pci_legacy_add_dev`,
and the modern body is absent. The companion patch
`patches/unikraft/0001-virtio-pci-modern-device-support.patch` (536 lines) is
**stale** — it no longer applies to the current Unikraft HEAD (v0.21.0,
`7351f8b`); `git apply --check` fails at `virtio_pci.c:133`.

## 4. Fix plan (grounded in the virtio-1.0 PCI transport spec)

Implement modern VirtIO-PCI in `unikraft/drivers/virtio/pci/`, following
virtio-1.2 §4.1 (PCI transport). Steps:

1. **Reconcile the partial work.** Either rebase
   `0001-virtio-pci-modern-device-support.patch` onto HEAD (regenerate hunks
   that fail at line 133) or fold its content into the tree's existing partial
   edits — one coherent implementation, not both.
2. **Capability scan.** Walk the PCI capability list for
   `VIRTIO_PCI_CAP_VENDOR` entries and map the four structures (spec §4.1.4):
   `COMMON_CFG` (cfg_type 1), `NOTIFY_CFG` (2, with `notify_off_multiplier`),
   `ISR_CFG` (3), `DEVICE_CFG` (4). Resolve each `bar`+`offset`+`length` to an
   MMIO pointer.
3. **`virtio_config_ops` for modern.** Implement against `common_cfg`:
   `device_reset` (write 0 to `device_status`, poll until 0), `status_get/set`,
   64-bit `features_get/set` via `device_feature_select`/`driver_feature_select`
   windows, `config_get/set` against `DEVICE_CFG`, and `vqs_find`/`vq_setup`/
   `vq_release` programming `queue_select`/`queue_size`/`queue_desc`/
   `queue_driver`/`queue_device`/`queue_enable`, with notifies via the
   `NOTIFY_CFG` BAR using `queue_notify_off * notify_off_multiplier`. Negotiate
   `VIRTIO_F_VERSION_1` and complete the spec §3.1 status handshake
   (ACK→DRIVER→FEATURES_OK→DRIVER_OK).
4. **Dispatcher.** In `virtio_pci_add_dev`, branch on device ID:
   `0x1040–0x107f` → `virtio_pci_modern_add_dev` (virtio_device_id =
   device_id − 0x1040); `0x1000–0x103f` → existing legacy path.
5. **Kconfig.** Gate behind `CONFIG_LIBUKVIRTIO_PCI_MODERN` (default y when
   virtio-gpu is selected); ensure the VOGUE `.config.vogue_qemu-x86_64`
   selects it.
6. **Rebuild** the appliance: `make kmscube-build` (uses `CONFIG_UK_BASE=
   /mydata/JerryT/unikraft`), then boot via the probe.

## 5. Plan to pass all test/perf evaluations

Gate every promotion on the existing Make targets; never hand-edit the matrix.

| Gate | Command | Pass criterion (doc-grounded) |
|---|---|---|
| Host native regression (must stay green) | `make native-tests` | 164/164 + 12 binaries pass (currently PASS) |
| `xport.qemu-vgpu` | `make venus-check` | probe boots under QEMU 11.0.1, guest binds `0x1050`, serial shows `real_virtio_gpu=1`; status flips `modern-pci-unsupported`→`pass` |
| `gfx.kmscube.submit` | `make kmscube-run && make kmscube-check` | same-run serial `PASS` marker for SUBMIT_3D delivery |
| `gfx.kmscube.frame` | `make kmscube-check` | non-blank screendump whose per-frame mean colour matches the encoded CLEAR colour (pixel proof) |
| `llm.bench.vk` / `.real` | `make llama-vulkan-check` | same-run PASS line + pp512/tg128 vs `config/perf_baseline.json` |
| `host.vk.probe` etc. | `make eval-check` | Unikraft-domain JSON with required `evidence_id`/schema (not host-baseline) |
| Perf regression | `make perf-check` | best-of-N (`VOGUE_APP_PERF_REPS`) within baseline thresholds |
| Full gate | `make verify` | all of the above |

**Order of operations:** finish §4 → `make native-tests` (regression guard) →
`make venus-check` (expect `xport.qemu-vgpu` pass) → `make kmscube-run` +
`make eval-check` → Vulkan/llama rows → `make perf-check` → `make verify`.

## 6b. Modern VirtIO-PCI transport — IMPLEMENTED & VERIFIED (2026-05-30)

Layer 2 is now **done**. `unikraft/drivers/virtio/pci/virtio_pci.c` gained a
complete modern (virtio-1.0) PCI transport per spec §4.1:

- PCI capability-list scan mapping `COMMON_CFG`/`NOTIFY_CFG`/`ISR_CFG`/
  `DEVICE_CFG` BAR windows (+ `SHARED_MEMORY_CFG` for blob via a real
  `virtio_pci_shm_region_get` overriding the driver's weak stub).
- MMIO `virtio_config_ops`: reset/status, 64-bit feature negotiation with
  `VIRTIO_F_VERSION_1`, device-config access, and split-virtqueue setup
  (desc/driver/device addresses, per-queue notify offset, queue_enable).
- `virtio_pci_add_dev` now routes device ids `0x1040–0x107f` to the modern
  path and `0x1000–0x103f` to the legacy path.

**Verified on QEMU 11.0.1 + virglrenderer 1.3.0 (Venus):** the guest binds the
modern `virtio-gpu-gl-pci` (device 0x1050), detects capsets `virgl`/`virgl2`/
`venus(id=4)`, runs the virgl SUBMIT_3D path, and prints
`PASS kmscube_vgpu_gl ... real_virtio_gpu=1`. The old `Invalid Virtio Devices
1050` rejection is gone.

**Evaluation matrix:** 13 blocked → **7 blocked / 20 pass**.
Flipped to `pass` by the transport (real backend):
`xport.qemu-vgpu`, `gfx.kmscube.submit`. Regression guard `make native-tests`
stays 164/0; `make venus-check` passes (`real_virtio_gpu_path_check` 10/10).

Also added `uk_virtio_gpu_resource_flush` to the kmscube virgl present path
(spec §5.7.6.10) so the rendered scanout is flushed to the host display.

### Host-visible Venus memory unblocked (2026-05-30, follow-up)

A second real blocker was found and fixed. The driver's
`virtio_pci_shm_region_get` was defined **weak in the same TU that calls it**;
Unikraft's two-stage partial linking (`ld -r`) pre-binds the call to that local
weak stub, so the transport's strong override never linked (confirmed via
`nm`: both a local `t` and global `T`). Replacing the weak definition with an
`extern` declaration (the transport always provides the symbol) fixes it.

Result, verified on real QEMU 11.0.1 + virglrenderer Venus:
- The transport's cap scan finds QEMU's host-visible window
  (`SHARED_MEMORY_CFG` cap, `cfg_type=8 bar=4 id=1`).
- `uk-kmscube: ... host_visible=1` (was 0).
- The **real Venus ring** now registers and round-trips over host-visible blob
  memory: `uk-venus: ring registered resource=4 size=65536`, `ring flush ok`,
  `venus_ring_protocol=pass` — over the real backend, not just native tests.

This clears `libukvulkan_venus`'s `blocked:host-visible-missing` wall, the prerequisite
for every Venus Vulkan-runtime row. The remaining gap to `host.vk.probe` is the
`vkCreateInstance`→`vkEnumeratePhysicalDevices`→`vkGetPhysicalDeviceProperties`
reply round-trip (encoders exist in `libukvulkan_venus`; needs reply-buffer wiring and
virglrenderer Venus acceptance), then ggml-vulkan compute dispatch for the
token rows.

### Exact reply-protocol gap pinpointed (2026-05-30, follow-up 3)

Traced from the **host** source (`virglrenderer/src/venus/vkr_ring.c`): the ring
has regions head/tail/status/buffer **and `extra`**; the host writes reply
values into the `extra` region via `vkr_ring_write_extra()` ("Mesa always sets
offset to 0"). The exact guest-side gap is one line:
`libs/libukvulkan_venus/venus_cs.c:391` encodes `vkCreateRingMESA` with **`extraSize =
0`** — i.e. no reply region — so the host has nowhere to write sync-command
replies (device count, `deviceName`, `apiVersion`).

Concrete implementation blueprint for `host.vk.probe` (now that command
consumption is proven to work):
1. Grow the ring blob and set `extraOffset`/`extraSize > 0` in
   `uk_venus_encode_vkCreateRingMESA` + `uk_venus_ring_register`.
2. Encode `vkCreateInstance` / `vkEnumeratePhysicalDevices` /
   `vkGetPhysicalDeviceProperties` with `VK_COMMAND_GENERATE_REPLY_BIT_EXT` and
   a reply destination in the extra region.
3. After the host advances `head`, decode the reply (count, handle,
   `VkPhysicalDeviceProperties.deviceName`/`apiVersion`).
4. Log `vk: physical_device=<name> api=<version>`; a probe script records
   `results/llama/vulkan_probe.json` and the eval flips `host.vk.probe`.

This is bounded, source-grounded work — but multi-hour with protocol-decode
uncertainty, and only the first of the six Vulkan rows (the rest add
ggml-vulkan compute dispatch + token output).

### Venus command consumption confirmed working (2026-05-30, follow-up 2)

Experimental result (reproducible): a hand-encoded real `vkCreateInstance`
(156 bytes) submitted into the registered ring is **consumed by virglrenderer's
Venus renderer** — the shared `head` advances to `tail` in ~130 ms with no CS
error:

```
vkprobe head=156 tail=156 elapsed_ms=128 CONSUMED
vkprobe head=156 tail=156 elapsed_ms=154 CONSUMED
```

(An earlier "head=0" reading was a fixed-iteration wait timing out before the
~130 ms async host processing; a wall-clock poll shows reliable consumption.)

This proves the **command-submission half** of the Venus runtime works
end-to-end over the modern transport: ring create → register → write command →
notify → host consume. The remaining gap for `host.vk.probe` is the **reply
path**: returning `vkEnumeratePhysicalDevices` count/handles and
`vkGetPhysicalDeviceProperties` (`deviceName`, `apiVersion`). Mesa's reply
protocol (`vn_ring_submit_command`/`vn_ring_get_command_reply`, reply-shmem with
`VK_COMMAND_GENERATE_REPLY_BIT_EXT`) lives in Mesa's `vn_ring.c`, which is not
in the `venus-protocol` checkout (only stubs returning NULL), so it must be
reverse-engineered/implemented. That, then ggml-vulkan compute dispatch, is the
remaining multi-session work for the Vulkan rows.

**Remaining 7 blocked rows are NOT transport blockers** — they need the full
Venus *Vulkan-compute runtime* or a QEMU display capability, each a separate
effort, and must not be promoted without same-run PASS artifacts:

| Row | Real blocker (not the transport) |
|---|---|
| `gfx.kmscube.frame` | QEMU `egl-headless` GL-scanout `screendump` returns `no surface`; colour-band pixel proof needs host GL framebuffer readback. |
| `host.vk.probe` | Native `libvulkan`/`libukvulkan_venus` must round-trip `vkEnumeratePhysicalDevices` through Venus and read back the reply (`vk: physical_device=…`). |
| `host.bench.vk.run`, `host.bench.vk`, `llm.bench.vk`, `llm.bench.vk.real`, `llm.server.vk` | ggml-vulkan compute dispatch executing through Venus → host GPU with real token output; the static dispatch passes natively but the live ring-reply/compute path is unproven. |

## 6. Done this session (planning pass)

- Verified the full Venus-capable host env (table §1).
- Proved the two-layer root cause from real QEMU boots.
- Fixed Layer 1: `scripts/venus_qemu_probe.py` now auto-selects a Venus-capable
  QEMU; the probe artifact is now the honest `blocked:modern-pci-unsupported`
  with an actionable `next_step`, instead of the misleading `probe-incomplete`.
- Scoped Layer 2 (modern VirtIO-PCI) against the virtio-1.2 spec with concrete
  steps and exact gates.

## Sources

- QEMU VirtIO-GPU: https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html
- Mesa Venus: https://docs.mesa3d.org/drivers/venus.html
- virtio 1.2 spec (PCI transport §4.1): https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
- KraftKit build: https://unikraft.org/docs/cli/reference/kraft/build
- Vulkan loader: https://docs.vulkan.org/guide/latest/loader.html
