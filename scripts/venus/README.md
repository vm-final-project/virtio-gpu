# Venus generator harness

Deterministic generation of libukvenus' Venus wire encoders from the pinned
upstream `../../venus-protocol/` checkout. VOGUE never forks Mesa's `vk.xml`
parsing or Mako templates — `scripts/gen_libukvenus.py` runs upstream
`vn_protocol.py --outdir libs/libukvenus/generated/` (driver/guest variant) and
records a sha256 `GENERATED.lock`. The driver headers are committed verbatim;
`make gen-libukvenus-verify` regenerates to a temp dir and diffs, and gates
`make governance-check`.

See `libs/libukvenus/GENERATOR.md` for the full workflow and the parity-lock
source-of-truth model.

## Files

- `pin.json` — pinned upstream commit + slice config (single source of truth).
- `test_pin.py` — assert the checkout still matches `pin.json`.
- `test_extract.py` — assert `scripts/extract_ggml_vk_commands.py` is sorted,
  deterministic, and separates wire commands from guest-side ones.
- `test_manifest.py` — assert `config/venus_command_manifest.json` matches a
  live scan of `ggml-vulkan.cpp`.
- `test_generate.py` — assert generation is reproducible, the lock matches, and
  no unexpected external includes leak in.
- `test_coverage.py` — assert every Venus wire command ggml uses has a
  generated `vn_encode_*`.
- `_runner.py` — minimal self-running test harness (the repo has no pytest).

Run all of them with `make gen-libukvenus-selftest`.

## Slice policy

The generator slices by the upstream `VK_XML_EXTENSION_LIST` (extension + core
version), not per command; unreferenced `static inline` encoders are dropped by
compile-time DCE, so the image stays minimal. `pin.json`'s `wanted_extensions`
is the set `test_coverage.py` asserts is present; the authoritative command set
is `config/venus_command_manifest.json`, regenerated from `ggml-vulkan.cpp`.
