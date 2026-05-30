#ifndef UK_DRM_COMPAT_H
#define UK_DRM_COMPAT_H

#include <stdint.h>

struct uk_drm_compat_mode {
	uint32_t width;
	uint32_t height;
	uint32_t refresh_hz;
};

int uk_drm_compat_query_default_mode(struct uk_drm_compat_mode *mode);
const char *uk_drm_compat_scope(void);

#endif
