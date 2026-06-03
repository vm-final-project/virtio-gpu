/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * dma_buf_test — device-backing memory allocation + scatter-gather.
 *
 * VOGUE no longer ships a first-party DMA buffer/pool library; backing memory
 * is allocated with the upstream Unikraft allocator API (uk_posix_memalign)
 * and described with the upstream scatter-gather list API (uksglist). On the
 * host these resolve to the shims in tests/shim/uk/. This test exercises that
 * path: aligned allocation and a single coalesced sg segment.
 */
#include <stdint.h>
#include <stdio.h>
#include <uk/alloc.h>
#include <uk/sglist.h>

int main(void)
{
	struct uk_alloc *a = uk_alloc_get_default();
	struct uk_sglist sg;
	struct uk_sglist_seg seg[2];
	void *p = NULL;

	/* 4096-aligned device-backing allocation. */
	if (uk_posix_memalign(a, &p, 4096, 4096) != 0 || !p)
		return 1;
	if (((uintptr_t)p % 4096) != 0)
		return 2;

	/* Describe it as a scatter-gather list: one contiguous segment. */
	uk_sglist_init(&sg, 2, seg);
	if (uk_sglist_append(&sg, p, 4096) != 0)
		return 3;
	if (sg.sg_nseg != 1 || sg.sg_segs[0].ss_len != 4096)
		return 4;
	if (uk_sglist_length(&sg) != 4096)
		return 5;

	uk_free(a, p);
	printf("dma_buf_test passed alignment=4096 sg=1 len=4096\n");
	return 0;
}
