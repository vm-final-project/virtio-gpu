#include <errno.h>
#include <stddef.h>

#include <uk/drm_compat.h>

int uk_drm_compat_query_default_mode(struct uk_drm_compat_mode *mode)
{
	if (!mode)
		return -EINVAL;
	mode->width = 1280;
	mode->height = 800;
	mode->refresh_hz = 60;
	return 0;
}

const char *uk_drm_compat_scope(void)
{
	return "bounded-kmscube-only-no-generic-dev-dri-ioctl";
}
