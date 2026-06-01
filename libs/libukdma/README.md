# libukdma

`libukdma` provides the minimal DMA-buffer abstraction used by VOGUE libraries.
It allocates aligned memory, records a guest physical/I/O address value, creates
single-entry scatter-gather descriptors, and exposes cache-sync hooks used by the
VirtIO-GPU path.

Current stage: DMA-backed display and Venus paths are covered by passing native
and QEMU evidence rows. This library still does not by itself prove host-visible
GPU memory performance or llama.cpp throughput.

## Configuring applications to use `libukdma`

Enable `CONFIG_LIBUKDMA` in the application `Kraftfile` or Kconfig:

```yaml
unikraft:
  kconfig:
    CONFIG_LIBUKDMA: 'y'
```

Libraries such as `libukvirtio_gpu`, `libukegl`, and
`libukgbm_compat` select or include it when they need framebuffer or command
backing memory.

`Makefile.uk` registers the library with Unikraft `addlib` and publishes the include paths used by dependent libraries or applications.

## Public API

The public API is declared in `include/uk/dma.h`:

- `uk_dma_alloc()` / `uk_dma_free()` allocate and release a DMA buffer.
- `uk_dma_build_sg()` creates scatter-gather entries for VirtIO resource backing.
- `uk_dma_sync_for_device()` and `uk_dma_sync_for_cpu()` are the device/CPU
  synchronization points.
- `UK_DMA_F_CONTIGUOUS`, `UK_DMA_F_ZEROED`, and `UK_DMA_F_HOST_SHARED` describe
  allocation intent.

## Example

```c
#include <uk/dma.h>

struct uk_dma_buf buf;
struct uk_dma_sg sg;
size_t nr_sg;

uk_dma_alloc(&buf, 1280 * 800 * 4, 4096,
             UK_DMA_F_CONTIGUOUS | UK_DMA_F_ZEROED);
uk_dma_build_sg(&buf, &sg, 1, &nr_sg);
uk_dma_sync_for_device(&buf, UK_DMA_TO_DEVICE);
uk_dma_free(&buf);
```

## Design boundaries

`libukdma` is intentionally not an IOMMU, VFIO, or host-visible memory manager.
It uses Unikraft allocation support and exposes enough metadata for current
VirtIO-GPU backing. Host-visible blob mapping for Venus remains in
`libukvirtio_gpu` and is still gated by the current Unikraft virtio-pci shared
memory BAR blocker.

## Verification

Run:

```console
make -C tests native
make verify
```

The `dma_buf_test` validates alignment, scatter-gather output, and pool behavior.
