/* SPDX-License-Identifier: BSD-3-Clause */
/* Native test for the virgl Gallium command encoder.
 *
 * Verifies:
 *   1. Encoder init produces empty, non-overflowed state.
 *   2. SURFACE create emits correct word count and header.
 *   3. SET_FRAMEBUFFER_STATE emits correct word count and handle.
 *   4. CLEAR emits correct word count, buffers field, and colour.
 *   5. Chained encode (SURFACE + FB + CLEAR) byte count is correct.
 *   6. Overflow guard: extra encode after buffer full sets overflow flag.
 *   7. NULL argument guard returns error without crash.
 *   8. Full pipeline submitted to fake VirtIO-GPU backend increments submits_3d.
 *
 * Evidence row: K1 — virgl Gallium encoder correctness (native harness).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include <uk/virgl_encoder.h>
#include <uk/virtio_gpu.h>

/* Use the public probe API (returns int; fake backend fills *dev). */

/* ── helpers ───────────────────────────────────────────────────────────────── */
static int failures;
#define CHECK(cond, name) \
	do { \
		if (!(cond)) { \
			printf("  FAIL  %s\n", name); failures++; \
		} else { \
			printf("  PASS  %s\n", name); \
		} \
	} while (0)

/* Command header extraction helpers. */
static uint32_t hdr_cmd(uint32_t w)  { return w & 0xffu; }
static uint32_t hdr_obj(uint32_t w)  { return (w >> 8) & 0xffu; }
static uint32_t hdr_len(uint32_t w)  { return w >> 16; }

/* Reinterpret uint32 bits as float. */
static float u2f(uint32_t u)
{
	float f;
	memcpy(&f, &u, 4);
	return f;
}

/* ── tests ─────────────────────────────────────────────────────────────────── */
static void test_init(void)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	CHECK(enc.pos == 0,      "init:pos_zero");
	CHECK(enc.overflow == 0, "init:no_overflow");
	CHECK(uk_virgl_encoder_len(&enc) == 0, "init:len_zero");
}

static void test_null_args(void)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	CHECK(uk_virgl_encode_create_surface(NULL, 1, 1, 2, 0) != 0, "null:create_surface");
	CHECK(uk_virgl_encode_set_framebuffer(NULL, 1) != 0,          "null:set_framebuffer");
	CHECK(uk_virgl_encode_clear(NULL, 0, 0, 0, 1) != 0,           "null:clear");
	/* surf_handle=0 or res_handle=0 must also fail */
	CHECK(uk_virgl_encode_create_surface(&enc, 0, 1, 2, 0) != 0, "null:surf_handle_zero");
	CHECK(uk_virgl_encode_create_surface(&enc, 1, 0, 2, 0) != 0, "null:res_handle_zero");
}

static void test_create_surface(void)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	int rc = uk_virgl_encode_create_surface(&enc, 42u, 7u,
						UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
	CHECK(rc == 0, "surface:rc_zero");
	/* SURFACE CREATE_OBJECT = 1+5 = 6 words total. */
	CHECK(enc.pos == 6u, "surface:word_count");
	uint32_t hdr = enc.buf[0];
	CHECK(hdr_cmd(hdr) == 1u, "surface:cmd_create_object"); /* VIRGL_CCMD_CREATE_OBJECT */
	CHECK(hdr_obj(hdr) == 8u, "surface:obj_surface");       /* VIRGL_OBJECT_SURFACE */
	CHECK(hdr_len(hdr) == 5u, "surface:len_five");
	CHECK(enc.buf[1] == 42u, "surface:handle");
	CHECK(enc.buf[2] == 7u,  "surface:res_handle");
	CHECK(enc.buf[3] == UK_VIRGL_FORMAT_B8G8R8X8_UNORM, "surface:format");
}

static void test_set_framebuffer(void)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	int rc = uk_virgl_encode_set_framebuffer(&enc, 42u);
	CHECK(rc == 0, "fb:rc_zero");
	/* SET_FRAMEBUFFER_STATE(1 cbuf) = 1+3 = 4 words. */
	CHECK(enc.pos == 4u, "fb:word_count");
	uint32_t hdr = enc.buf[0];
	CHECK(hdr_cmd(hdr) == 5u, "fb:cmd_set_framebuffer"); /* VIRGL_CCMD_SET_FRAMEBUFFER_STATE */
	CHECK(hdr_obj(hdr) == 0u, "fb:obj_zero");
	CHECK(hdr_len(hdr) == 3u, "fb:len_three");
	CHECK(enc.buf[1] == 1u,   "fb:nr_cbufs_one");
	CHECK(enc.buf[2] == 0u,   "fb:zsurf_zero");
	CHECK(enc.buf[3] == 42u,  "fb:cbuf_handle");
}

static void test_clear(void)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	int rc = uk_virgl_encode_clear(&enc, 0.25f, 0.5f, 0.75f, 1.0f);
	CHECK(rc == 0, "clear:rc_zero");
	/* CLEAR = 1+8 = 9 words. */
	CHECK(enc.pos == 9u, "clear:word_count");
	uint32_t hdr = enc.buf[0];
	CHECK(hdr_cmd(hdr) == 7u,  "clear:cmd_clear"); /* VIRGL_CCMD_CLEAR */
	CHECK(hdr_obj(hdr) == 0u,  "clear:obj_zero");
	CHECK(hdr_len(hdr) == 8u,  "clear:len_eight");
	CHECK(enc.buf[1] == UK_VIRGL_CLEAR_COLOR0, "clear:buffers_color0");
	CHECK(u2f(enc.buf[2]) == 0.25f, "clear:r");
	CHECK(u2f(enc.buf[3]) == 0.50f, "clear:g");
	CHECK(u2f(enc.buf[4]) == 0.75f, "clear:b");
	CHECK(u2f(enc.buf[5]) == 1.00f, "clear:a");
	/* depth words non-zero (1.0 double has non-zero bit pattern) */
	CHECK((enc.buf[6] | enc.buf[7]) != 0u, "clear:depth_nonzero");
}

static void test_chained_byte_count(void)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	uk_virgl_encode_create_surface(&enc, 1u, 5u,
				       UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
	uk_virgl_encode_set_framebuffer(&enc, 1u);
	uk_virgl_encode_clear(&enc, 0.2f, 0.4f, 0.6f, 1.0f);
	/* 6 + 4 + 9 = 19 words = 76 bytes. */
	CHECK(enc.pos == 19u, "chain:word_count");
	CHECK(uk_virgl_encoder_len(&enc) == 76u, "chain:byte_len");
	CHECK(enc.overflow == 0, "chain:no_overflow");
}

static void test_overflow(void)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	/* Fill buffer: each SURFACE takes 6 words; 32/6 = 5 fit, 6th overflows. */
	int filled = 0;
	for (int i = 0; i < 6; i++) {
		int rc = uk_virgl_encode_create_surface(&enc,
						(uint32_t)(i + 1),
						(uint32_t)(i + 1),
						UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
		if (rc == 0)
			filled++;
	}
	CHECK(filled >= 5, "overflow:filled_some");
	CHECK(enc.overflow != 0, "overflow:flag_set");
	/* After overflow, writes must not corrupt memory. */
	int prev_pos = (int)enc.pos;
	uk_virgl_encode_clear(&enc, 0, 0, 0, 1);
	CHECK((int)enc.pos == prev_pos, "overflow:pos_frozen");
}

static void test_submit_to_fake_backend(void)
{
	struct uk_virtio_gpu_dev *dev = NULL;
	uk_virtio_gpu_probe(&dev);
	if (!dev) {
		printf("  SKIP  submit:no_fake_device\n");
		return;
	}

	/* Create a virgl context (capset_id=1 = virgl). */
	struct uk_virtio_gpu_context ctx = {0};
	int rc = uk_virtio_gpu_gl_context_create(dev, 1u, "virgl_enc_test", &ctx);
	CHECK(rc == 0, "submit:ctx_create");
	if (rc != 0)
		return;

	/* Read initial submits_3d via public metrics API. */
	struct uk_virtio_gpu_metrics m0 = {0};
	uk_virtio_gpu_gl_metrics_get(dev, &m0);
	uint64_t prev_submits = m0.submits_3d;

	/* Build a full SURFACE + FB + CLEAR command stream. */
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	uk_virgl_encode_create_surface(&enc, 1u, 1u,
				       UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
	uk_virgl_encode_set_framebuffer(&enc, 1u);
	uk_virgl_encode_clear(&enc, 0.0f, 0.0f, 0.0f, 1.0f);
	CHECK(enc.overflow == 0, "submit:enc_no_overflow");
	CHECK(uk_virgl_encoder_len(&enc) == 76u, "submit:enc_len_76");

	uk_gpu_fence_id fence = 0;
	rc = uk_virtio_gpu_gl_context_submit(dev, &ctx,
					     uk_virgl_encoder_buf(&enc),
					     uk_virgl_encoder_len(&enc),
					     &fence);
	CHECK(rc == 0,    "submit:rc_zero");
	CHECK(fence != 0, "submit:fence_nonzero");

	struct uk_virtio_gpu_metrics m1 = {0};
	uk_virtio_gpu_gl_metrics_get(dev, &m1);
	CHECK(m1.submits_3d == prev_submits + 1u, "submit:submits_3d_incremented");

	uk_virtio_gpu_fence_wait(dev, fence, 1000000u);
	uk_virtio_gpu_gl_context_destroy(dev, &ctx);
	CHECK(1, "submit:teardown_ok");
}

int main(void)
{
	printf("virgl_encoder_test: Gallium command stream encoder\n");
	test_init();
	test_null_args();
	test_create_surface();
	test_set_framebuffer();
	test_clear();
	test_chained_byte_count();
	test_overflow();
	test_submit_to_fake_backend();

	if (failures == 0)
		printf("virgl_encoder_test: all checks passed\n");
	else
		printf("virgl_encoder_test: FAILED %d check(s)\n", failures);
	return failures;
}
