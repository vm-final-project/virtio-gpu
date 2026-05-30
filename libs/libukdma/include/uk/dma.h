#pragma once
#include <stddef.h>
#include <stdint.h>

enum uk_dma_dir { UK_DMA_TO_DEVICE, UK_DMA_FROM_DEVICE, UK_DMA_BIDIRECTIONAL };
#define UK_DMA_F_CONTIGUOUS (1u << 0)
#define UK_DMA_F_ZEROED (1u << 1)
#define UK_DMA_F_HOST_SHARED (1u << 2)

struct uk_dma_buf { void *vaddr; uintptr_t paddr_or_iova; size_t len; size_t align; uint32_t flags; };
struct uk_dma_sg { uintptr_t paddr_or_iova; size_t len; };

int uk_dma_alloc(struct uk_dma_buf *buf, size_t len, size_t align, uint32_t flags);
void uk_dma_free(struct uk_dma_buf *buf);
int uk_dma_build_sg(const struct uk_dma_buf *buf, struct uk_dma_sg *sg, size_t max_sg, size_t *nr_sg);
int uk_dma_sync_for_device(struct uk_dma_buf *buf, enum uk_dma_dir dir);
int uk_dma_sync_for_cpu(struct uk_dma_buf *buf, enum uk_dma_dir dir);
