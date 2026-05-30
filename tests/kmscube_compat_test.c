#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uk/drm_compat.h>
#include <uk/gbm_compat.h>

#define FAIL_IF(cond, code) do { if (cond) { printf("kmscube_compat_test fail %d\n", (code)); return (code); } } while (0)

static uint32_t checksum(const uint32_t *p, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

int main(void)
{
	struct uk_drm_compat_mode mode = {0};
	struct uk_gbm_compat_bo bo = {0};
	uint32_t c0, c1;

	FAIL_IF(uk_drm_compat_query_default_mode(NULL) == 0, 1);
	FAIL_IF(uk_drm_compat_query_default_mode(&mode) != 0, 2);
	FAIL_IF(mode.width == 0 || mode.height == 0 || mode.refresh_hz == 0, 3);
	FAIL_IF(strstr(uk_drm_compat_scope(), "no-generic-dev-dri-ioctl") == NULL, 4);

	FAIL_IF(uk_gbm_compat_bo_init(NULL, 64, 64, UK_GBM_COMPAT_FORMAT_XRGB8888) == 0, 5);
	FAIL_IF(uk_gbm_compat_bo_init(&bo, 0, 64, UK_GBM_COMPAT_FORMAT_XRGB8888) == 0, 6);
	FAIL_IF(uk_gbm_compat_bo_init(&bo, 64, 64, 0) == 0, 7);
	FAIL_IF(uk_gbm_compat_bo_init(&bo, 16, 16, UK_GBM_COMPAT_FORMAT_XRGB8888) != 0, 8);
	FAIL_IF(!bo.pixels || bo.stride != 64 || bo.size != 1024, 9);
	uk_gbm_compat_bo_fill_test_pattern(&bo, 0);
	c0 = checksum(bo.pixels, bo.size / sizeof(uint32_t));
	uk_gbm_compat_bo_fill_test_pattern(&bo, 1);
	c1 = checksum(bo.pixels, bo.size / sizeof(uint32_t));
	FAIL_IF(c0 == c1, 10);
	uk_gbm_compat_bo_fini(&bo);
	FAIL_IF(bo.pixels != NULL || bo.size != 0, 11);
	printf("kmscube_compat_test passed mode=%ux%u c0=0x%08x c1=0x%08x\n", mode.width, mode.height, c0, c1);
	return 0;
}
