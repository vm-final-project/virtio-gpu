# libukdrm_compat

`libukdrm_compat` is a bounded DRM/KMS compatibility facade for VOGUE
application ports. It provides only the small metadata surface required by the
current kmscube-style proof and does not emulate Linux `/dev/dri`, GEM, KMS
ioctls, or file-descriptor ownership.

Current stage: the compatibility facade supports passing kmscube/glmark2
software-substrate rows. Full Linux DRM/KMS behavior remains outside this
library; virtgpu ioctl translation belongs to `libukvirtgpu_drm`.

## Configuring applications to use `libukdrm_compat`

Enable `CONFIG_LIBUKDRM_COMPAT`; the library selects `LIBUKVIRTIO_GPU` because
its mode information is tied to the VOGUE display path.

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKDRM_COMPAT: 'y'
```

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the include paths used by dependent libraries or applications.

## Public API

The public API is declared in `include/uk/drm_compat.h`:

- `struct uk_drm_compat_mode` describes width, height, and refresh rate.
- `uk_drm_compat_query_default_mode()` returns the default VOGUE mode.
- `uk_drm_compat_scope()` returns a text description of the supported boundary.

## Design boundaries

This library is an application-source compatibility shim, not a Linux DRM port.
It must not grow ioctl emulation, device-node handling, GEM object management, or
PCI/virtio probing. Real device work belongs in `libukvirtio_gpu`; compatibility
shortcuts must remain explicit safe stubs.

## Verification

`kmscube_compat_test` and `app_port_check.py` exercise this boundary through:

```console
make -C tests native
make app-port-check
make verify
```
