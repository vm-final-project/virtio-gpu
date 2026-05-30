/*
 * uk_glue.c — Unikraft glue for upstream kmscube source files.
 *
 * Provides: vertices[], create_program(), init_gbm(), init_egl(),
 * and all perfcntr/fpscntr stubs.  Backed by libukegl shim.
 *
 * Evidence row: gfx.kmscube.sw — upstream cube-smooth.c compiled against libukegl.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "upstream/common.h"

/* ── Cube geometry (24 vertices = 6 faces × 4 corners) ──────────────────── */
const struct vertex vertices[6 * 4] = {
	/* +Z face */
	{{ 1, 1, 1}, {1,0,0}, {0,0,1}, {1,1}},
	{{-1, 1, 1}, {1,0,0}, {0,0,1}, {0,1}},
	{{ 1,-1, 1}, {1,0,0}, {0,0,1}, {1,0}},
	{{-1,-1, 1}, {1,0,0}, {0,0,1}, {0,0}},
	/* -Z face */
	{{-1, 1,-1}, {0,1,0}, {0,0,-1}, {0,1}},
	{{ 1, 1,-1}, {0,1,0}, {0,0,-1}, {1,1}},
	{{-1,-1,-1}, {0,1,0}, {0,0,-1}, {0,0}},
	{{ 1,-1,-1}, {0,1,0}, {0,0,-1}, {1,0}},
	/* +X face */
	{{ 1, 1,-1}, {0,0,1}, {1,0,0}, {1,1}},
	{{ 1, 1, 1}, {0,0,1}, {1,0,0}, {0,1}},
	{{ 1,-1,-1}, {0,0,1}, {1,0,0}, {1,0}},
	{{ 1,-1, 1}, {0,0,1}, {1,0,0}, {0,0}},
	/* -X face */
	{{-1, 1, 1}, {1,1,0}, {-1,0,0}, {0,1}},
	{{-1, 1,-1}, {1,1,0}, {-1,0,0}, {1,1}},
	{{-1,-1, 1}, {1,1,0}, {-1,0,0}, {0,0}},
	{{-1,-1,-1}, {1,1,0}, {-1,0,0}, {1,0}},
	/* +Y face */
	{{ 1, 1,-1}, {0,1,1}, {0,1,0}, {1,1}},
	{{-1, 1,-1}, {0,1,1}, {0,1,0}, {0,1}},
	{{ 1, 1, 1}, {0,1,1}, {0,1,0}, {1,0}},
	{{-1, 1, 1}, {0,1,1}, {0,1,0}, {0,0}},
	/* -Y face */
	{{ 1,-1, 1}, {1,0,1}, {0,-1,0}, {1,1}},
	{{-1,-1, 1}, {1,0,1}, {0,-1,0}, {0,1}},
	{{ 1,-1,-1}, {1,0,1}, {0,-1,0}, {1,0}},
	{{-1,-1,-1}, {1,0,1}, {0,-1,0}, {0,0}},
};

/* ── Shader compiler ─────────────────────────────────────────────────────── */
int create_program(const char *vs_src, const char *fs_src, GLuint *out_prog)
{
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	GLint  ok = 0;

	glShaderSource(vs, 1, &vs_src, NULL);
	glCompileShader(vs);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		printf("uk_glue: vertex shader compile failed\n");
		glDeleteShader(vs);
		glDeleteShader(fs);
		return -1;
	}

	glShaderSource(fs, 1, &fs_src, NULL);
	glCompileShader(fs);
	glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		printf("uk_glue: fragment shader compile failed\n");
		glDeleteShader(vs);
		glDeleteShader(fs);
		return -1;
	}

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	glDeleteShader(vs);
	glDeleteShader(fs);
	if (!ok) {
		printf("uk_glue: program link failed\n");
		glDeleteProgram(prog);
		return -1;
	}

	*out_prog = prog;
	return 0;
}

/* ── GBM stub: width/height come from virtio-gpu display info ────────────── */
static struct gbm s_gbm;

const struct gbm *init_gbm(int drm_fd, int w, int h,
                            uint32_t format, uint64_t modifier,
                            bool surfaceless)
{
	(void)drm_fd; (void)format; (void)modifier; (void)surfaceless;
	s_gbm.dev     = NULL;
	s_gbm.surface = NULL;
	s_gbm.format  = format;
	s_gbm.width   = w;
	s_gbm.height  = h;
	return &s_gbm;
}

/* ── EGL stub: backed by libukegl which manages the real EGL context ─────── */
static struct egl s_egl;

struct egl *init_egl(const struct gbm *gbm, int samples)
{
	(void)gbm; (void)samples;
	s_egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major, minor;
	eglInitialize(s_egl.display, &major, &minor);
	s_egl.config  = NULL;
	s_egl.context = EGL_NO_CONTEXT;
	s_egl.surface = EGL_NO_SURFACE;
	memset(s_egl.fbs, 0, sizeof(s_egl.fbs));
	s_egl.modifiers_supported = false;
	/* extension pointers: NULL — shim handles calls directly */
	s_egl.eglGetPlatformDisplayEXT        = NULL;
	s_egl.eglCreateImageKHR               = NULL;
	s_egl.eglDestroyImageKHR              = NULL;
	s_egl.glEGLImageTargetTexture2DOES    = NULL;
	s_egl.eglCreateSyncKHR                = NULL;
	s_egl.eglDestroySyncKHR               = NULL;
	s_egl.eglWaitSyncKHR                  = NULL;
	s_egl.eglClientWaitSyncKHR            = NULL;
	s_egl.eglDupNativeFenceFDANDROID      = NULL;
	return &s_egl;
}

/* ── Performance counter stubs ───────────────────────────────────────────── */
void init_perfcntrs(const struct egl *egl, const char *perfcntrs)
{
	(void)egl; (void)perfcntrs;
}
void start_perfcntrs(void) {}
void end_perfcntrs(void)   {}
void start_fpscntrs(void)  {}
void end_fpscntrs(void)    {}
void finish_fpscntrs(void) {}

int buf_to_fd(const struct gbm *gbm, uint32_t width, uint32_t height,
              uint32_t bpp, const void *ptr,
              uint32_t *pstride, uint64_t *modifier)
{
	(void)gbm; (void)width; (void)height; (void)bpp; (void)ptr;
	if (pstride)  *pstride   = 0;
	if (modifier) *modifier  = DRM_FORMAT_MOD_LINEAR;
	return -1; /* dmabuf export not supported in shim */
}
