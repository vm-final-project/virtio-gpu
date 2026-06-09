# libukvulkan_venus autogeneration

`libukvulkan_venus` is the Unikraft guest-side encoder for the Venus Vulkan wire
format. Mesa's [venus-protocol](https://gitlab.freedesktop.org/mesa/venus-protocol)
owns the canonical generator: it walks `vk.xml` plus
`VK_MESA_venus_protocol.xml` / `VK_EXT_command_serialization.xml` and renders
Mako templates into header-only encoders. VOGUE *consumes* that generator
through the sibling `../venus-protocol/` checkout — it never forks the 3,000+
lines of Python or the 30+ Mako templates.

This is now a **real, reproducible flow** (not a plan): the generated driver
headers are committed verbatim and CI proves they still match a fresh upstream
regen.

## Layout

```
scripts/venus/pin.json                 # pinned upstream commit + slice config
scripts/extract_ggml_vk_commands.py    # ggml-vulkan.cpp -> command manifest
config/venus_command_manifest.json     # frozen Venus wire command set (66 cmds)
scripts/gen_libukvenus.py              # generate/verify driver headers
libs/libukvulkan_venus/generated/             # committed driver headers + GENERATED.lock
libs/libukvulkan_venus/include/uk/vn_cs.h     # encoder/decoder/handle-id shim
libs/libukvulkan_venus/include/uk/vn_ring.h   # 4-function transport shim
libs/libukvulkan_venus/vn_ring_shim.c         # transport thunk (test-only; see below)
libs/libukvulkan_venus/{venus_cs,venus_init,venus_compute}.c  # in-image encoders
```

## Workflow

```sh
make gen-libukvenus-plan        # JSON plan: artifacts, slices, source script
make gen-libukvenus-check       # verify ../venus-protocol checkout + mako
make gen-libukvenus             # regenerate libs/libukvulkan_venus/generated/* + lock
make gen-libukvenus-verify      # regenerate to a temp dir + diff (CI gate)
make gen-libukvenus-selftest    # run the pin/extract/manifest/generate/coverage tests
```

The generator runs `../venus-protocol/vn_protocol.py --outdir <dir>` (no
`--renderer` ⇒ driver/guest variant), then writes `GENERATED.lock` with a
sha256 of every emitted header. `gen-libukvenus-verify` is a prerequisite of
`make governance-check`, so a drifted tree fails the governance gate.

## Slicing for image size

The generator slices by **extension** (the upstream `VK_XML_EXTENSION_LIST`)
and Vulkan core version; it does not slice per command. Every `vn_encode_*`
is `static inline`, so the encoders ggml never calls are dropped by
compile-time dead-code elimination. `config/venus_command_manifest.json` is the
deterministic list of the Venus wire commands ggml-vulkan actually uses
(derived by `scripts/extract_ggml_vk_commands.py`); `make
gen-libukvenus-selftest` asserts every one of them has a generated encoder
(`scripts/venus/test_coverage.py`). Loader-only and blob-mapped-memory commands
(`vkGetInstanceProcAddr`, `vkMapMemory`, ...) are listed under `client_side` and
intentionally excluded — they are serviced guest-side, never serialized.

## The shim (what makes the generated headers compile on Unikraft)

The generated headers depend on a tiny, upstream-documented interface, supplied
by two hand-written headers:

- `vn_cs.h` — `vn_cs_encoder_{write,reserve,get_len}`,
  `vn_cs_decoder_{read,peek,set_fatal}`, `vn_cs_handle_{load,store}_id`, and the
  `vn_cs_renderer_protocol_has_{extension,api_version}` capability gates. Maps
  onto `struct uk_venus_encoder`; Vulkan handles are bare `uint64` guest ids
  (`id == (uintptr_t)handle`).
- `vn_ring.h` — the four `vn_ring_*` functions and `struct
  vn_ring_submit_command` used by the `vn_submit_*`/`vn_call_*` wrappers, plus a
  no-op `VN_TRACE_FUNC`.

`tests/venus_generated_compile_test.c` (the `venus-gen-compile` gate) compiles
the generated tree through this shim and exercises real encoders.

## Source-of-truth model: the image uses the generated encoders

The generated encoders are the Venus wire format used **in the image**. Each
scalar `uk_venus_encode_*` entry point in `venus_cs.c`/`venus_compute.c` is a
thin bridge: it builds the real `Vk*` struct from its arguments and calls the
generated `vn_encode_vk*`. No hand-rolled byte layout remains. `libukvenus`
therefore compiles against the generated tree and the Vulkan headers
(`Makefile.uk` adds `-Igenerated -Iinclude/uk -I$(VULKAN_HEADERS_INCLUDE)`),
exactly like `libukggml_vulkan`; the generated tree is verified to build against
the kraft Vulkan-Headers (VK_HEADER_VERSION 352).

Guards: `tests/venus_parity_test.c` (the `venus-parity` gate) plus the focused
`venus_encoder_test` byte-oracle regression confirm the bridges emit the
expected streams, and `venus-gen-compile` keeps the generated tree compiling
through the shim. The Vulkan/Venus llama.cpp server boots over real
virtio-gpu-gl Venus on the evaluation host through this exact path.

`vn_ring_shim.c` provides the four `vn_ring_*` functions the generated
`vn_submit_*`/`vn_call_*` wrappers reference; the encode-only image path never
calls them, so they are dropped by DCE in the image and used only by the native
tests + the optional `vn_call_*` round-trip path.

## Upgrade procedure

1. `git -C ../venus-protocol pull` (or move to the desired Mesa SHA) and update
   `commit` in `scripts/venus/pin.json`.
2. `make gen-libukvenus` to regenerate `libs/libukvulkan_venus/generated/` + lock.
3. `make gen-libukvenus-selftest` and `make -C tests venus-parity` to confirm
   coverage and wire-format parity. If a `vn_encode_*` byte layout changed,
   update the matching in-image encoder until parity is restored — the
   generated output wins.
4. Commit `scripts/venus/pin.json` + `libs/libukvulkan_venus/generated/`.
