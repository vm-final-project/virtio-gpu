/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include <uk/virgl_encoder.h>
#include <uk/virtio_gpu.h>

#include "test_utils.h"

static uint32_t hdr_cmd(uint32_t w)  { return w & 0xffu; }
static uint32_t hdr_obj(uint32_t w)  { return (w >> 8) & 0xffu; }
static uint32_t hdr_len(uint32_t w)  { return w >> 16; }

static float u2f(uint32_t u)
{
	float f;
	memcpy(&f, &u, 4);
	return f;
}

static void test_init(struct test_state *t)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	TEST_CHECK(t, "init:pos_zero", enc.pos == 0);
	TEST_CHECK(t, "init:no_overflow", enc.overflow == 0);
	TEST_CHECK(t, "init:len_zero", uk_virgl_encoder_len(&enc) == 0);
}

static void test_null_args(struct test_state *t)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	TEST_CHECK(t, "null:create_surface", uk_virgl_encode_create_surface(NULL, 1, 1, 2, 0) != 0);
	TEST_CHECK(t, "null:set_framebuffer", uk_virgl_encode_set_framebuffer(NULL, 1) != 0);
	TEST_CHECK(t, "null:clear", uk_virgl_encode_clear(NULL, 0, 0, 0, 1) != 0);
	TEST_CHECK(t, "null:surf_handle_zero", uk_virgl_encode_create_surface(&enc, 0, 1, 2, 0) != 0);
	TEST_CHECK(t, "null:res_handle_zero", uk_virgl_encode_create_surface(&enc, 1, 0, 2, 0) != 0);
}

static void test_create_surface(struct test_state *t)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	int rc = uk_virgl_encode_create_surface(&enc, 42u, 7u, UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
	TEST_CHECK(t, "surface:rc_zero", rc == 0);
	TEST_CHECK(t, "surface:word_count", enc.pos == 6u);
	uint32_t hdr = enc.buf[0];
	TEST_CHECK(t, "surface:cmd_create_object", hdr_cmd(hdr) == 1u);
	TEST_CHECK(t, "surface:obj_surface", hdr_obj(hdr) == 8u);
	TEST_CHECK(t, "surface:len_five", hdr_len(hdr) == 5u);
	TEST_CHECK(t, "surface:handle", enc.buf[1] == 42u);
	TEST_CHECK(t, "surface:res_handle", enc.buf[2] == 7u);
	TEST_CHECK(t, "surface:format", enc.buf[3] == UK_VIRGL_FORMAT_B8G8R8X8_UNORM);
}

static void test_set_framebuffer(struct test_state *t)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	int rc = uk_virgl_encode_set_framebuffer(&enc, 42u);
	TEST_CHECK(t, "fb:rc_zero", rc == 0);
	TEST_CHECK(t, "fb:word_count", enc.pos == 4u);
	uint32_t hdr = enc.buf[0];
	TEST_CHECK(t, "fb:cmd_set_framebuffer", hdr_cmd(hdr) == 5u);
	TEST_CHECK(t, "fb:len_three", hdr_len(hdr) == 3u);
	TEST_CHECK(t, "fb:nr_cbufs_one", enc.buf[1] == 1u);
	TEST_CHECK(t, "fb:cbuf_handle", enc.buf[3] == 42u);
}

static void test_clear(struct test_state *t)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	int rc = uk_virgl_encode_clear(&enc, 0.25f, 0.5f, 0.75f, 1.0f);
	TEST_CHECK(t, "clear:rc_zero", rc == 0);
	TEST_CHECK(t, "clear:word_count", enc.pos == 9u);
	uint32_t hdr = enc.buf[0];
	TEST_CHECK(t, "clear:cmd_clear", hdr_cmd(hdr) == 7u);
	TEST_CHECK(t, "clear:buffers_color0", enc.buf[1] == UK_VIRGL_CLEAR_COLOR0);
	TEST_CHECK(t, "clear:r", u2f(enc.buf[2]) == 0.25f);
	TEST_CHECK(t, "clear:g", u2f(enc.buf[3]) == 0.50f);
	TEST_CHECK(t, "clear:b", u2f(enc.buf[4]) == 0.75f);
	TEST_CHECK(t, "clear:a", u2f(enc.buf[5]) == 1.0f);
}

static void test_chained(struct test_state *t)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	uk_virgl_encode_create_surface(&enc, 1u, 5u, UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
	uk_virgl_encode_set_framebuffer(&enc, 1u);
	uk_virgl_encode_clear(&enc, 0.2f, 0.4f, 0.6f, 1.0f);
	TEST_CHECK(t, "chain:word_count", enc.pos == 19u);
	TEST_CHECK(t, "chain:byte_len", uk_virgl_encoder_len(&enc) == 76u);
}

static void test_overflow(struct test_state *t)
{
	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	for (int i = 0; i < 6; i++) {
		uk_virgl_encode_create_surface(&enc, (uint32_t)(i + 1), (uint32_t)(i + 1), UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
	}
	TEST_CHECK(t, "overflow:flag_set", enc.overflow != 0);
}

static void test_submit(struct test_state *t)
{
	struct uk_virtio_gpu_dev *dev = test_device_init(t);
	if (!dev) return;

	struct uk_virtio_gpu_context ctx = test_context_create(t, dev, 1u, "virgl_enc");

	struct uk_virgl_encoder enc;
	uk_virgl_encoder_init(&enc);
	uk_virgl_encode_create_surface(&enc, 1u, 1u, UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
	uk_virgl_encode_set_framebuffer(&enc, 1u);
	uk_virgl_encode_clear(&enc, 0.0f, 0.0f, 0.0f, 1.0f);

	uk_gpu_fence_id fence = 0;
	int rc = uk_virtio_gpu_gl_context_submit(dev, &ctx, uk_virgl_encoder_buf(&enc), uk_virgl_encoder_len(&enc), &fence);
	TEST_CHECK(t, "submit:rc_zero", rc == 0);
	TEST_CHECK(t, "submit:fence_nonzero", fence != 0);

	uk_virtio_gpu_fence_wait(dev, fence, 1000000u);
	test_context_destroy(t, dev, &ctx);
	test_device_cleanup(dev);
}

int main(void)
{
	struct test_state t = { .suite = "virgl_encoder_test" };
	test_init(&t);
	test_null_args(&t);
	test_create_surface(&t);
	test_set_framebuffer(&t);
	test_clear(&t);
	test_chained(&t);
	test_overflow(&t);
	test_submit(&t);
	return test_finish(&t);
}
