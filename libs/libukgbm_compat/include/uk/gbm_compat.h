#ifndef UK_GBM_COMPAT_H
#define UK_GBM_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#define UK_GBM_COMPAT_FORMAT_XRGB8888 0x34325258u

struct uk_gbm_compat_bo {
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t format;
	size_t size;
	uint32_t *pixels;
};

int uk_gbm_compat_bo_init(struct uk_gbm_compat_bo *bo, uint32_t width, uint32_t height, uint32_t format);
void uk_gbm_compat_bo_fill_test_pattern(struct uk_gbm_compat_bo *bo, uint32_t frame_index);
void uk_gbm_compat_bo_fini(struct uk_gbm_compat_bo *bo);
const char *uk_gbm_compat_scope(void);

#endif
