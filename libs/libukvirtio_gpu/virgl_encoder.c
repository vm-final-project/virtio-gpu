/* SPDX-License-Identifier: BSD-3-Clause */
/* Minimal Gallium/virgl command stream encoder.
 *
 * Wire format (from virglrenderer/src/virgl_protocol.h):
 *   word[0] = (length << 16) | (object_type << 8) | command
 *   word[1..length] = command-specific payload
 *
 * Command and object IDs are taken directly from the virgl_protocol.h enum so
 * virglrenderer can decode the stream without modification.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <uk/virgl_encoder.h>

/* ── virgl command / object constants ─────────────────────────────────────── */
#define VIRGL_CCMD_CREATE_OBJECT         1u
#define VIRGL_CCMD_SET_FRAMEBUFFER_STATE 5u
#define VIRGL_CCMD_CLEAR                 7u

#define VIRGL_OBJECT_SURFACE             8u

/* SURFACE object word sizes (VIRGL_OBJ_SURFACE_SIZE = 5 = words after hdr). */
#define OBJ_SURFACE_SIZE   5u
/* SET_FRAMEBUFFER_STATE with 1 cbuf = nr_cbufs + 2 = 3 words after hdr. */
#define FB_STATE_SIZE(n)   ((n) + 2u)
/* CLEAR object size = 8 words after hdr. */
#define OBJ_CLEAR_SIZE     8u

static inline uint32_t cmd_hdr(uint32_t len, uint32_t obj, uint32_t cmd)
{
	return (len << 16) | (obj << 8) | cmd;
}

static int enc_word(struct uk_virgl_encoder *enc, uint32_t w)
{
	if (enc->overflow || enc->pos >= UK_VIRGL_ENC_BUF_WORDS) {
		enc->overflow = 1;
		return -1;
	}
	enc->buf[enc->pos++] = w;
	return 0;
}

/* Reinterpret float bits as uint32 (strict-aliasing safe via memcpy). */
static uint32_t f2u(float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}

/* Reinterpret double bits as two uint32 (low, high). */
static void d2u(double d, uint32_t *lo, uint32_t *hi)
{
	uint64_t u64;
	memcpy(&u64, &d, 8);
	*lo = (uint32_t)(u64 & 0xffffffffu);
	*hi = (uint32_t)(u64 >> 32);
}

/* ── Public API ────────────────────────────────────────────────────────────── */

void uk_virgl_encoder_init(struct uk_virgl_encoder *enc)
{
	if (!enc)
		return;
	enc->pos      = 0;
	enc->overflow = 0;
	memset(enc->buf, 0, sizeof(enc->buf));
}

int uk_virgl_encode_create_surface(struct uk_virgl_encoder *enc,
				   uint32_t surf_handle,
				   uint32_t res_handle,
				   uint32_t format,
				   uint32_t level)
{
	if (!enc || !surf_handle || !res_handle)
		return -22; /* EINVAL */

	/* Header: CREATE_OBJECT / SURFACE / 5 data words. */
	if (enc_word(enc, cmd_hdr(OBJ_SURFACE_SIZE,
				   VIRGL_OBJECT_SURFACE,
				   VIRGL_CCMD_CREATE_OBJECT)))  return -1;
	if (enc_word(enc, surf_handle))  return -1; /* HANDLE   */
	if (enc_word(enc, res_handle))   return -1; /* RES_HANDLE */
	if (enc_word(enc, format))       return -1; /* FORMAT   */
	if (enc_word(enc, level))        return -1; /* FIRST_LEVEL / FIRST_ELEMENT */
	if (enc_word(enc, level))        return -1; /* LAST_LEVEL  / LAST_ELEMENT  */
	return 0;
}

int uk_virgl_encode_set_framebuffer(struct uk_virgl_encoder *enc,
				    uint32_t surf_handle)
{
	if (!enc)
		return -22;

	/* Header: SET_FRAMEBUFFER_STATE / obj=0 / 3 data words (1 cbuf). */
	if (enc_word(enc, cmd_hdr(FB_STATE_SIZE(1), 0,
				   VIRGL_CCMD_SET_FRAMEBUFFER_STATE))) return -1;
	if (enc_word(enc, 1u))          return -1; /* NR_CBUFS = 1           */
	if (enc_word(enc, 0u))          return -1; /* ZSURF_HANDLE = 0 (none) */
	if (enc_word(enc, surf_handle)) return -1; /* CBUF_HANDLE[0]         */
	return 0;
}

int uk_virgl_encode_clear(struct uk_virgl_encoder *enc,
			  float r, float g, float b, float a)
{
	uint32_t dlo, dhi;
	double   depth = 1.0;

	if (!enc)
		return -22;

	d2u(depth, &dlo, &dhi);

	/* Header: CLEAR / obj=0 / 8 data words. */
	if (enc_word(enc, cmd_hdr(OBJ_CLEAR_SIZE, 0,
				   VIRGL_CCMD_CLEAR)))   return -1;
	if (enc_word(enc, UK_VIRGL_CLEAR_COLOR0)) return -1; /* BUFFERS    */
	if (enc_word(enc, f2u(r)))    return -1;             /* COLOR[0] r */
	if (enc_word(enc, f2u(g)))    return -1;             /* COLOR[1] g */
	if (enc_word(enc, f2u(b)))    return -1;             /* COLOR[2] b */
	if (enc_word(enc, f2u(a)))    return -1;             /* COLOR[3] a */
	if (enc_word(enc, dlo))       return -1;             /* DEPTH lo   */
	if (enc_word(enc, dhi))       return -1;             /* DEPTH hi   */
	if (enc_word(enc, 0u))        return -1;             /* STENCIL    */
	return 0;
}
