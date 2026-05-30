#pragma once
/* Bounded GLES 2.0 subset for Unikraft virtio-gpu-gl ports.
 * Covers the draw calls used by kmscube-smooth and glmark2-build/shading. */
#include <stdint.h>
#include <stddef.h>

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef float          GLclampf;
typedef void           GLvoid;
typedef intptr_t       GLintptr;
typedef ptrdiff_t      GLsizeiptr;
typedef char           GLchar;

/* Boolean */
#define GL_FALSE 0
#define GL_TRUE  1

/* Primitives */
#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_LINE_LOOP      0x0002
#define GL_LINE_STRIP     0x0003
#define GL_TRIANGLES      0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN   0x0006

/* Buffer objects */
#define GL_ARRAY_BUFFER         0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW          0x88B4
#define GL_DYNAMIC_DRAW         0x88B8
#define GL_STREAM_DRAW          0x88B0

/* Framebuffer / renderbuffer */
#define GL_FRAMEBUFFER  0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0  0x8CE0
#define GL_DEPTH_ATTACHMENT   0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT16  0x81A5
#define GL_RGBA4              0x8056
#define GL_RGB5_A1            0x8057
#define GL_RGB565             0x8D62

/* Clear bits */
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400

/* Data types */
#define GL_BYTE           0x1400
#define GL_UNSIGNED_BYTE  0x1401
#define GL_SHORT          0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT            0x1404
#define GL_UNSIGNED_INT   0x1405
#define GL_FLOAT          0x1406
#define GL_FIXED          0x140C

/* Pixel formats */
#define GL_RGBA            0x1908
#define GL_RGB             0x1907
#define GL_LUMINANCE       0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_ALPHA           0x1906

/* Depth test */
#define GL_NEVER    0x0200
#define GL_LESS     0x0201
#define GL_EQUAL    0x0202
#define GL_LEQUAL   0x0203
#define GL_GREATER  0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL   0x0206
#define GL_ALWAYS   0x0207
#define GL_DEPTH_TEST 0x0B71

/* Blend */
#define GL_BLEND      0x0BE2
#define GL_ZERO       0
#define GL_ONE        1
#define GL_SRC_COLOR  0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA           0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR           0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE  0x0308

/* Culling */
#define GL_CULL_FACE  0x0B44
#define GL_FRONT      0x0404
#define GL_BACK       0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW  0x0900
#define GL_CCW 0x0901

/* Shader */
#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_DELETE_STATUS   0x8B80
#define GL_VALIDATE_STATUS 0x8B83
#define GL_ACTIVE_UNIFORMS 0x8B86
#define GL_ACTIVE_ATTRIBUTES 0x8B89

/* Textures */
#define GL_TEXTURE_2D           0x0DE1
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_NEAREST              0x2600
#define GL_LINEAR               0x2601
#define GL_REPEAT               0x2901
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_TEXTURE0             0x84C0
#define GL_TEXTURE1             0x84C1
#define GL_ACTIVE_TEXTURE       0x84E0
#define GL_TEXTURE_BINDING_2D   0x8069
#define GL_UNPACK_ALIGNMENT     0x0CF5
#define GL_PACK_ALIGNMENT       0x0D05

/* Error codes */
#define GL_NO_ERROR          0
#define GL_INVALID_ENUM      0x0500
#define GL_INVALID_VALUE     0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY     0x0505

/* Vertex attrib */
#define GL_MAX_VERTEX_ATTRIBS 0x8869

/* Queries */
#define GL_CURRENT_PROGRAM   0x8B8D
#define GL_VIEWPORT          0x0BA2

/* GLES2 API subset needed by kmscube + glmark2 */
void     glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void     glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void     glClearDepthf(GLfloat d);
void     glClear(GLbitfield mask);
void     glEnable(GLenum cap);
void     glDisable(GLenum cap);
void     glCullFace(GLenum mode);
void     glFrontFace(GLenum mode);
void     glDepthFunc(GLenum func);
void     glBlendFunc(GLenum sfactor, GLenum dfactor);
GLenum   glGetError(void);
void     glGetIntegerv(GLenum pname, GLint *params);
void     glGetFloatv(GLenum pname, GLfloat *params);

/* Shaders */
GLuint   glCreateShader(GLenum type);
void     glShaderSource(GLuint shader, GLsizei count,
                         const GLchar *const *strings, const GLint *len);
void     glCompileShader(GLuint shader);
void     glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
void     glGetShaderInfoLog(GLuint shader, GLsizei max, GLsizei *len,
                             GLchar *log);
void     glDeleteShader(GLuint shader);
GLuint   glCreateProgram(void);
void     glAttachShader(GLuint prog, GLuint shader);
void     glBindAttribLocation(GLuint prog, GLuint index, const GLchar *name);
void     glLinkProgram(GLuint prog);
void     glGetProgramiv(GLuint prog, GLenum pname, GLint *params);
void     glGetProgramInfoLog(GLuint prog, GLsizei max, GLsizei *len,
                              GLchar *log);
void     glUseProgram(GLuint prog);
void     glDeleteProgram(GLuint prog);

/* Uniforms */
GLint    glGetUniformLocation(GLuint prog, const GLchar *name);
void     glUniform1i(GLint loc, GLint v0);
void     glUniform1f(GLint loc, GLfloat v0);
void     glUniform2f(GLint loc, GLfloat v0, GLfloat v1);
void     glUniform3f(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2);
void     glUniform4f(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void     glUniformMatrix3fv(GLint loc, GLsizei count, GLboolean transpose,
                              const GLfloat *value);
void     glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose,
                              const GLfloat *value);

/* Vertex attributes */
GLint    glGetAttribLocation(GLuint prog, const GLchar *name);
void     glEnableVertexAttribArray(GLuint index);
void     glDisableVertexAttribArray(GLuint index);
void     glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                GLboolean normalized, GLsizei stride,
                                const void *ptr);

/* Buffers */
void     glGenBuffers(GLsizei n, GLuint *buffers);
void     glBindBuffer(GLenum target, GLuint buffer);
void     glBufferData(GLenum target, GLsizeiptr size, const void *data,
                       GLenum usage);
void     glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                          const void *data);
void     glDeleteBuffers(GLsizei n, const GLuint *buffers);

/* Draw */
void     glDrawArrays(GLenum mode, GLint first, GLsizei count);
void     glDrawElements(GLenum mode, GLsizei count, GLenum type,
                         const void *indices);

/* Textures */
void     glGenTextures(GLsizei n, GLuint *textures);
void     glBindTexture(GLenum target, GLuint texture);
void     glActiveTexture(GLenum texture);
void     glTexImage2D(GLenum target, GLint level, GLint internalformat,
                       GLsizei width, GLsizei height, GLint border,
                       GLenum format, GLenum type, const void *pixels);
void     glTexParameteri(GLenum target, GLenum pname, GLint param);
void     glDeleteTextures(GLsizei n, const GLuint *textures);
void     glPixelStorei(GLenum pname, GLint param);
void     glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, void *pixels);

/* Framebuffers */
void     glGenFramebuffers(GLsizei n, GLuint *framebuffers);
void     glBindFramebuffer(GLenum target, GLuint framebuffer);
void     glFramebufferTexture2D(GLenum target, GLenum attachment,
                                 GLenum textarget, GLuint texture, GLint level);
void     glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                    GLenum renderbuffertarget,
                                    GLuint renderbuffer);
GLenum   glCheckFramebufferStatus(GLenum target);
void     glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
void     glGenRenderbuffers(GLsizei n, GLuint *renderbuffers);
void     glBindRenderbuffer(GLenum target, GLuint renderbuffer);
void     glRenderbufferStorage(GLenum target, GLenum internalformat,
                                GLsizei width, GLsizei height);
void     glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);
void     glFlush(void);
void     glFinish(void);
void     glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void     glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void     glDepthMask(GLboolean flag);
