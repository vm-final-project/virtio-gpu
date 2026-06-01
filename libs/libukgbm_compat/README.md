# libukgbm_compat

`libukgbm_compat` is a narrow GBM buffer-object compatibility layer used by
VOGUE ports. It provides enough buffer metadata and test-pattern support for the
current software-render/display evidence rows without importing Mesa GBM or
Linux DRM buffer management.

Current stage: GBM compatibility supports passing software and bounded frame
proof rows. DMABUF export, modifiers, and full Mesa GBM remain non-claims.

## Configuring applications to use `libukgbm_compat`

Enable `CONFIG_LIBUKGBM_COMPAT`; it selects `LIBUKDMA` for buffer backing.

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKGBM_COMPAT: 'y'
```

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the include paths used by dependent libraries or applications.

## Public API

The public API is declared in `include/uk/gbm_compat.h`:

- `uk_gbm_compat_bo_init()` initializes a bounded XRGB8888 buffer object.
- `uk_gbm_compat_bo_fill_test_pattern()` writes deterministic pixels.
- `uk_gbm_compat_bo_fini()` releases resources.
- `uk_gbm_compat_scope()` describes the supported compatibility surface.

## Design boundaries

This library is not Mesa GBM and does not manage DRM file descriptors, modifiers,
DMABUF export, or full format negotiation. Those features remain outside the
current VOGUE claim boundary unless separately implemented and tested.

## Verification

Run:

```console
make -C tests native
make app-perf-check
make verify
```
