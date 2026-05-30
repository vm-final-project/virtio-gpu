# Venus generator templates

This directory documents the libukvenus generation plan. The actual Mako
templates live upstream in `../../venus-protocol/templates/` so that VOGUE
never forks Mesa's `vk.xml` parsing or wire-format renderers.

`scripts/gen_libukvenus.py` delegates to `../../venus-protocol/vn_protocol.py`
and writes output into `libs/libukvenus/generated/`. The output is rebuilt on
demand; the directory is `make clean`-removed.

See `libs/libukvenus/GENERATOR.md` for the full workflow.

## Slice policy

Only the upstream extensions actually exercised by VOGUE's pinned
`ggml-vulkan.cpp` are emitted. Adding an extension means appending it to
`WANTED_EXTENSIONS` in `scripts/gen_libukvenus.py` *and* updating a regression
test that links the new entry points. Keep the slice minimal so that
`libs/libukvenus/generated/` stays small and the resulting unikernel image
keeps its single-purpose claim.
