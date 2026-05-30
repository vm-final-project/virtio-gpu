#pragma once
#include <GLES2/gl2.h>
/* AMD_performance_monitor stubs (used by kmscube) */
typedef void (*PFNGLGETPERFMONITORGROUPSAMDPROC)(GLint*, GLsizei, GLuint*, GLint*);
typedef void (*PFNGLGETPERFMONITORCOUNTERSAMDPROC)(GLuint, GLint*, GLint*, GLsizei, GLuint*);
typedef void (*PFNGLGETPERFMONITORGROUPSTRINGAMDPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (*PFNGLGETPERFMONITORCOUNTERSTRINGAMDPROC)(GLuint, GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (*PFNGLGETPERFMONITORCOUNTERINFOAMDPROC)(GLuint, GLuint, GLenum, void*);
typedef void (*PFNGLGENPERFMONITORSAMDPROC)(GLsizei, GLuint*);
typedef void (*PFNGLDELETEPERFMONITORSAMDPROC)(GLsizei, GLuint*);
typedef void (*PFNGLSELECTPERFMONITORCOUNTERSAMDPROC)(GLuint, GLboolean, GLuint, GLint, GLuint*);
typedef void (*PFNGLBEGINPERFMONITORAMDPROC)(GLuint);
typedef void (*PFNGLENDPERFMONITORAMDPROC)(GLuint);
typedef void (*PFNGLGETPERFMONITORCOUNTERDATAAMDPROC)(GLuint, GLenum, GLsizei, GLuint*, GLint*);
/* OES_EGL_image */
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum, void*);
