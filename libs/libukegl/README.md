# libukegl

`libukegl` is VOGUE's bounded EGL/GLES2/GBM/DRM shim. It lets selected graphics
application sources compile against familiar EGL and GLES2 headers while routing
presentation through the VOGUE VirtIO-GPU 2D path. The current rendering backend
is `libukswrender` software output; virgl/Venus acceleration remains separately
gated.

## Configuring applications to use `libukegl`

Enable `CONFIG_LIBUKEGL` in the application `Kraftfile` or Kconfig. The library
selects `LIBUKVIRTIO_GPU`, `LIBUKDMA`, `LIBUKSWRENDER`, and upstream `LIBMUSL`.

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKEGL: 'y'
    CONFIG_LIBUKVIRTIO_GPU: 'y'
```

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the include paths used by dependent libraries or applications.

## Public API

`libukegl` installs compatibility headers used by kmscube and glmark2-style
ports:

- `EGL/egl.h` and `EGL/eglext.h`
- `GLES2/gl2.h` and `GLES2/gl2ext.h`
- `gbm.h`
- `drm-common.h`
- `drm_fourcc.h`

The implementation lives in `src/egl_glue.c`.

## Runtime behavior

- `eglInitialize()` probes `libukvirtio_gpu` and records whether a virgl context
  substrate is available.
- `eglCreateWindowSurface()` allocates a software framebuffer and DMA backing.
- `eglSwapBuffers()` renders or clears the current software frame, transfers it
  to the VirtIO-GPU 2D resource, and flushes it to scanout.

## Design boundaries

This library is not Mesa, not a full EGL implementation, and not a Vulkan/Venus
guest driver. Unsupported GL/DRM behavior must remain explicit, bounded, and
reviewable. A successful `libukegl` run proves software-render display
compatibility only (`gfx.kmscube.sw`/`gfx.glmark2.sw`), not hardware acceleration.

## Verification

Run:

```console
make -C tests native
make app-perf-check
make verify
```

The paper and evaluation matrix preserve the distinction between software rows
and blocked accelerated rows.
