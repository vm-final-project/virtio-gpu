/* SPDX-License-Identifier: MIT */
/*
 * tests/shim/uk/sglist.h — host-native build shim for <uk/sglist.h>.
 *
 * The deterministic host-native suite compiles guest libukvirtio_gpu /
 * libukvulkan_venus sources directly against the fake VirtIO-GPU backend
 * (tests/virtio_gpu_fake.c). Those sources now describe device backing
 * memory with the upstream Unikraft scatter-gather list API
 * (unikraft/lib/uksglist). The real implementation lives in
 * unikraft/lib/uksglist/sglist.c and depends on uk/paging, uk/vmem and
 * uk/alloc, none of which are available in the host build. This shim
 * provides the same struct layout and the subset of the API the guest code
 * uses, implemented for the host's identity address mapping
 * (ss_paddr == (uintptr_t)vaddr), matching what the real uksglist produces
 * under VOGUE's direct-mapped (CONFIG_LIBUKPAGING off) configuration.
 *
 * Only the subset the guest code and tests use is provided:
 *   struct uk_sglist_seg, struct uk_sglist,
 *   uk_sglist_init, uk_sglist_reset, uk_sglist_append, uk_sglist_length.
 */
#ifndef _TESTS_SHIM_UK_SGLIST_H_
#define _TESTS_SHIM_UK_SGLIST_H_

#include <stddef.h>
#include <stdint.h>
#include <errno.h>

struct uk_sglist_seg {
	uintptr_t ss_paddr; /* Physical address (identity-mapped on host) */
	size_t    ss_len;   /* Length of the buffer */
};

struct uk_sglist {
	struct uk_sglist_seg *sg_segs;   /* Segment array (caller-provided) */
	int       sg_refs;               /* Reference count (advisory here) */
	uint16_t  sg_nseg;               /* Number of populated segments */
	uint16_t  sg_maxseg;             /* Capacity of sg_segs */
};

static inline void uk_sglist_init(struct uk_sglist *sg, uint16_t maxsegs,
				  struct uk_sglist_seg *segs)
{
	sg->sg_segs = segs;
	sg->sg_nseg = 0;
	sg->sg_maxseg = maxsegs;
	sg->sg_refs = 1;
}

static inline void uk_sglist_reset(struct uk_sglist *sg)
{
	sg->sg_nseg = 0;
}

/*
 * Append one contiguous virtual range. Under the host's identity mapping the
 * range maps to a single physical segment; coalesce with the previous segment
 * when physically adjacent, mirroring uksglist's behaviour.
 */
static inline int uk_sglist_append(struct uk_sglist *sg, void *buf, size_t len)
{
	uintptr_t pa;

	if (!sg || !sg->sg_segs)
		return EINVAL;
	if (len == 0)
		return 0;

	pa = (uintptr_t)buf;
	if (sg->sg_nseg > 0) {
		struct uk_sglist_seg *last = &sg->sg_segs[sg->sg_nseg - 1];
		if (last->ss_paddr + last->ss_len == pa) {
			last->ss_len += len;
			return 0;
		}
	}
	if (sg->sg_nseg >= sg->sg_maxseg)
		return EFBIG;
	sg->sg_segs[sg->sg_nseg].ss_paddr = pa;
	sg->sg_segs[sg->sg_nseg].ss_len = len;
	sg->sg_nseg++;
	return 0;
}

static inline size_t uk_sglist_length(struct uk_sglist *sg)
{
	size_t total = 0;
	uint16_t i;

	for (i = 0; i < sg->sg_nseg; i++)
		total += sg->sg_segs[i].ss_len;
	return total;
}

#endif /* _TESTS_SHIM_UK_SGLIST_H_ */
