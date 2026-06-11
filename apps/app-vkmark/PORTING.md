# app-vkmark — vkmark Vulkan benchmark substrate port

## Upstream provenance

- **Repository**: <https://github.com/vkmark/vkmark>
- **Commit**: not vendored (local clone not present; source metadata documented here)
- **License**: LGPL-2.1+
- **Author**: Alexandros Frantzis

## Evidence

| Row | Status | Claim |
|-----|--------|-------|
| `gfx.vkmark` | `pass` | vkmark Unikraft port uses the native Venus driver (libukvulkan_venus) over libukvirtio_gpu; Venus driver open and context creation PASS; 10 scenes documented with host llvmpipe/NVIDIA baselines |

Current stage: the substrate row passes, but Unikraft-internal vkmark scene FPS
is still not claimed. The next gate is a QEMU/Venus run with non-empty render
payloads, same-run frame proof, and per-scene FPS artifacts.

## Porting boundary

This port provides build substrate proof and Venus detection — the dependency surface compiles against Unikraft shims, VirtIO-GPU capset enumeration works at boot, 10 benchmark scenes are documented as evaluation targets, and host-side llvmpipe/NVIDIA measurements provide comparison baselines.

Full vkmark scene execution is not claimed because it requires the complete Mesa Venus ICD, SPIR-V scene shaders, and same-run frame proof.

## Unikraft build system

- `Config.uk` — declares `CONFIG_APP_VKMARK` and selects `libukvulkan_venus` (native Venus driver)
- `Makefile.uk` — registers with `addlib`, uses `APPVKMARK_*` variables, lists `main.c`; vkmark meson build system is not reproduced
- `exportsyms.uk` — exports only `main`
- No dedicated `kraft/Kraftfile.*`; the substrate is exercised through
  `make vulkan-check`.

## Scene baselines

Host llvmpipe/NVIDIA scene-fps comparisons belong in canonical Vulkan evidence;
they do not live in this file so the port
metadata stays small. Unikraft-internal fps requires native Venus render payload
support plus same-run frame proof and is not claimed in this revision.

## Claim boundaries

**Allowed**: `gfx.vkmark` — scene enumeration, build substrate, ICD init, Venus context creation, host baselines documented.

**Forbidden**: vkmark fps scores inside Unikraft, GPU acceleration claims; full
score requires native Venus render payload gates and same-run frame proof.

## Verification

```sh
make vulkan-check
make verify
```
