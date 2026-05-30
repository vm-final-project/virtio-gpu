#pragma once
/* Stub for kmscube drm-common.h used only via kmscube upstream.
 * Unikraft replaces DRM page-flip loop with virtio-gpu 2D flush. */
#include <stdint.h>
#include <stdbool.h>
#include <EGL/egl.h>

#define DRM_DISPLAY_MODE_LEN 32

struct gbm;
struct gbm_bo;
struct egl;

struct drm_fb {
	uint32_t fb_id;
};

struct drm {
	int fd;
	unsigned int width;
	unsigned int height;
	unsigned int rotation;
	unsigned int crtc_index;
	int (*run)(const struct gbm *gbm, const struct egl *egl,
	           unsigned int count, bool nonblocking);
};

const struct drm *init_drm_legacy(const char *device, const char *mode_str,
                                   unsigned int vrefresh,
                                   unsigned int connector_id,
                                   uint32_t format, uint64_t modifier,
                                   int samples, int atomic);
const struct drm *init_drm_atomic(const char *device, const char *mode_str,
                                   unsigned int vrefresh,
                                   unsigned int connector_id,
                                   uint32_t format, uint64_t modifier,
                                   int samples);

struct drm_fb *drm_fb_get_from_bo(struct gbm_bo *bo);
