# app-kmscube — kmscube VirtIO-GPU/Unikraft proof harness

## Upstream provenance

- **Repository**: <https://gitlab.freedesktop.org/mesa/kmscube>
- **Vendored subset**: `upstream/cube-smooth.c`, `upstream/esTransform.c`, `upstream/esUtil.h`, and a Unikraft-compatible `upstream/common.h`
- **Local glue**: `uk_glue.c` supplies the bounded EGL/GBM/DRM compatibility hooks used by the vendored source.
- **License**: follows the vendored kmscube source headers and project license notices.

## Evidence

| Row | Status | Claim |
|-----|--------|-------|
| `gfx.kmscube.sw` | `pass` when `make app-perf-check`/`make eval-check` pass | software-rendered cube frames flow through the Unikraft VirtIO-GPU 2D/display substrate |
| `gfx.kmscube.submit` | `blocked:*` or `pass` from `make kmscube-check` | virgl submit proof only when QEMU and the real VirtIO-GPU path provide same-run evidence |
| `gfx.kmscube.frame` | `blocked:*` or `pass` from `make kmscube-check` | frame proof only when QEMU screendump/pixel evidence is captured in the same run |

## Porting boundary

This app is the canonical VOGUE graphics proof harness. It compiles the bounded
kmscube source subset against Unikraft compatibility shims, then boots directly
into one appliance entrypoint. The default supported proof is the CPU
`libukswrender` cube path backed by VirtIO-GPU 2D scanout; the virgl path is a
probe and remains claim-gated by `kmscube-check` artifacts.

The port does not import Mesa, Linux DRM/KMS, a shell, or a native window-system
launcher into the guest. Full virgl/GLES acceleration is not implied by the
software-render pass.

## Unikraft build system

- `Config.uk` — declares `CONFIG_APP_KMSCUBE` and selects the bounded VOGUE graphics libraries
- `Makefile.uk` — registers `appkmscube`, compiles `main.c`, `uk_glue.c`, and the vendored kmscube subset
- `exportsyms.uk` — exports only `main`
- `Kraftfile` and `kraft/Kraftfile.kmscube-vgpu-gl` — select `app-kmscube` for reviewer-facing images

## Claim boundaries

**Allowed**: `gfx.kmscube.sw` software-render/display-substrate evidence, plus
structured `blocked:*` rows for QEMU/virgl/frame prerequisites when the gate exits
zero under existing policy.

**Forbidden**: claiming Mesa compatibility, full kmscube GLES acceleration,
Unikraft GPU fps, or K1/virgl frame success without same-run `kmscube-check`
PASS artifacts.

## Verification

```sh
make app-port-check
make app-perf-check
make kmscube-check
make eval-check
```
