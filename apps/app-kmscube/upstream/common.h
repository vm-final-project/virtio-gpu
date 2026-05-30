/*
 * common.h — Unikraft-compatible replacement for kmscube's common.h
 * Routes to libukegl shim instead of Linux Mesa/DRM.
 */
#ifndef _UK_COMMON_H
#define _UK_COMMON_H

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN2(A,B)  ((A)<(B)?(A):(B))
#define MAX2(A,B)  ((A)>(B)?(A):(B))

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0ULL
#endif
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID (~0ULL)
#endif
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258
#endif

#define NUM_BUFFERS 2

struct vertex {
	GLfloat position[3];
	GLfloat color[3];
	GLfloat normal[3];
	GLfloat texCoord[2];
};

extern const struct vertex vertices[6 * 4];

struct gbm {
	struct gbm_device  *dev;
	struct gbm_surface *surface;
	uint32_t format;
	int      width;
	int      height;
};

struct framebuffer {
	EGLImageKHR image;
	GLuint      tex;
	GLuint      fb;
};

struct egl {
	EGLDisplay display;
	EGLConfig  config;
	EGLContext context;
	EGLSurface surface;
	struct framebuffer fbs[NUM_BUFFERS];
	bool       modifiers_supported;
	/* extension function pointers — NULL in shim */
	void *eglGetPlatformDisplayEXT;
	void *eglCreateImageKHR;
	void *eglDestroyImageKHR;
	void *glEGLImageTargetTexture2DOES;
	void *eglCreateSyncKHR;
	void *eglDestroySyncKHR;
	void *eglWaitSyncKHR;
	void *eglClientWaitSyncKHR;
	void *eglDupNativeFenceFDANDROID;
};

struct cube {
	void (*draw)(unsigned i);
};

const struct gbm *init_gbm(int drm_fd, int w, int h,
                            uint32_t format, uint64_t modifier,
                            bool surfaceless);
struct egl *init_egl(const struct gbm *gbm, int samples);
int create_program(const char *vs_src, const char *fs_src,
                   GLuint *out_program);

const struct cube *init_cube_smooth(const struct egl *egl,
                                    const struct gbm *gbm);

void init_perfcntrs(const struct egl *egl, const char *perfcntrs);
void start_perfcntrs(void);
void end_perfcntrs(void);
void start_fpscntrs(void);
void end_fpscntrs(void);
void finish_fpscntrs(void);

int buf_to_fd(const struct gbm *gbm, uint32_t width, uint32_t height,
              uint32_t bpp, const void *ptr,
              uint32_t *pstride, uint64_t *modifier);

#endif /* _UK_COMMON_H */
