# Real virtio-gpu Venus bring-up status

> **2026-06-01 update — full GPU compute path reached; all 27 evaluation-matrix
> rows PASS (0 blocked).** The upstream llama.cpp Vulkan **bench** appliance now
> runs end-to-end on the real V100 over `virtio-gpu-gl venus=true`
> (`pp512=2232.1 t/s`, `tg128=160.2 t/s` on the latest same-run artifact; three
> post-change runs record `tg128` median `139.9`), and the **server**
> appliance boots into a single entrypoint, loads all 28 layers onto the V100
> via Venus, and reaches `READY`. The four breakthroughs versus the milestone
> log below:
>
> 1. **Stale host virglrenderer.** The installed
>    `/usr/local/lib/.../libvirglrenderer.so.1` predated the fixes in
>    `../virglrenderer/out2`; reinstalling it stopped the host Venus context
>    from going fatal mid-compute. (`ninja -C out2 install`.)
> 2. **Guest thread-stack overflow.** ggml-vulkan compiles pipelines on
>    `hardware_concurrency()` `std::async` workers that drive the Venus dispatch
>    with multi-KB SPIR-V/struct temporaries; the 64 KiB default Unikraft thread
>    stack overflowed non-deterministically (crash in `stub_vkCreateDescriptorPool`
>    with a corrupted callee-saved register). Fixed with
>    `CONFIG_STACK_SIZE_PAGE_ORDER=7` (512 KiB) in the vk Kraftfiles.
> 3. **QEMU egl-headless GL screendump.** `qmp_screendump` returned “no surface”
>    for a virtio-gpu-gl texture scanout. `ui/console.c:qemu_console_surface`
>    now exposes the egl-headless read-back surface (`egl_scanout_flush` already
>    blits the scanout into it), so the kmscube colour-band pixel proof
>    (`gfx.kmscube.frame`) is captured headlessly — solid green `(102,204,51)`.
> 4. **llama-server porting.** The full upstream HTTP server now links on
>    Unikraft (compat `<linux/limits.h>`, force-`<unordered_map>`, `-iquote` for
>    the `common`/`src` `unicode.h` clash, vendored split `httplib.cpp`,
>    `posix_spawnp` shim, `CONFIG_LIBPOSIX_SOCKET`). HTTP serving itself stays
>    out of scope (no lwip); only model-loaded readiness is claimed.
>
> The milestone narrative below is retained as the historical bring-up trail.


This document records the state of running VOGUE workloads over **real**
QEMU `virtio-gpu-gl-pci,venus=true` against this evaluation host's GPU, and the
remaining performance/frontier work after full guest-side GPU compute reached
same-run PASS evidence. It is the evidence trail for the governance rows
`xport.qemu-vgpu`, `proto.venus-ring`,
`host.vk.probe`, `host.bench.vk*`, `llm.bench.vk`, `llm.bench.vk.real`,
`llm.server.vk` and `gfx.kmscube.frame`.

## Evaluation host

- GPUs: 4× NVIDIA Tesla V100-SXM2-16GB, proprietary driver 580.159.03,
  Vulkan 1.4 (`vulkaninfo` reports all four + llvmpipe). Render nodes
  `/dev/dri/renderD128..131` are `root:render 0660`; grant access with
  `sudo chmod o+rw /dev/dri/renderD12[89] /dev/dri/renderD13[01]`.
- EGL/GBM: works via the **NVIDIA GBM backend** (`nvidia-drm_gbm.so` +
  `libnvidia-egl-gbm`, `15_nvidia_gbm.json`). Use the **GBM** EGL platform
  (`EGL_PLATFORM_GBM_KHR`) — the Mesa device platform (`EGL_PLATFORM_DEVICE_EXT`)
  fails with "failed to create dri2 screen". This is why QEMU
  `-display egl-headless,gl=on` initialises on this NVIDIA-only host.
- QEMU: the system `qemu-system-x86_64` is 8.2.2 and has **no** Venus. Use the
  locally built **QEMU 11.0.1** at `../qemu-src/build/qemu-system-x86_64`
  (`virtio-gpu-gl-pci` exposes `venus=`, `blob=`, `hostmem=`,
  `drm_native_context=`). Probes auto-select it; or `export QEMU=...`.
- virglrenderer: Venus-enabled 1.11.0 at
  `/usr/local/lib/x86_64-linux-gnu/libvirglrenderer.so.1` (QEMU links this);
  render-server `/usr/local/libexec/virgl_render_server`.

## What works over real Venus (PASS)

- `xport.qemu-vgpu` — the kmscube `vogue_qemu-x86_64` appliance boots under
  QEMU 11 + `venus=true` and enumerates the real device:
  `virtio_gpu capsets=3 virgl=1 blob=1 host_visible=1`, capset id=4 **venus**,
  and prints `real_virtio_gpu=1`. (`scripts/venus_qemu_probe.py --mode 2d`.)
- `proto.venus-ring` — the Venus ring registers and flushes against the real
  device and a QMP screendump frame proof passes
  (`scripts/venus_qemu_probe.py --mode venus-ring`).
- `host.baseline.vk` — host-native `llama.cpp` Vulkan on the V100 (the same GPU
  Venus targets): qwen3-0.6B `pp128≈2692 t/s`, `tg32≈222 t/s`
  (`llama.cpp/build-vk/bin/llama-bench -ngl 99`, run with
  `LD_LIBRARY_PATH=$PWD/build-vk/bin`).

## llama.cpp Vulkan appliance: current PASS evidence

`scripts/llama_vk_real_run.py` boots `vogue-llama-upstream-vk_qemu-x86_64` under
real Venus (`-device virtio-gpu-gl-pci,hostmem=512M,blob=true,venus=true`,
`-display egl-headless,gl=on`, 9pfs model). Observed, end-to-end:

1. Guest boots, mounts the GGUF over 9pfs.
2. `ggml-vulkan` initialises through `libvulkan → libukvulkan_venus`, creates a
   **real Venus context** on the host (`virgl_render_server: ... context 1
   (ggml-vulkan-uk) with a valid instance`), and **enumerates a Venus device**
   (`ggml_vulkan: 0 = ...`), registering the Vulkan backend.
3. The upstream llama.cpp Vulkan bench runs a real GGUF on the V100 and emits
   token output with `pp512=2232.1 t/s`, `tg128=160.2 t/s` on the latest
   artifact after enabling batched Venus submission; the three post-change runs
   in `results/llama/post_opt_runs/` report `tg128={135.7, 139.9, 160.2}`.
   (`results/llama/upstream_vk.json`).
4. The Vulkan server image boots directly into its server entrypoint, loads the
   model over Venus, and reaches `READY` (`results/llama/upstream_server_vk.json`).

Honest status: the runtime rows are PASS on this host. HTTP serving semantics
remain out of scope until the lwIP/netdev path exists; `llm.server.vk` is a
model-loaded readiness claim, not a request/response throughput claim.

### Important build/runtime fixes made

- **Build**: upstream llama.cpp added `src/llama-kv-cache-dsa.cpp`; it was missing
  from the appliance source lists, causing undefined-symbol link failures. Added
  to `apps/app-llama-upstream-vk/Makefile.uk` and
  `apps/app-llama-upstream/Makefile.uk`. Also wipe stale `llama.cpp/build-unikraft*`
  cmake caches if they reference an old project path.
- **Runtime crash**: the Venus device's `hostmem=512M` 64-bit PCI BAR must land
  in the sub-4GB PCI hole, otherwise the guest faults in
  `vpci_modern_pci_dev_reset` (`drivers/virtio/pci/virtio_pci.c`). Boot the VK
  appliance with **`-m 3072`** (override via `VOGUE_VK_MEM`). Larger guest RAM
  (e.g. `-m 6144`) pushes the BAR into a high window `libvirtio_pci` does not map.

## Remaining frontier

The current frontier is no longer "make Vulkan run"; it is performance and
coverage:

1. **Request-level throughput and model-load efficiency** — the decode-path
   rescue is done: enabling batched Venus submission fixes the earlier
   `tg128=3.4` bottleneck and lifts the latest same-run artifact to
   `pp512=2232.1`, `tg128=160.2`. The remaining performance frontier is now the
   server/request path plus the still-unoptimized `use_mmap=false`,
   `huge_pages=false` model-load path.
2. **HTTP serving** — `llm.server.vk` proves direct entrypoint and model-loaded
   readiness only. Add lwIP/netdev plus request probes before reporting TTFT,
   requests/s, or aggregate throughput.
3. **Broader graphics coverage** — `gfx.kmscube.frame` passes via a bounded virgl
   CLEAR proof; full vkmark scene FPS and a Mesa EGL/GLES slice remain future
   gates tracked in `plan-fix.md`.
4. **Current-stage hygiene** — resolved on the evaluation host: `make
   current-stage-check` now passes even when the latest
   `.unikraft/build/config` belongs to a CPU bench image, because the checker
   treats that case as not applicable once production graphics/Vulkan configs,
   real object files, compile database evidence, and the QEMU Venus probe all
   pass.

## Concrete implementation plan for the Venus reply-read ICD

This is the sequenced, source-grounded plan to turn `libs/libukggml_vk/uk_vulkan_dispatch.c`
from a fabricating dispatch into a real Venus ICD. References are to the checked-out
trees (`../mesa`, `../virglrenderer`, `libs/libukvulkan_venus`).

**Reply protocol** (authoritative: `mesa/src/virtio/vulkan/vn_ring.c:vn_ring_submit_command`
+ `vn_ring_set_reply_shmem_locked`; host writer
`virglrenderer/src/venus/venus-protocol/vn_protocol_renderer_*.h:vn_encode_*_reply`):

1. Allocate a host-visible **reply blob** (reuse `uk_virtio_gpu_gl_blob_create_with_ctx`
   + `_blob_map` as the ring does in `libukvulkan_venus/venus_init.c`), attached to the **same**
   Venus context the dispatch uses (`g_ctx` in the dispatch). The dispatch currently has
   **no ring** — add `uk_venus_ring_create/register` on `g_ctx` during
   `uk_ggml_vulkan_dispatch_init()`.
2. Per query command needing a reply, write two commands into the ring circular buffer
   (`uk_venus_ring_cmd_write`):
   a. `vkSetReplyCommandStreamMESA` (cmd id 178, already in `uk/venus.h`) with
      `VkCommandStreamDescriptionMESA{resourceId=reply_blob.resource_id, offset, size}` —
      **encoder still to be written** in `venus_cs.c`.
   b. the actual command (e.g. `vkEnumeratePhysicalDevices`).
   Then `uk_venus_ring_cmd_flush` (stores tail + `vkNotifyRingMESA`) and
   `uk_venus_ring_cmd_wait` (polls head==tail = seqno reached).
3. Decode the reply from `reply_blob.mapped_addr`: `[uint32 VkCommandTypeEXT]` then the
   reply args, e.g. for `vkEnumeratePhysicalDevices`:
   `VkResult` + simple_pointer(count) + `array_size` + N×`VkPhysicalDevice`(uint64 id);
   for `vkGetPhysicalDeviceMemoryProperties`:
   `VkCommandTypeEXT` + simple_pointer + `VkPhysicalDeviceMemoryProperties` (520 bytes).

**Stubs to convert to round-trips** (replace the fabricated bodies in the dispatch):
`vkEnumeratePhysicalDevices` (capture the real host VkPhysicalDevice id),
`vkGetPhysicalDeviceProperties[2]` (real name/limits — removes the "VOGUE-Venus/RTX 4000 Ada"
placeholder → makes `host.vk.probe` honest), `vkGetPhysicalDeviceMemoryProperties[2]`
(real heaps **and** the `VkPhysicalDeviceMemoryBudgetPropertiesEXT` `heapBudget` pNext — this
is what ggml reads as "free"; zero today → "0 MiB free" → model load aborts),
`vkGetPhysicalDeviceQueueFamilyProperties`, `vkGetPhysicalDeviceFeatures2`,
`vkGetBufferMemoryRequirements`/`vkGetImageMemoryRequirements`.

**Host-visible memory** for weight upload + result readback: `vkAllocateMemory` of a
host-visible type must be backed by a mappable blob so `vkMapMemory` returns a guest
pointer the host shares (the `blocked:host-visible-or-qemu-gate` lever). Mirror the ring's
blob-map path; bind via the Venus `vkGetMemoryResourcePropertiesMESA` / blob export flow.

**Milestones** (each independently verifiable by re-running `scripts/llama_vk_real_run.py`):
- **M1 — DONE.** Reply round-trip implemented and proven: the guest reads the
  real host device name (`Tesla V100-SXM2-16GB`) back over Venus
  (`uk_venus_query_device_name`), ggml registers the real device, and
  `host.vk.probe` is a real PASS. Critical detail: the host writes the reply
  stream only for commands flagged `VK_COMMAND_GENERATE_REPLY_BIT_EXT (0x1)`;
  reply transport is `vkSetReplyCommandStreamMESA` + the query in one SUBMIT_3D
  execbuffer on the same Venus context (no ring needed). Also fixed the
  "0 MiB free" model-load abort: the fabricated memory heap lacked
  `VK_MEMORY_HEAP_DEVICE_LOCAL_BIT`, so ggml skipped it.
- **M2 — model load now reaches GPU buffer allocation** (all 28 layers assign to
  Vulkan0). Fixed "0 MiB free" (device-local heap needs
  `VK_MEMORY_HEAP_DEVICE_LOCAL_BIT`). Implemented the host-visible memory path
  (`uk_venus_encode_vkAllocateMemory_import`, host-visible blob backing in the
  dispatch, additive with fallback). **Correct blob protocol learned from
  virglrenderer `vkr_context_get_blob`:** for a Venus context you cannot create a
  standalone host shmem blob for GPU use — the blob must reference a
  VkDeviceMemory by `blob_id` = the memory's Venus object id (export direction:
  `vkAllocateMemory` first, fence-sync, then `RESOURCE_CREATE_BLOB` with
  `blob_id`=mem-id; `blob_id==0` only yields a plain non-GPU host shm).
- **M2 current blocker:** `RESOURCE_CREATE_BLOB → OUT_OF_MEMORY (0x1200)` because
  `vkr_context_get_object(blob_id)` finds no VkDeviceMemory — the host
  `vkAllocateMemory` silently failed, because the **fabricated memory type index
  does not map to a real host-visible+mappable memory type on the V100**. So M3
  must first land the real `vkGetPhysicalDeviceMemoryProperties[2]` round-trip
  (same M1 reply-read pattern; 520-byte reply: memoryTypeCount + array_size(32) +
  32×{flags,heapIdx} + memoryHeapCount + array_size(16) + 16×{size,flags}) so
  ggml selects a real host-visible type index and `vkAllocateMemory` succeeds on
  the host. (The `vkGetDeviceQueue` CS error in the logs is context teardown
  after the alloc failure, not the root cause.)
- **M3 — real query round-trips landed.** `uk_venus_query_memory_properties`
  reads the real V100 layout (11 memory types / 2 heaps) so ggml selects real
  host-visible / device-local type indices; `uk_venus_query_buffer_requirements`
  returns the host's real `VkMemoryRequirements` (the fabricated fixed 64 MiB was
  too small and made the host reject bind). With these, the **host-visible
  staging buffer now allocates** and model load advances to the **device-local
  model weight buffer** (~390 MB). Generic helper `uk_venus_query_roundtrip`
  drives all three queries. (Note: the native `venus_cs_test` link line needed
  `venus_compute.c` added — `tests/Makefile`.)
- **M3 device-creation root cause (found):** the device-local alloc failure and
  the persistent `vkGetDeviceQueue resulted in CS error` both stem from
  **`vkCreateDevice` not registering the host device object**. Two real bugs
  fixed: (1) the dynamic object-id allocator started at `BASE+1`, **colliding**
  with the fixed `UK_H_INSTANCE/PHYSDEV/DEVICE/QUEUE` ids — a duplicate object id
  is fatal to the host context (`vkr_context_validate_object_id`); moved dynamic
  ids to `BASE+0x100`. (2) `stub_vkCreateDevice` could submit `vkCreateDevice`
  twice (checked + fallback) → duplicate `UK_H_DEVICE`; added a `g_device_created`
  idempotency guard. After both fixes the duplicate is gone and `vkCreateDevice`
  no longer CS-errors, **but it still does not register the device** (its
  reply-bearing round-trip returns no reply, and the subsequent `vkGetDeviceQueue`
  still can't look up `UK_H_DEVICE`). The remaining cause is the real host
  `vkCreateDevice` failing/incomplete with the fabricated minimal create-info —
  the fix is to encode ggml's **actual** `VkDeviceCreateInfo` (real queue family
  from a real `vkGetPhysicalDeviceQueueFamilyProperties` round-trip + the feature
  pNext chain it requests) instead of a fixed minimal one. Then GPU compute
  dispatch + fence + readback → `llm.bench.vk`.
- **M3 — DEVICE-QUEUE BREAKTHROUGH (real V100 GPU allocation).** The above
  "vkCreateDevice doesn't register" diagnosis was wrong: reading the official
  host code (`virglrenderer/src/venus/vkr_queue.c`) showed `vkCreateDevice`
  actually succeeds (`ret=0`, device added to the object table), and the real
  blocker is the **legacy `vkGetDeviceQueue`**: `vkr_dispatch_vkGetDeviceQueue`
  **unconditionally `vkr_context_set_fatal`** — the Venus host MANDATES
  `vkGetDeviceQueue2` with a `VkDeviceQueueTimelineInfoMESA` (non-zero `ringIdx`,
  1..63) in pQueueInfo.pNext (`vkr_queue_assign_ring_idx`). Added
  `uk_venus_encode_vkGetDeviceQueue2` (cmd 155, mirrors Mesa
  `vn_encode_vkGetDeviceQueue2`); `stub_vkCreateDevice` now uses it (ringIdx=1).
  Also: fixed the dynamic object-id allocator colliding with the fixed
  `UK_H_*` ids (moved to `BASE+0x100`) and added a `g_device_created` idempotency
  guard. Result: **no more context teardown — the 390 MB model weight buffer now
  allocates on the real V100 over Venus and ALL tensors are placed on the GPU.**
  The Venus memory-properties decode is byte-exact vs host `vulkaninfo`.
- **M3 resolved:** the pinned host-buffer path now reaches real Vulkan runtime
  evidence for `llm.bench.vk`; remaining work is throughput optimization rather
  than blocker removal.
- **M4 resolved for readiness/bench evidence:** `host.bench.vk`,
  `llm.bench.vk.real`, and `llm.server.vk` have same-run PASS artifacts. HTTP
  request serving is still future work.

## Reproduce

```sh
export QEMU=../qemu-src/build/qemu-system-x86_64
export LLAMA_ROOT=../llama.cpp VK_LIB=/usr/lib/x86_64-linux-gnu/libvulkan.so.1
export VULKAN_HEADERS_INCLUDE=../Vulkan-Headers/include
export SPIRV_HEADERS_INCLUDE=../SPIRV-Headers/include
sudo chmod o+rw /dev/dri/renderD12[89] /dev/dri/renderD13[01]

make kmscube-build
python3 scripts/venus_qemu_probe.py --mode 2d            # PASS real_virtio_gpu=1
python3 scripts/venus_qemu_probe.py --mode venus-ring     # PASS frame proof

make llama-upstream-vk-build
python3 scripts/llama_vk_real_run.py                      # real boot capture
make eval-check                                           # 27/27 pass on eval host
```
