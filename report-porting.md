# Venus encoder porting — outcome report

Implements `plan-porting-virtgpu.md`: port Mesa's Venus guest encoder into VOGUE
by a deterministic generator, sliced to the commands ggml-vulkan/llama.cpp uses.

## What was delivered

| Milestone | Status | Evidence |
|---|---|---|
| M0 pin + slice config | done | `scripts/venus/pin.json`, `test_pin.py` PASS |
| M1 ggml command manifest | done | `scripts/extract_ggml_vk_commands.py`, `config/venus_command_manifest.json` (66 wire cmds, 2 client-side) |
| M2 real reproducible generator | done | `scripts/gen_libukvenus.py generate/verify`, 38 driver headers committed + `GENERATED.lock`; coverage test PASS |
| M3 vn_cs/vn_ring shim | done | `include/uk/vn_cs.h`, `include/uk/vn_ring.h`, `vn_ring_shim.c` |
| M4 compile gate | done | `tests/venus_generated_compile_test.c` (`make -C tests venus-gen-compile`) PASS |
| M5 wire-format parity | done | `tests/venus_parity_test.c` (`make -C tests venus-parity`): 14/14 byte-identical |
| M6 cutover (image uses generated encoders) | done | see section below; native 164/0 + server runs on GPU |
| M7 build wiring + governance + docs | done | `gen-libukvenus-verify` gates `governance-check`; `GENERATOR.md` rewritten |

## Key finding: hand-written encoders already match the generated reference

Every command on the ggml compute path encodes **byte-for-byte identically**
through the hand-written `uk_venus_encode_*` and the generated `vn_encode_vk*`
(14/14 in `venus_parity_test`, e.g. `vkCreateBuffer` 92 B, `vkCmdDispatch` 28 B,
`vkCreateShaderModule` 100 B). No wire-format divergences were found — the
hand-written encoders were a faithful manual port of the Venus format.

## M6: full cutover — the image uses the generated Mesa encoders

The cutover was completed (an earlier interim step parity-locked the hand-written
encoders as a staging measure; that has been superseded). Every scalar
`uk_venus_encode_*` in `venus_cs.c`/`venus_compute.c` now builds the real `Vk*`
struct and calls the generated `vn_encode_vk*`; no hand-rolled byte layout
remains, and the now-orphaned encode primitives
(`size`/`float32`/`array_size`/`command_header`) and the `VkApplicationInfo`
helper were removed.

- `libukvenus/Makefile.uk` adds `-Igenerated -Iinclude/uk
  -I$(VULKAN_HEADERS_INCLUDE)`; the generated tree was verified to compile
  against the kraft Vulkan-Headers (VK_HEADER_VERSION 352), and the MESA structs
  (`VkRingCreateInfoMESA`, `VkDeviceQueueTimelineInfoMESA`,
  `VkCommandStreamDescriptionMESA`) are self-defined by the generated
  `defines.h`, so no special `vulkan.h` is needed.
- The bootstrap/transport encoders convert cleanly too: the generated
  `vkGetDeviceQueue2` pNext walker encodes `VkDeviceQueueTimelineInfoMESA`
  (`ringIdx`) that virglrenderer requires, and `vkCreateRingMESA` /
  `vkSetReplyCommandStreamMESA` map onto their generated `Vk*MESA` structs.
- Guards: `venus_cs_test` + `venus_compute_test` byte oracles and `venus-parity`
  stay green (164/0 native), and the Vulkan/Venus HTTP **server boots over real
  virtio-gpu-gl Venus on the Tesla V100 and serves `/health`+`/v1/models`+
  `/completion` (all 200)** with the rebuilt image — end-to-end proof the
  generated-encoder handshake works.

The other three virtgpu libs (`libukvirtgpu_drm`, `libukvirtio_gpu`,
`libukvk_icd`) are the hand-written transport/shim layer that mirrors Mesa's
non-generatable `vn_renderer_virtgpu.c` (plan Appendix A); a dead-code scan
found nothing stale to remove in them.

## Gates (host-native, no QEMU/GPU)

- `make -C tests native` → 164 passed, 0 failed (incl. new compile + parity gates)
- `make gen-libukvenus-selftest` → pin/extract/manifest/generate/coverage all PASS
- `make gen-libukvenus-verify` → committed tree matches upstream regen
- `make governance-check` → PASS apps=6 libs=9 (now depends on verify)

## Transport (reference, not generated)

`mesa/.../vn_renderer_virtgpu.c` (open `/dev/dri/renderD128`, `ioctl`, `mmap`)
is OS glue, not generatable; it is already replaced by `libukvirtio_gpu` +
`libukvirtgpu_drm`. Mapping in `plan-porting-virtgpu.md` Appendix A.
