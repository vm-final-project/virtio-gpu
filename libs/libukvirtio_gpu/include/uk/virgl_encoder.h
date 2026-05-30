/* SPDX-License-Identifier: BSD-3-Clause */
/* Minimal Gallium/virgl command stream encoder for VOGUE K1 render path.
 *
 * Encodes SURFACE create, SET_FRAMEBUFFER_STATE, and CLEAR commands following
 * the virgl_protocol.h wire format used by virglrenderer. The encoder writes
 * a compact uint32 array suitable for passing to uk_virtio_gpu_gl_context_submit().
 *
 * Evidence row: K1 (virgl GPU rendering). Requires a real virgl-capable QEMU
 * device; the native test uses the fake backend to verify encoding correctness.
 */
#ifndef UK_VIRGL_ENCODER_H
#define UK_VIRGL_ENCODER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Buffer large enough for one SURFACE + one SET_FRAMEBUFFER + one CLEAR. */
#define UK_VIRGL_ENC_BUF_WORDS  32u

/* virgl surface/format constants (from virgl_hw.h / virgl_protocol.h). */
#define UK_VIRGL_FORMAT_B8G8R8X8_UNORM  2u
#define UK_VIRGL_FORMAT_B8G8R8A8_UNORM  3u

/* Gallium PIPE_TEXTURE_TARGET subset used by VOGUE resource_3d descriptors.
 * Matches src/gallium/include/pipe/p_defines.h in Mesa.
 */
#define UK_VIRGL_PIPE_TEXTURE_2D        2u

/* Gallium PIPE_BIND_* subset used by VOGUE resource_3d descriptors.
 * virglrenderer's vrend_resource_create classifies resources by bind flags;
 * a 2D texture used as a colour attachment requires PIPE_BIND_RENDER_TARGET.
 */
#define UK_VIRGL_PIPE_BIND_RENDER_TARGET 2u
#define UK_VIRGL_PIPE_BIND_SAMPLER_VIEW  4u

/* Gallium PIPE_CLEAR_* bits used in the CLEAR command buffers field. */
#define UK_VIRGL_CLEAR_COLOR0   (1u << 2)   /* PIPE_CLEAR_COLOR0 */
#define UK_VIRGL_CLEAR_DEPTH    (1u << 0)   /* PIPE_CLEAR_DEPTH  */
#define UK_VIRGL_CLEAR_STENCIL  (1u << 1)   /* PIPE_CLEAR_STENCIL */

struct uk_virgl_encoder {
	uint32_t  buf[UK_VIRGL_ENC_BUF_WORDS];
	uint32_t  pos;       /* next write position (in words) */
	int       overflow;  /* set on first overflow; no further writes */
};

/* Initialise or reset the encoder to empty. */
void uk_virgl_encoder_init(struct uk_virgl_encoder *enc);

/* Encode a CREATE_OBJECT SURFACE command.
 *   surf_handle : caller-assigned Gallium object handle (non-zero)
 *   res_handle  : VirtIO-GPU resource id to back the surface
 *   format      : UK_VIRGL_FORMAT_* constant
 *   level       : mip level (0 for base)
 */
int uk_virgl_encode_create_surface(struct uk_virgl_encoder *enc,
				   uint32_t surf_handle,
				   uint32_t res_handle,
				   uint32_t format,
				   uint32_t level);

/* Encode a SET_FRAMEBUFFER_STATE command binding surf_handle as cbuf[0].
 *   surf_handle : surface object handle created above (0 = unbind)
 */
int uk_virgl_encode_set_framebuffer(struct uk_virgl_encoder *enc,
				    uint32_t surf_handle);

/* Encode a CLEAR command clearing colour buffer 0.
 *   surf_handle : surface bound as cbuf[0] (for documentation only; not
 *                 re-encoded here — framebuffer must already be set)
 *   r,g,b,a     : clear colour [0.0–1.0]
 */
int uk_virgl_encode_clear(struct uk_virgl_encoder *enc,
			  float r, float g, float b, float a);

/* Return pointer to the encoded word buffer. */
static inline const uint32_t *
uk_virgl_encoder_buf(const struct uk_virgl_encoder *enc)
{
	return enc->buf;
}

/* Return byte length of the encoded command stream. */
static inline size_t
uk_virgl_encoder_len(const struct uk_virgl_encoder *enc)
{
	return (size_t)enc->pos * sizeof(uint32_t);
}

/* Return number of encoded words. */
static inline uint32_t
uk_virgl_encoder_words(const struct uk_virgl_encoder *enc)
{
	return enc->pos;
}

#ifdef __cplusplus
}
#endif

#endif /* UK_VIRGL_ENCODER_H */
