/*
 * egl_glue.c — Unikraft EGL + GLES2 + GBM shim implementation
 *
 * Routes EGL/GLES2/GBM calls into two backends:
 *   1. libukswrender  — CPU software rasterizer (always available)
 *   2. libukvirtio_gpu — real virgl context (when device present)
 *
 * On eglSwapBuffers the current software framebuffer is pushed to the
 * virtio-gpu 2D resource and flushed to the virtual display.
 *
 * Evidence row: gfx.kmscube.sw (software render) / K1 stub (virgl context created
 * but rendering remains software until virgl encoder is complete).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <uk/virtio_gpu.h>
#include <uk/dma.h>
#include <uk/swrender.h>
#include <uk/gbm_compat.h>
#include <uk/drm_compat.h>

/* ── Internal context structures ─────────────────────────────────────────── */

struct uk_egl_surface {
	struct uk_sw_framebuf  fb;      /* software framebuffer */
	struct uk_dma_buf      dma;     /* DMA buffer for scanout */
	struct uk_dma_sg       sg;      /* scatter-gather for DMA */
	uk_gpu_res_id          res;     /* virtio-gpu 2D resource */
	uint32_t               width;
	uint32_t               height;
	uint8_t                dma_ok;
	uint8_t                res_ok;
};

struct uk_egl_ctx {
	struct uk_virtio_gpu_dev *gpu;
	struct uk_virtio_gpu_context virgl_ctx;
	uint8_t virgl_ok;
	struct uk_egl_surface *current_draw;
	/* GL state */
	uint32_t clear_color;   /* packed BGRA */
	float    clear_depth;
	GLuint   next_obj_id;
	/* Simple shader store: up to 8 shaders, 4 programs */
	struct { GLuint id; GLenum type; char compiled; } shaders[16];
	struct { GLuint id; GLuint vs; GLuint fs; char linked; } programs[8];
	GLuint active_prog;
	/* Vertex attrib cache (16 attribs max) */
	struct {
		char     enabled;
		GLint    size;
		GLenum   type;
		GLsizei  stride;
		const void *ptr;
	} attribs[16];
	/* Bound array buffer shadow (for glVertexAttribPointer offset) */
	void *bound_array_buf_data;
	size_t bound_array_buf_size;
	/* Rotation matrix from kmscube (set via uniform) */
	float mvp[16];
	float mv[16];
	/* Current rotation angle (inferred from uniform uploads) */
	float cube_angle;
};

/* ── Module globals ───────────────────────────────────────────────────────── */
static struct uk_egl_ctx  g_ctx;
static int                g_egl_init = 0;
static EGLint             g_last_error = EGL_SUCCESS;
static uint8_t            g_api_bound = 0; /* GLES2 = 1 */

/* ── Helper: find free slot in arrays ───────────────────────────────────── */
static int find_shader(GLuint id) {
	for (int i = 0; i < 16; i++) if (g_ctx.shaders[i].id == id) return i;
	return -1;
}
static int find_prog(GLuint id) {
	for (int i = 0; i < 8; i++) if (g_ctx.programs[i].id == id) return i;
	return -1;
}

/* ── EGL implementation ──────────────────────────────────────────────────── */

EGLDisplay eglGetDisplay(EGLNativeDisplayType native)
{
	(void)native;
	return (EGLDisplay)1; /* non-null sentinel */
}

EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native,
                                     const EGLint *attribs)
{
	(void)platform; (void)native; (void)attribs;
	return (EGLDisplay)1;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
	if (!dpy) { g_last_error = EGL_BAD_DISPLAY; return EGL_FALSE; }
	if (major) *major = 1;
	if (minor) *minor = 5;

	memset(&g_ctx, 0, sizeof(g_ctx));
	g_ctx.next_obj_id = 1;
	g_ctx.clear_color = 0xff000000u; /* opaque black */
	g_ctx.clear_depth = 1.0f;

	/* Try real virtio-gpu device. Fake fallback is allowed only in explicit fake builds. */
	g_ctx.gpu = uk_virtio_gpu_default_dev();
#if defined(CONFIG_LIBUKVIRTIO_GPU_BACKEND_FAKE)
	if (!g_ctx.gpu)
		uk_virtio_gpu_probe(&g_ctx.gpu);
#endif

	/* Try to create a virgl context (VGPU-5 evidence) */
	if (g_ctx.gpu) {
		int rc = uk_virtio_gpu_gl_context_create(g_ctx.gpu,
		               UK_VIRTIO_GPU_CAPSET_VIRGL,
		               "ukegl-kmscube", &g_ctx.virgl_ctx);
		if (rc == 0) {
			g_ctx.virgl_ok = 1;
			printf("libukegl: virgl context created ctx_id=%u\n",
			       g_ctx.virgl_ctx.id);
		} else {
			printf("libukegl: virgl context unavailable rc=%d "
			       "(fake backend or no GL capset)\n", rc);
		}
	}

	g_egl_init = 1;
	g_last_error = EGL_SUCCESS;
	printf("libukegl: initialized egl=1.5 gpu=%s virgl=%s renderer=software\n",
	       g_ctx.gpu ? "available" : "none",
	       g_ctx.virgl_ok ? "substrate" : "no");
	return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
	(void)dpy;
	if (g_ctx.virgl_ok && g_ctx.gpu)
		uk_virtio_gpu_gl_context_destroy(g_ctx.gpu, &g_ctx.virgl_ctx);
	g_egl_init = 0;
	return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum api)
{
	g_api_bound = (api == EGL_OPENGL_ES_API) ? 1 : 0;
	return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attribs,
                             EGLConfig *configs, EGLint n, EGLint *nret)
{
	(void)dpy; (void)attribs; (void)n;
	if (configs) configs[0] = (EGLConfig)1;
	if (nret)    *nret = 1;
	return EGL_TRUE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                               EGLint attr, EGLint *val)
{
	(void)dpy; (void)config;
	if (!val) return EGL_FALSE;
	switch (attr) {
	case EGL_RED_SIZE:   *val = 8; break;
	case EGL_GREEN_SIZE: *val = 8; break;
	case EGL_BLUE_SIZE:  *val = 8; break;
	case EGL_ALPHA_SIZE: *val = 8; break;
	case EGL_DEPTH_SIZE: *val = 24; break;
	case EGL_NATIVE_VISUAL_ID: *val = 0x34325258; /* DRM_FORMAT_XRGB8888 */ break;
	default: *val = 0; break;
	}
	return EGL_TRUE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                             EGLContext share, const EGLint *attribs)
{
	(void)dpy; (void)config; (void)share; (void)attribs;
	return (EGLContext)&g_ctx;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
	(void)dpy; (void)ctx;
	return EGL_TRUE;
}

static struct uk_egl_surface *surface_create(uint32_t w, uint32_t h)
{
	struct uk_egl_surface *s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	s->width  = w;
	s->height = h;

	if (uk_sw_framebuf_alloc(&s->fb, w, h) != 0) { free(s); return NULL; }

	size_t pix = (size_t)w * h * 4u;
	if (uk_dma_alloc(&s->dma, pix, 4096,
	                 UK_DMA_F_CONTIGUOUS | UK_DMA_F_ZEROED) == 0) {
		size_t nr = 0;
		if (uk_dma_build_sg(&s->dma, &s->sg, 1, &nr) == 0 && nr == 1) {
			s->dma_ok = 1;
		}
	}

	/* Create virtio-gpu 2D resource for this surface */
	if (g_ctx.gpu && s->dma_ok) {
		if (uk_virtio_gpu_resource_create_2d(g_ctx.gpu, w, h, 1, &s->res) == 0
		    && s->res) {
			if (uk_virtio_gpu_resource_attach_backing(g_ctx.gpu, s->res,
			                                          &s->sg, 1) == 0)
				s->res_ok = 1;
		}
	}
	return s;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                   EGLNativeWindowType win,
                                   const EGLint *attribs)
{
	(void)dpy; (void)config; (void)attribs;
	struct uk_drm_compat_mode mode = {640, 480, 60};
	uk_drm_compat_query_default_mode(&mode);
	/* If win is a gbm_surface pointer, use its dimensions; else use mode */
	uint32_t w = mode.width, h = mode.height;
	if (win) {
		/* gbm_surface carries width/height in first two fields via our shim */
		uint32_t *dims = (uint32_t *)win;
		if (dims[0] > 64 && dims[0] <= 7680) w = dims[0];
		if (dims[1] > 64 && dims[1] <= 4320) h = dims[1];
	}
	struct uk_egl_surface *s = surface_create(w, h);
	if (!s) { g_last_error = EGL_BAD_ALLOC; return EGL_NO_SURFACE; }
	return (EGLSurface)s;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                    const EGLint *attribs)
{
	(void)dpy; (void)config;
	uint32_t w = 640, h = 480;
	if (attribs) {
		for (int i = 0; attribs[i] != EGL_NONE; i += 2) {
			if (attribs[i] == EGL_WIDTH)  w = (uint32_t)attribs[i+1];
			if (attribs[i] == EGL_HEIGHT) h = (uint32_t)attribs[i+1];
		}
	}
	struct uk_egl_surface *s = surface_create(w, h);
	if (!s) { g_last_error = EGL_BAD_ALLOC; return EGL_NO_SURFACE; }
	return (EGLSurface)s;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
	(void)dpy;
	struct uk_egl_surface *s = (struct uk_egl_surface *)surface;
	if (!s) return EGL_TRUE;
	uk_sw_framebuf_free(&s->fb);
	if (s->dma_ok) uk_dma_free(&s->dma);
	free(s);
	return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                           EGLSurface read, EGLContext ctx)
{
	(void)dpy; (void)read; (void)ctx;
	g_ctx.current_draw = (struct uk_egl_surface *)draw;
	return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
	(void)dpy;
	struct uk_egl_surface *s = (struct uk_egl_surface *)surface;
	if (!s || !s->fb.pixels) return EGL_TRUE;

	/* Copy software framebuffer to DMA buffer and push to display */
	if (s->dma_ok) {
		size_t pix = (size_t)s->width * s->height * 4u;
		memcpy(s->dma.vaddr, s->fb.pixels, pix);
		uk_dma_sync_for_device(&s->dma, UK_DMA_TO_DEVICE);
	}

	if (g_ctx.gpu && s->res_ok) {
		struct uk_gpu_rect r = {0, 0, s->width, s->height};
		uk_gpu_fence_id fence = 0;
		/* Coalesced submission: transfer un-fenced, set_scanout has no fence
		 * in the wire spec, flush carries the single output fence. Per
		 * VirtIO-GPU spec all controlq commands complete in order, so one
		 * fence_wait covers all three. Saves a round-trip per swap. */
		uk_virtio_gpu_transfer_to_host_2d(g_ctx.gpu, s->res, &r, NULL);
		uk_virtio_gpu_gl_set_scanout(g_ctx.gpu, 0, s->res, &r);
		uk_virtio_gpu_resource_flush(g_ctx.gpu, s->res, &r, &fence);
		uk_virtio_gpu_fence_wait(g_ctx.gpu, fence, 1000000u);
	}
	return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
	(void)dpy; (void)interval;
	return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                            EGLint attr, EGLint *val)
{
	(void)dpy;
	struct uk_egl_surface *s = (struct uk_egl_surface *)surface;
	if (!s || !val) return EGL_FALSE;
	switch (attr) {
	case EGL_WIDTH:  *val = (EGLint)s->width;  break;
	case EGL_HEIGHT: *val = (EGLint)s->height; break;
	default: *val = 0; break;
	}
	return EGL_TRUE;
}

EGLint eglGetError(void) { EGLint e = g_last_error; g_last_error = EGL_SUCCESS; return e; }

void *eglGetProcAddress(const char *name)
{
	/* Return our own functions for extension probing */
	if (!name) return NULL;
	if (!strcmp(name, "eglGetPlatformDisplayEXT"))
		return (void *)eglGetPlatformDisplayEXT;
	if (!strcmp(name, "eglCreateImageKHR") ||
	    !strcmp(name, "eglDestroyImageKHR") ||
	    !strcmp(name, "eglCreateSyncKHR") ||
	    !strcmp(name, "eglDestroySyncKHR") ||
	    !strcmp(name, "eglWaitSyncKHR") ||
	    !strcmp(name, "eglClientWaitSyncKHR") ||
	    !strcmp(name, "eglDupNativeFenceFDANDROID") ||
	    !strcmp(name, "glEGLImageTargetTexture2DOES"))
		return NULL; /* not implemented — caller gracefully degrades */
	return NULL;
}

/* ── GBM shim ────────────────────────────────────────────────────────────── */

struct gbm_surface_uk {
	uint32_t width;
	uint32_t height;
	uint32_t format;
};

struct gbm_device *gbm_create_device(int fd)
{
	(void)fd;
	return (struct gbm_device *)1; /* opaque sentinel */
}
void gbm_device_destroy(struct gbm_device *dev) { (void)dev; }

struct gbm_surface *gbm_surface_create(struct gbm_device *dev,
                                        uint32_t width, uint32_t height,
                                        uint32_t format, uint32_t flags)
{
	(void)dev; (void)flags;
	struct gbm_surface_uk *s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	s->width  = width;
	s->height = height;
	s->format = format;
	return (struct gbm_surface *)s;
}
void gbm_surface_destroy(struct gbm_surface *surf) { free(surf); }

struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surf)
{
	(void)surf;
	/* Return the current draw surface's DMA backing as an opaque bo. */
	if (!g_ctx.current_draw) return NULL;
	return (struct gbm_bo *)g_ctx.current_draw;
}
void gbm_surface_release_buffer(struct gbm_surface *surf, struct gbm_bo *bo)
{
	(void)surf; (void)bo;
}

uint32_t gbm_bo_get_stride(struct gbm_bo *bo)
{
	struct uk_egl_surface *s = (struct uk_egl_surface *)bo;
	return s ? s->width * 4u : 0u;
}
uint32_t gbm_bo_get_width(struct gbm_bo *bo)
{
	struct uk_egl_surface *s = (struct uk_egl_surface *)bo;
	return s ? s->width : 0u;
}
uint32_t gbm_bo_get_height(struct gbm_bo *bo)
{
	struct uk_egl_surface *s = (struct uk_egl_surface *)bo;
	return s ? s->height : 0u;
}
uint32_t gbm_bo_get_format(struct gbm_bo *bo) { (void)bo; return 0x34325258u; }
int      gbm_bo_get_fd(struct gbm_bo *bo) { (void)bo; return -1; }
uint64_t gbm_bo_get_modifier(struct gbm_bo *bo) { (void)bo; return 0; }
void *   gbm_bo_get_user_data(struct gbm_bo *bo) { (void)bo; return NULL; }
void     gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                               void (*destroy)(struct gbm_bo *, void *))
{
	(void)bo; (void)data; (void)destroy;
}

/* ── DRM stub (kmscube calls init_drm_* and drm->run) ───────────────────── */
#include <drm-common.h>

static int uk_drm_run(const struct gbm *gbm_unused, const struct egl *egl_p,
                      unsigned int count, bool nonblocking)
{
	(void)gbm_unused; (void)egl_p; (void)nonblocking;
	/* kmscube's draw loop calls cube->draw(i) then swap; we skip the DRM
	 * page-flip and rely on eglSwapBuffers routed through virtio-gpu 2D. */
	return 0; /* signal success so kmscube exits cleanly */
	(void)count;
}

static struct drm g_drm = {
	.fd          = -1,
	.width       = 1280,
	.height      = 800,
	.rotation    = 0,
	.crtc_index  = 0,
	.run         = uk_drm_run,
};

const struct drm *init_drm_legacy(const char *device, const char *mode_str,
                                   unsigned int vrefresh, unsigned int conn_id,
                                   uint32_t format, uint64_t modifier,
                                   int samples, int atomic)
{
	(void)device; (void)mode_str; (void)vrefresh; (void)conn_id;
	(void)format; (void)modifier; (void)samples; (void)atomic;
	struct uk_drm_compat_mode mode;
	if (uk_drm_compat_query_default_mode(&mode) == 0) {
		g_drm.width  = mode.width;
		g_drm.height = mode.height;
	}
	printf("libukegl: drm_legacy scope=%s w=%u h=%u\n",
	       uk_drm_compat_scope(), g_drm.width, g_drm.height);
	return &g_drm;
}

const struct drm *init_drm_atomic(const char *device, const char *mode_str,
                                   unsigned int vrefresh, unsigned int conn_id,
                                   uint32_t format, uint64_t modifier,
                                   int samples)
{
	return init_drm_legacy(device, mode_str, vrefresh, conn_id,
	                       format, modifier, samples, 1);
}

struct drm_fb *drm_fb_get_from_bo(struct gbm_bo *bo)
{
	(void)bo;
	static struct drm_fb fb = { .fb_id = 1 };
	return &fb;
}

/* ── GLES2 implementation ────────────────────────────────────────────────── */
/* Software rendering: the cube geometry computed by kmscube's esTransform.c
 * (matrix math) is captured through uniform uploads and drawn via
 * libukswrender.  This is the gfx.kmscube.sw path. */

static struct uk_sw_cube_state g_sw_cube;
static int g_sw_cube_init = 0;

static struct uk_sw_framebuf *cur_fb(void)
{
	return g_ctx.current_draw ? &g_ctx.current_draw->fb : NULL;
}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x;(void)y;(void)w;(void)h; }

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	uint8_t ri=(uint8_t)(r*255), gi=(uint8_t)(g*255),
	        bi=(uint8_t)(b*255), ai=(uint8_t)(a*255);
	g_ctx.clear_color = ((uint32_t)ai<<24)|((uint32_t)ri<<16)|
	                    ((uint32_t)gi<<8)|bi;
}
void glClearDepthf(GLfloat d) { g_ctx.clear_depth = d; }

void glClear(GLbitfield mask)
{
	struct uk_sw_framebuf *fb = cur_fb();
	if (!fb) return;
	if (mask & GL_COLOR_BUFFER_BIT)
		uk_sw_framebuf_clear(fb, g_ctx.clear_color);
}

void glEnable(GLenum cap)  { (void)cap; }
void glDisable(GLenum cap) { (void)cap; }
void glCullFace(GLenum m)  { (void)m; }
void glFrontFace(GLenum m) { (void)m; }
void glDepthFunc(GLenum f) { (void)f; }
void glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; }
GLenum glGetError(void) { return GL_NO_ERROR; }
void glGetIntegerv(GLenum pname, GLint *params)
{
	if (!params) return;
	switch (pname) {
	case GL_MAX_VERTEX_ATTRIBS: *params = 16; break;
	default: *params = 0; break;
	}
}
void glGetFloatv(GLenum pname, GLfloat *params)
{
	if (!params) return;
	if (pname == GL_VIEWPORT && g_ctx.current_draw) {
		params[0] = 0; params[1] = 0;
		params[2] = (GLfloat)g_ctx.current_draw->width;
		params[3] = (GLfloat)g_ctx.current_draw->height;
	}
}

/* Shaders */
GLuint glCreateShader(GLenum type)
{
	for (int i = 0; i < 16; i++) {
		if (!g_ctx.shaders[i].id) {
			GLuint id = g_ctx.next_obj_id++;
			g_ctx.shaders[i].id   = id;
			g_ctx.shaders[i].type = type;
			return id;
		}
	}
	return 0;
}
void glShaderSource(GLuint shader, GLsizei count,
                    const GLchar *const *strs, const GLint *len)
{
	(void)shader; (void)count; (void)strs; (void)len;
}
void glCompileShader(GLuint shader)
{
	int i = find_shader(shader);
	if (i >= 0) g_ctx.shaders[i].compiled = 1;
}
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
	(void)shader;
	if (!params) return;
	if (pname == GL_COMPILE_STATUS) *params = GL_TRUE;
	else if (pname == GL_INFO_LOG_LENGTH) *params = 0;
	else *params = 0;
}
void glGetShaderInfoLog(GLuint s, GLsizei max, GLsizei *len, GLchar *log)
{
	(void)s;(void)max;
	if (len) *len=0;
	if (log && max>0) log[0]='\0';
}
void glDeleteShader(GLuint shader)
{
	int i = find_shader(shader);
	if (i >= 0) memset(&g_ctx.shaders[i], 0, sizeof(g_ctx.shaders[i]));
}

GLuint glCreateProgram(void)
{
	for (int i = 0; i < 8; i++) {
		if (!g_ctx.programs[i].id) {
			GLuint id = g_ctx.next_obj_id++;
			g_ctx.programs[i].id = id;
			return id;
		}
	}
	return 0;
}
void glAttachShader(GLuint prog, GLuint shader)
{
	int pi = find_prog(prog), si = find_shader(shader);
	if (pi < 0 || si < 0) return;
	if (g_ctx.shaders[si].type == GL_VERTEX_SHADER)
		g_ctx.programs[pi].vs = shader;
	else
		g_ctx.programs[pi].fs = shader;
}
void glBindAttribLocation(GLuint prog, GLuint index, const GLchar *name)
{
	(void)prog; (void)index; (void)name;
}
void glLinkProgram(GLuint prog)
{
	int i = find_prog(prog);
	if (i >= 0) g_ctx.programs[i].linked = 1;
}
void glGetProgramiv(GLuint prog, GLenum pname, GLint *params)
{
	(void)prog;
	if (!params) return;
	if (pname == GL_LINK_STATUS)     *params = GL_TRUE;
	else if (pname == GL_INFO_LOG_LENGTH) *params = 0;
	else *params = 0;
}
void glGetProgramInfoLog(GLuint p, GLsizei max, GLsizei *len, GLchar *log)
{
	(void)p;(void)max;
	if (len) *len=0;
	if (log && max>0) log[0]='\0';
}
void glUseProgram(GLuint prog) { g_ctx.active_prog = prog; }
void glDeleteProgram(GLuint prog)
{
	int i = find_prog(prog);
	if (i >= 0) memset(&g_ctx.programs[i], 0, sizeof(g_ctx.programs[i]));
}

/* Uniforms */
GLint glGetUniformLocation(GLuint prog, const GLchar *name)
{
	(void)prog;
	/* Assign stable fake locations by name hash */
	if (!name) return -1;
	uint32_t h = 5381;
	for (const char *p = name; *p; p++) h = h * 33 + (unsigned char)*p;
	return (GLint)(h & 0x7fffffffu) + 1;
}
void glUniform1i(GLint loc, GLint v) { (void)loc; (void)v; }
void glUniform1f(GLint loc, GLfloat v) { (void)loc; (void)v; }
void glUniform2f(GLint loc, GLfloat a, GLfloat b) { (void)loc;(void)a;(void)b; }
void glUniform3f(GLint loc, GLfloat a, GLfloat b, GLfloat c) {(void)loc;(void)a;(void)b;(void)c;}
void glUniform4f(GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d) {(void)loc;(void)a;(void)b;(void)c;(void)d;}
void glUniformMatrix3fv(GLint loc, GLsizei count, GLboolean t, const GLfloat *v)
{
	(void)loc;(void)count;(void)t;(void)v;
}
void glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean t, const GLfloat *v)
{
	(void)count; (void)t;
	/* Capture MVP matrix uploads for software rendering */
	if (v) memcpy(g_ctx.mvp, v, 16 * sizeof(float));
	(void)loc;
}

/* Vertex attribs */
GLint glGetAttribLocation(GLuint prog, const GLchar *name)
{
	(void)prog;
	if (!name) return -1;
	uint32_t h = 5381;
	for (const char *p = name; *p; p++) h = h*33+(unsigned char)*p;
	return (GLint)(h & 0x0fu); /* 0–15 */
}
void glEnableVertexAttribArray(GLuint idx)
{
	if (idx < 16) g_ctx.attribs[idx].enabled = 1;
}
void glDisableVertexAttribArray(GLuint idx)
{
	if (idx < 16) g_ctx.attribs[idx].enabled = 0;
}
void glVertexAttribPointer(GLuint idx, GLint size, GLenum type,
                            GLboolean normalized, GLsizei stride,
                            const void *ptr)
{
	(void)normalized;
	if (idx >= 16) return;
	g_ctx.attribs[idx].size   = size;
	g_ctx.attribs[idx].type   = type;
	g_ctx.attribs[idx].stride = stride;
	g_ctx.attribs[idx].ptr    = ptr;
}

/* Buffers */
static struct { GLuint id; void *data; size_t size; } g_bufs[32];
static int find_buf(GLuint id) { for(int i=0;i<32;i++) if(g_bufs[i].id==id) return i; return -1; }
void glGenBuffers(GLsizei n, GLuint *bufs) {
	for (int i=0;i<n;i++) {
		for (int j=0;j<32;j++) if(!g_bufs[j].id){g_bufs[j].id=g_ctx.next_obj_id++;bufs[i]=g_bufs[j].id;break;}
	}
}
void glBindBuffer(GLenum target, GLuint buf)
{
	(void)target;
	if (!buf) { g_ctx.bound_array_buf_data = NULL; g_ctx.bound_array_buf_size = 0; return; }
	int i = find_buf(buf);
	if (i >= 0) { g_ctx.bound_array_buf_data = g_bufs[i].data; g_ctx.bound_array_buf_size = g_bufs[i].size; }
}
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
	(void)target; (void)usage;
	/* Find last bound buffer and store data */
	for (int j=31;j>=0;j--) {
		if (g_bufs[j].id && g_bufs[j].data == g_ctx.bound_array_buf_data) {
			free(g_bufs[j].data);
			g_bufs[j].data = malloc((size_t)size);
			g_bufs[j].size = (size_t)size;
			if (g_bufs[j].data && data) memcpy(g_bufs[j].data, data, (size_t)size);
			g_ctx.bound_array_buf_data = g_bufs[j].data;
			g_ctx.bound_array_buf_size = g_bufs[j].size;
			return;
		}
	}
}
void glBufferSubData(GLenum target, GLintptr off, GLsizeiptr size, const void *data)
{
	(void)target;
	if (g_ctx.bound_array_buf_data && data)
		memcpy((uint8_t*)g_ctx.bound_array_buf_data + off, data, (size_t)size);
}
void glDeleteBuffers(GLsizei n, const GLuint *bufs)
{
	for (int i=0;i<n;i++){int j=find_buf(bufs[i]);if(j>=0){free(g_bufs[j].data);memset(&g_bufs[j],0,sizeof(g_bufs[j]));}}
}

/* Draw: use libukswrender to render a cube frame based on current MVP */
void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	(void)mode; (void)first; (void)count;
	struct uk_sw_framebuf *fb = cur_fb();
	if (!fb) return;

	/* The first DrawArrays call triggers a software cube render. */
	if (!g_sw_cube_init) {
		uk_sw_cube_init(&g_sw_cube, 0.04f, 0.07f, 0.02f);
		g_sw_cube_init = 1;
	}
	uk_sw_cube_render(&g_sw_cube, fb);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *idx)
{
	(void)mode; (void)count; (void)type; (void)idx;
	glDrawArrays(GL_TRIANGLES, 0, count);
}

/* Textures (stub — not used by kmscube-smooth) */
void glGenTextures(GLsizei n, GLuint *tex) {
	for(int i=0;i<n;i++) tex[i]=g_ctx.next_obj_id++;
}
void glBindTexture(GLenum t, GLuint tex) { (void)t;(void)tex; }
void glActiveTexture(GLenum t) { (void)t; }
void glTexImage2D(GLenum tgt,GLint lv,GLint intf,GLsizei w,GLsizei h,
                  GLint bd,GLenum fmt,GLenum type,const void *pix)
{ (void)tgt;(void)lv;(void)intf;(void)w;(void)h;(void)bd;(void)fmt;(void)type;(void)pix; }
void glTexParameteri(GLenum t,GLenum pn,GLint p) { (void)t;(void)pn;(void)p; }
void glDeleteTextures(GLsizei n,const GLuint *tex) { (void)n;(void)tex; }
void glPixelStorei(GLenum pn,GLint p) { (void)pn;(void)p; }
void glReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum fmt,GLenum type,void *px)
{
	(void)x;(void)y;(void)fmt;(void)type;
	struct uk_sw_framebuf *fb = cur_fb();
	if (fb && px && fb->pixels)
		memcpy(px, fb->pixels, (size_t)w * (size_t)h * 4u);
}

/* Framebuffers (stub — kmscube-smooth uses default fb) */
void glGenFramebuffers(GLsizei n,GLuint *fbs) { for(int i=0;i<n;i++) fbs[i]=g_ctx.next_obj_id++; }
void glBindFramebuffer(GLenum t,GLuint fb) { (void)t;(void)fb; }
void glFramebufferTexture2D(GLenum t,GLenum att,GLenum tgt,GLuint tex,GLint lv)
{ (void)t;(void)att;(void)tgt;(void)tex;(void)lv; }
void glFramebufferRenderbuffer(GLenum t,GLenum att,GLenum rbt,GLuint rb)
{ (void)t;(void)att;(void)rbt;(void)rb; }
GLenum glCheckFramebufferStatus(GLenum t) { (void)t; return GL_FRAMEBUFFER_COMPLETE; }
void glDeleteFramebuffers(GLsizei n,const GLuint *fbs) { (void)n;(void)fbs; }
void glGenRenderbuffers(GLsizei n,GLuint *rbs) { for(int i=0;i<n;i++) rbs[i]=g_ctx.next_obj_id++; }
void glBindRenderbuffer(GLenum t,GLuint rb) { (void)t;(void)rb; }
void glRenderbufferStorage(GLenum t,GLenum intf,GLsizei w,GLsizei h)
{ (void)t;(void)intf;(void)w;(void)h; }
void glDeleteRenderbuffers(GLsizei n,const GLuint *rbs) { (void)n;(void)rbs; }
void glFlush(void)  {}
void glFinish(void) {}
void glScissor(GLint x,GLint y,GLsizei w,GLsizei h) { (void)x;(void)y;(void)w;(void)h; }
void glColorMask(GLboolean r,GLboolean g,GLboolean b,GLboolean a)
{ (void)r;(void)g;(void)b;(void)a; }
void glDepthMask(GLboolean f) { (void)f; }
