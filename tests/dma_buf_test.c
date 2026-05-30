#include <stdint.h>
#include <stdio.h>
#include <uk/buf.h>

int main(void)
{
	struct uk_dma_buf b = {0};
	struct uk_dma_sg sg[2];
	size_t nsg = 0;
	struct uk_buf_pool *pool = NULL;
	struct uk_dma_buf *a, *c;
	struct uk_buf_pool_cfg cfg = { .object_size = 128, .object_align = 64, .object_count = 2, .dma_flags = UK_DMA_F_ZEROED };
	if (uk_dma_alloc(&b, 4096, 4096, UK_DMA_F_ZEROED) != 0) return 1;
	if (((uintptr_t)b.vaddr % 4096) != 0) return 2;
	if (uk_dma_build_sg(&b, sg, 2, &nsg) != 0 || nsg != 1 || sg[0].len != 4096) return 3;
	if (uk_dma_sync_for_device(&b, UK_DMA_TO_DEVICE) != 0 || uk_dma_sync_for_cpu(&b, UK_DMA_FROM_DEVICE) != 0) return 4;
	uk_dma_free(&b);
	if (uk_buf_pool_create(&pool, &cfg) != 0) return 5;
	if (uk_buf_get(pool, &a) != 0 || uk_buf_get(pool, &c) != 0) return 6;
	if (uk_buf_get(pool, &c) == 0) return 7;
	uk_buf_put(pool, a);
	if (uk_buf_get(pool, &a) != 0) return 8;
	uk_buf_pool_destroy(pool);
	printf("dma_buf_test passed alignment=4096 sg=1 pool=2\n");
	return 0;
}
