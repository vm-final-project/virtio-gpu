# libukbuf

`libukdma_pool` is the directory name for the `CONFIG_LIBUKBUF` DMA buffer-pool library.
`libukbuf` is a small Unikraft library for fixed-size DMA buffer pools used by
VOGUE device paths. It keeps short-lived command or data buffers reusable while
leaving the actual DMA allocation and cache-sync semantics to `libukdma`.

## Configuring applications to use `libukbuf`

Select the library in the application `Kraftfile` or Kconfig:

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKBUF: 'y'
```

`Config.uk` exposes `CONFIG_LIBUKBUF`; `Makefile.uk` registers the library with
`addlib` and exports the public include directory.

## Public API

The public API is declared in `include/uk/buf.h`:

- `uk_buf_pool_create()` / `uk_buf_pool_destroy()` create and tear down a pool.
- `uk_buf_get()` obtains a `struct uk_dma_buf` from the pool.
- `uk_buf_put()` returns a buffer for reuse.
- `struct uk_buf_pool_cfg` defines object size, alignment, count, and DMA flags.

## Example

```c
#include <uk/buf.h>

struct uk_buf_pool *pool;
struct uk_buf_pool_cfg cfg = {
        .object_size = 4096,
        .object_align = 4096,
        .object_count = 8,
        .dma_flags = UK_DMA_F_ZEROED,
};

uk_buf_pool_create(&pool, &cfg);
/* ... uk_buf_get()/uk_buf_put() around device commands ... */
uk_buf_pool_destroy(pool);
```

## Design boundaries

`libukbuf` does not implement a general allocator. It reuses `libukdma` and, by
extension, Unikraft allocation primitives. It should not grow PCI, virtqueue, or
device-specific policy; those belong in the corresponding driver library.

## Verification

The buffer-pool behavior is covered by the native DMA/buffer tests run through:

```console
make -C tests native
make verify
```
