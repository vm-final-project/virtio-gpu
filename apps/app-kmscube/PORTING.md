# app-kmscube — kmscube VirtIO-GPU/Unikraft proof harness

## Upstream provenance

- **Repository**: <https://gitlab.freedesktop.org/mesa/kmscube>
- **Vendored subset**: removed — `upstream/` (cube-smooth.c, esTransform.c, esUtil.h, common.h) and `uk_glue.c` have been deleted as part of the virgl-only refactor.
- **Local glue**: none — the app calls libukvirtio_gpu virgl APIs directly from `main.c`.
- **License**: follows the vendored kmscube source headers and project license notices.

## Evidence

| Row | Status | Claim |
|-----|--------|-------|
| `gfx.kmscube.submit` | `pass` on the evaluation host | virgl submit proof with same-run QEMU and real VirtIO-GPU evidence |
| `gfx.kmscube.frame` | `pass` on the evaluation host | colour-band frame proof from same-run QEMU screendump/pixel evidence |

Current stage: virgl rows pass on the evaluation host. On hosts without
QEMU GL/Venus scanout read-back, the QEMU rows must remain structured
`blocked:*` rows and cannot be promoted from native software evidence alone.

## Porting boundary

This app is the canonical VOGUE graphics proof harness. It boots directly
into one appliance entrypoint and exercises the virgl command-stream path
through libukvirtio_gpu. The software-render path (`libukswrender`) has been
removed; `uk_glue.c` and `upstream/` (cube-smooth.c, esTransform.c) no longer
exist in this tree.

The app does not require libukegl, libukswrender, libukdrm_compat, or
libukgbm_compat. The port does not import Mesa, Linux DRM/KMS, a shell, or a
native window-system launcher into the guest.

## Unikraft build system

- `Config.uk` — declares `CONFIG_APP_KMSCUBE` and selects libukvirtio_gpu
- `Makefile.uk` — registers `appkmscube`, compiles `main.c` only
- `exportsyms.uk` — exports only `main`
- `Kraftfile` and `kraft/Kraftfile.kmscube-vgpu-gl` — select `app-kmscube` for reviewer-facing images; only libmusl and libukvirtio_gpu are required

## Claim boundaries

**Allowed**: `gfx.kmscube.submit` and `gfx.kmscube.frame` virgl evidence, plus
structured `blocked:*` rows for QEMU/virgl/frame prerequisites when the gate exits
zero under existing policy.

**Forbidden**: claiming Mesa compatibility, full kmscube GLES acceleration,
Unikraft GPU fps, K1/virgl frame success without same-run `kmscube-check`
PASS artifacts, or `gfx.kmscube.sw` software-render evidence (path no longer exists).

## Verification

```sh
make kmscube-build   # build the virgl-proof appliance
make test-core       # virgl encoder + VirtIO-GPU core unit tests (the virgl path)
make test-fast       # full host-native gate
```
