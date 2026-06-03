/* SPDX-License-Identifier: MIT */
/*
 * tests/shim/uk/alloc.h — host-native build shim for <uk/alloc.h>.
 *
 * The deterministic host-native suite compiles guest sources and tests that
 * allocate device-backing memory with the upstream Unikraft allocator API
 * (uk_posix_memalign / uk_free against the default allocator). On the host
 * there is no Unikraft allocator instance; this shim maps that subset onto
 * libc so the same guest code path compiles and runs unchanged. The opaque
 * struct uk_alloc is a placeholder — its address is only used as an
 * allocator handle and never dereferenced here.
 *
 * Only the subset the guest code and tests use is provided:
 *   struct uk_alloc, uk_alloc_get_default,
 *   uk_posix_memalign, uk_memalign, uk_malloc, uk_calloc, uk_free.
 */
#ifndef _TESTS_SHIM_UK_ALLOC_H_
#define _TESTS_SHIM_UK_ALLOC_H_

#include <stddef.h>
#include <stdlib.h>

struct uk_alloc { int _placeholder; };

static inline struct uk_alloc *uk_alloc_get_default(void)
{
	static struct uk_alloc a;
	return &a;
}

static inline int uk_posix_memalign(struct uk_alloc *a, void **memptr,
				    size_t align, size_t size)
{
	(void)a;
	return posix_memalign(memptr, align, size);
}

static inline void *uk_memalign(struct uk_alloc *a, size_t align, size_t size)
{
	void *p = NULL;
	(void)a;
	if (posix_memalign(&p, align, size) != 0)
		return NULL;
	return p;
}

static inline void *uk_malloc(struct uk_alloc *a, size_t size)
{
	(void)a;
	return malloc(size);
}

static inline void *uk_calloc(struct uk_alloc *a, size_t n, size_t size)
{
	(void)a;
	return calloc(n, size);
}

static inline void uk_free(struct uk_alloc *a, void *ptr)
{
	(void)a;
	free(ptr);
}

#endif /* _TESTS_SHIM_UK_ALLOC_H_ */
