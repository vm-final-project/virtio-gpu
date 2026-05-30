#pragma once
#include <stddef.h>
#include <stdint.h>
#include <uk/dma.h>

struct uk_buf_pool;
struct uk_buf_pool_cfg { size_t object_size; size_t object_align; size_t object_count; uint32_t dma_flags; };
int uk_buf_pool_create(struct uk_buf_pool **pool, const struct uk_buf_pool_cfg *cfg);
void uk_buf_pool_destroy(struct uk_buf_pool *pool);
int uk_buf_get(struct uk_buf_pool *pool, struct uk_dma_buf **buf);
void uk_buf_put(struct uk_buf_pool *pool, struct uk_dma_buf *buf);
