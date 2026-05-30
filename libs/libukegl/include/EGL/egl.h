#pragma once
/* Bounded EGL 1.5 subset for Unikraft virtio-gpu-gl ports.
 * Covers the surface used by kmscube and glmark2-drm. */
#include <stdint.h>
#include <stddef.h>

typedef void *     EGLDisplay;
typedef void *     EGLSurface;
typedef void *     EGLContext;
typedef void *     EGLConfig;
typedef int32_t    EGLint;
typedef uint32_t   EGLenum;
typedef unsigned char EGLBoolean;
typedef uint64_t   EGLuint64KHR;
typedef void *     EGLImageKHR;
typedef void *     EGLSyncKHR;
typedef int        EGLNativeDisplayType;
typedef void *     EGLNativeWindowType;
typedef void *     EGLNativePixmapType;

#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY   ((EGLDisplay)0)
#define EGL_NO_SURFACE   ((EGLSurface)0)
#define EGL_NO_CONTEXT   ((EGLContext)0)
#define EGL_NO_IMAGE_KHR ((EGLImageKHR)0)
#define EGL_TRUE  ((EGLBoolean)1)
#define EGL_FALSE ((EGLBoolean)0)

/* Error codes */
#define EGL_SUCCESS                0x3000
#define EGL_NOT_INITIALIZED        0x3001
#define EGL_BAD_ACCESS             0x3002
#define EGL_BAD_ALLOC              0x3003
#define EGL_BAD_ATTRIBUTE          0x3004
#define EGL_BAD_CONFIG             0x3005
#define EGL_BAD_CONTEXT            0x3006
#define EGL_BAD_CURRENT_SURFACE    0x3007
#define EGL_BAD_DISPLAY            0x3008
#define EGL_BAD_MATCH              0x3009
#define EGL_BAD_NATIVE_PIXMAP      0x300A
#define EGL_BAD_NATIVE_WINDOW      0x300B
#define EGL_BAD_PARAMETER          0x300C
#define EGL_BAD_SURFACE            0x300D
#define EGL_CONTEXT_LOST           0x300E

/* Config attributes */
#define EGL_BUFFER_SIZE            0x3020
#define EGL_ALPHA_SIZE             0x3021
#define EGL_BLUE_SIZE              0x3022
#define EGL_GREEN_SIZE             0x3023
#define EGL_RED_SIZE               0x3024
#define EGL_DEPTH_SIZE             0x3025
#define EGL_STENCIL_SIZE           0x3026
#define EGL_CONFIG_CAVEAT          0x3027
#define EGL_CONFIG_ID              0x3028
#define EGL_LEVEL                  0x3029
#define EGL_MAX_PBUFFER_HEIGHT     0x302A
#define EGL_MAX_PBUFFER_PIXELS     0x302B
#define EGL_MAX_PBUFFER_WIDTH      0x302C
#define EGL_NATIVE_RENDERABLE      0x302D
#define EGL_NATIVE_VISUAL_ID       0x302E
#define EGL_NATIVE_VISUAL_TYPE     0x302F
#define EGL_SAMPLES                0x3031
#define EGL_SAMPLE_BUFFERS         0x3032
#define EGL_SURFACE_TYPE           0x3033
#define EGL_TRANSPARENT_TYPE       0x3034
#define EGL_TRANSPARENT_BLUE_VALUE 0x3035
#define EGL_TRANSPARENT_GREEN_VALUE 0x3036
#define EGL_TRANSPARENT_RED_VALUE  0x3037
#define EGL_NONE                   0x3038
#define EGL_BIND_TO_TEXTURE_RGB    0x3039
#define EGL_BIND_TO_TEXTURE_RGBA   0x303A
#define EGL_MIN_SWAP_INTERVAL      0x303B
#define EGL_MAX_SWAP_INTERVAL      0x303C
#define EGL_LUMINANCE_SIZE         0x303D
#define EGL_ALPHA_MASK_SIZE        0x303E
#define EGL_COLOR_BUFFER_TYPE      0x303F
#define EGL_RENDERABLE_TYPE        0x3040
#define EGL_MATCH_NATIVE_PIXMAP    0x3041
#define EGL_CONFORMANT             0x3042
#define EGL_WIDTH                  0x3057
#define EGL_HEIGHT                 0x3056
#define EGL_LARGEST_PBUFFER        0x3058
#define EGL_TEXTURE_FORMAT         0x3080
#define EGL_TEXTURE_TARGET         0x3081
#define EGL_MIPMAP_TEXTURE         0x3082
#define EGL_MIPMAP_LEVEL           0x3083
#define EGL_RENDER_BUFFER          0x3086
#define EGL_SINGLE_BUFFER          0x3085
#define EGL_BACK_BUFFER            0x3084
#define EGL_SWAP_BEHAVIOR          0x3093
#define EGL_BUFFER_PRESERVED       0x3094
#define EGL_BUFFER_DESTROYED       0x3095
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API          0x30A0
#define EGL_OPENVG_API             0x30A1
#define EGL_OPENGL_API             0x30A2
#define EGL_OPENGL_ES_BIT          0x0001
#define EGL_OPENVG_BIT             0x0002
#define EGL_OPENGL_ES2_BIT         0x0004
#define EGL_OPENGL_BIT             0x0008
#define EGL_WINDOW_BIT             0x0004
#define EGL_PBUFFER_BIT            0x0001
#define EGL_PIXMAP_BIT             0x0002
#define EGL_VG_COLORSPACE_LINEAR_BIT 0x0020
#define EGL_VG_ALPHA_FORMAT_PRE_BIT  0x0040
#define EGL_MULTISAMPLE_RESOLVE_BOX_BIT 0x0200
#define EGL_SWAP_BEHAVIOR_PRESERVED_BIT 0x0400
#define EGL_PLATFORM_GBM_KHR         0x31D7
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD

/* EGL API */
EGLDisplay eglGetDisplay(EGLNativeDisplayType native);
EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native,
                                     const EGLint *attribs);
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean eglTerminate(EGLDisplay dpy);
EGLBoolean eglBindAPI(EGLenum api);
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attribs,
                            EGLConfig *configs, EGLint config_size,
                            EGLint *num_config);
EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                               EGLint attribute, EGLint *value);
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                             EGLContext share, const EGLint *attribs);
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                   EGLNativeWindowType win,
                                   const EGLint *attribs);
EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                    const EGLint *attribs);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                           EGLSurface read, EGLContext ctx);
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                            EGLint attribute, EGLint *value);
EGLint     eglGetError(void);
void *     eglGetProcAddress(const char *name);
