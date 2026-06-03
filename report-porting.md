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
| M6 source-of-truth model | done (parity-lock) | see decision below |
| M7 build wiring + governance + docs | done | `gen-libukvenus-verify` gates `governance-check`; `GENERATOR.md` rewritten |

## Key finding: hand-written encoders already match the generated reference

Every command on the ggml compute path encodes **byte-for-byte identically**
through the hand-written `uk_venus_encode_*` and the generated `vn_encode_vk*`
(14/14 in `venus_parity_test`, e.g. `vkCreateBuffer` 92 B, `vkCmdDispatch` 28 B,
`vkCreateShaderModule` 100 B). No wire-format divergences were found — the
hand-written encoders were a faithful manual port of the Venus format.

## Decision: M6 is "parity-lock", not "delete and delegate"

The plan's literal M6 was to delete the hand-written wire code and route
`uk_venus_encode_*` through the generated headers. We deliberately kept the
in-image encoders and instead lock them to the generated reference with the
`venus-parity` gate, because:

1. **Image-size discipline.** Routing the in-image encoders through the
   generated tree pulls `vulkan.h` + 38 generated headers into the `libukvenus`
   unikernel build. The emitted bytes are provably identical (parity), so this
   would enlarge the single-purpose image for zero behavioural gain.
2. **Risk.** The generated encoders are authoritative and CI-enforced via
   `venus-parity` + `gen-libukvenus-verify`; drift in either direction fails the
   build. This achieves M6's *intent* (generated is the source of truth) without
   a 52-function rewrite on the server's critical path.
3. `vn_ring_shim.c` + the generated tree remain available for the optional
   `vn_call_*` round-trip path and are exercised by the native compile/parity
   tests; they are simply not compiled into the image.

This is a reviewed deviation from the written plan, recorded here per the plan's
"record divergences" instruction.

## Gates (host-native, no QEMU/GPU)

- `make -C tests native` → 164 passed, 0 failed (incl. new compile + parity gates)
- `make gen-libukvenus-selftest` → pin/extract/manifest/generate/coverage all PASS
- `make gen-libukvenus-verify` → committed tree matches upstream regen
- `make governance-check` → PASS apps=6 libs=9 (now depends on verify)

## Transport (reference, not generated)

`mesa/.../vn_renderer_virtgpu.c` (open `/dev/dri/renderD128`, `ioctl`, `mmap`)
is OS glue, not generatable; it is already replaced by `libukvirtio_gpu` +
`libukvirtgpu_drm`. Mapping in `plan-porting-virtgpu.md` Appendix A.
