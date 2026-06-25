# libukvulkan_venus autogeneration

`libukvulkan_venus` is the Unikraft guest-side encoder for the Venus Vulkan wire
format. Mesa's [venus-protocol](https://gitlab.freedesktop.org/mesa/venus-protocol)
owns the canonical generator: it walks `vk.xml` plus
`VK_MESA_venus_protocol.xml` / `VK_EXT_command_serialization.xml` and renders
Mako templates into header-only encoders. VOGUE *consumes* that generator
through the sibling `../venus-protocol/` checkout — it never forks the 3,000+
lines of Python or the 30+ Mako templates.

The generated driver headers are **committed verbatim** under `generated/` (with
`GENERATED.lock` and `config/venus_command_manifest.json`) and are the source of
truth used by the image build. They were produced from Mesa's upstream
venus-protocol generator; regeneration is a manual upstream-sync step (the
`make gen-libukvenus*` wrappers and `venus-parity`/`venus-gen-compile` gates
referenced in older notes are not wired into this checkout).

## Layout

```
scripts/venus/pin.json                 # pinned upstream commit + slice config
generated/ggml_vk_commands.json        # ggml-vulkan.cpp command manifest
config/venus_command_manifest.json     # frozen Venus wire command set (66 cmds)
generated/                             # generated/verified driver headers
libs/libukvulkan_venus/generated/             # committed driver headers + GENERATED.lock
libs/libukvulkan_venus/include/uk/vn_cs.h     # encoder/decoder/handle-id shim
libs/libukvulkan_venus/include/uk/vn_ring.h   # 4-function transport shim
libs/libukvulkan_venus/ring/vn_ring_shim.c    # transport thunk (test-only; see below)
libs/libukvulkan_venus/protocol/{venus_cs,venus_compute}.c  # in-image encoders
libs/libukvulkan_venus/ring/venus_ring.c      # ring/bootstrap transport
```

## Regeneration

The committed `generated/` tree is produced by Mesa's upstream venus-protocol
generator (`vn_protocol.py`, driver/guest variant — no `--renderer`) run against
the sibling `../venus-protocol/` checkout, with `GENERATED.lock` holding a sha256
of every emitted header. This is a manual upstream-sync step (see *Upgrade
procedure* below); it is not wrapped by a `make` target in this checkout.

## Slicing for image size

The generator slices by **extension** (the upstream `VK_XML_EXTENSION_LIST`)
and Vulkan core version; it does not slice per command. Every `vn_encode_*`
is `static inline`, so the encoders ggml never calls are dropped by
compile-time dead-code elimination. `config/venus_command_manifest.json` is the
deterministic list of the Venus wire commands ggml-vulkan actually uses
(derived from the pinned ggml-vulkan source); every one of them has a generated
encoder in the committed tree (the coverage manifest). Loader-only and
blob-mapped-memory commands
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

The host-native venus tests (`make -C tests venus-encoder-core`,
`venus-capset-core`) compile the generated tree through this shim and exercise
real encoders and capset decoding.

## Source-of-truth model: the image uses the generated encoders

The generated encoders are the Venus wire format used **in the image**. Each
scalar `uk_venus_encode_*` entry point in `protocol/venus_cs.c` and
`protocol/venus_compute.c` is a
thin bridge: it builds the real `Vk*` struct from its arguments and calls the
generated `vn_encode_vk*`. No hand-rolled byte layout remains. `libukvenus`
therefore compiles against the generated tree and the Vulkan headers
(`Makefile.uk` adds `-Igenerated -Iinclude/uk -I$(VULKAN_HEADERS_INCLUDE)`),
exactly like `libukggml_vulkan`; the generated tree is verified to build against
the kraft Vulkan-Headers (VK_HEADER_VERSION 352).

Guards: the host-native venus encoder/capset tests confirm the bridges emit the
expected streams and keep the generated tree compiling through the shim. The
Vulkan/Venus llama.cpp server boots over real virtio-gpu-gl Venus on the
evaluation host through this exact path.

`ring/vn_ring_shim.c` provides the four `vn_ring_*` functions the generated
`vn_submit_*`/`vn_call_*` wrappers reference; the encode-only image path never
calls them, so they are dropped by DCE in the image and used only by the native
runtime path and optional `vn_call_*` round-trip path.

## Upgrade procedure

1. `git -C ../venus-protocol pull` (or move to the desired Mesa SHA).
2. Run the upstream `vn_protocol.py` generator into
   `libs/libukvulkan_venus/generated/` and refresh `GENERATED.lock`.
3. Run `make -C tests venus-encoder-core` / `venus-capset-core` to confirm the
   in-image encoder bridges still match. If a `vn_encode_*` byte layout changed,
   update the matching in-image encoder until parity is restored — the
   generated output wins.
4. Commit `libs/libukvulkan_venus/generated/` + `GENERATED.lock`.
