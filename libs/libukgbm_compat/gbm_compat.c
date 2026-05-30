#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <uk/gbm_compat.h>

int uk_gbm_compat_bo_init(struct uk_gbm_compat_bo *bo, uint32_t width, uint32_t height, uint32_t format)
{
	uint64_t stride;
	uint64_t size;
	if (!bo || !width || !height || format != UK_GBM_COMPAT_FORMAT_XRGB8888)
		return -EINVAL;
	stride = (uint64_t)width * sizeof(uint32_t);
	size = stride * height;
	if (stride > UINT32_MAX || size > SIZE_MAX)
		return -EOVERFLOW;
	memset(bo, 0, sizeof(*bo));
	bo->pixels = calloc(1, (size_t)size);
	if (!bo->pixels)
		return -ENOMEM;
	bo->width = width;
	bo->height = height;
	bo->stride = (uint32_t)stride;
	bo->format = format;
	bo->size = (size_t)size;
	return 0;
}

void uk_gbm_compat_bo_fill_test_pattern(struct uk_gbm_compat_bo *bo, uint32_t frame_index)
{
	if (!bo || !bo->pixels)
		return;
	for (uint32_t y = 0; y < bo->height; y++) {
		for (uint32_t x = 0; x < bo->width; x++) {
			uint8_t r = (uint8_t)(x + frame_index * 17u);
			uint8_t g = (uint8_t)(y + frame_index * 29u);
			uint8_t b = (uint8_t)(x ^ y ^ frame_index);
			bo->pixels[(y * bo->stride / 4u) + x] = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
		}
	}
}

void uk_gbm_compat_bo_fini(struct uk_gbm_compat_bo *bo)
{
	if (!bo)
		return;
	free(bo->pixels);
	memset(bo, 0, sizeof(*bo));
}

const char *uk_gbm_compat_scope(void)
{
	return "bounded-kmscube-only-no-full-gbm-stack";
}
