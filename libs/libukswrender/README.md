# libukswrender

`libukswrender` is VOGUE's deterministic CPU software renderer. It produces
B8G8R8X8/BGRA pixel buffers for the VirtIO-GPU 2D scanout path, enabling display
pipeline validation before virgl or Venus acceleration is available.

## Configuring applications to use `libukswrender`

Enable `CONFIG_LIBUKSWRENDER`; the library selects upstream `LIBMUSL` for `math.h` (`sinf`, `cosf`, `tanf`, `fabsf`, `fminf`, `fmaxf`).

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKSWRENDER: 'y'
```

`libukegl` selects this renderer for the current software path.

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the include paths used by dependent libraries or applications.

## Public API

The public API is declared in `include/uk/swrender.h`:

- `uk_sw_framebuf_alloc()` / `uk_sw_framebuf_free()` manage framebuffers.
- `uk_sw_framebuf_clear()` and `uk_sw_framebuf_crc()` support deterministic tests.
- `uk_sw_cube_init()` and `uk_sw_cube_render()` implement the rotating-cube proof.
- `uk_sw_draw_triangle()`, `uk_sw_draw_rect()`, and `uk_sw_draw_gradient()` support
  small benchmark scenes.

## Example

```c
struct uk_sw_framebuf fb;
struct uk_sw_cube_state cube;

uk_sw_framebuf_alloc(&fb, 1280, 800);
uk_sw_cube_init(&cube, 1.0f, 1.5f, 0.0f);
uk_sw_cube_render(&cube, &fb);
uint32_t crc = uk_sw_framebuf_crc(&fb);
uk_sw_framebuf_free(&fb);
```

## Design boundaries

This library is a correctness and substrate benchmark tool, not GPU
acceleration. It must not be described as virgl, Venus, Vulkan, or hardware
rendering evidence. Accelerated rendering remains gated in `libukvirtio_gpu` and
QEMU/Venus tests.

## Verification

Run:

```console
make -C tests native
make app-perf-check
make verify
```
