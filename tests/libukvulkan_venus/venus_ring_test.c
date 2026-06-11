/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uk/venus.h>

#include "test_utils.h"

int main(void)
{
	struct test_state t = { .suite = "venus_ring_test" };
	struct uk_virtio_gpu_dev *dev = NULL;
	struct uk_venus_ring ring = { 0 };
	struct uk_venus_encoder enc;
	uint8_t enc_buf[256];
	volatile uint32_t *head_ptr;
	volatile uint32_t *tail_ptr;
	int rc;

	dev = test_device_init(&t);
	if (!dev)
		return test_finish(&t);

	TEST_CHECK(&t, "ring status pass", strcmp(uk_venus_ring_status(dev), "pass") == 0);
	TEST_CHECK(&t, "ring create rejects null dev", uk_venus_ring_create(NULL, &ring, 4096, 1) < 0);
	TEST_CHECK(&t, "ring create rejects zero size", uk_venus_ring_create(dev, &ring, 0, 1) < 0);
	TEST_CHECK(&t, "ring create works",
		   uk_venus_ring_create(dev, &ring, UK_VENUS_RING_BUFFER_OFFSET + 4096u, 0xBEEF0001ull) == 0);
	TEST_CHECK(&t, "ring ready", ring.ready && ring.base != NULL);

	TEST_CHECK(&t, "ring register",
		   uk_venus_ring_register(dev, &ring, 0xDEAD0001ull) == 0);
	TEST_CHECK(&t, "protocol ready", ring.protocol_ready);
	TEST_CHECK(&t, "buffer size", ring.buf_size == 4096u);
	TEST_CHECK(&t, "buffer mask", ring.buf_mask == 4095u);

	head_ptr = (volatile uint32_t *)(ring.base + UK_VENUS_RING_HEAD_OFFSET);
	tail_ptr = (volatile uint32_t *)(ring.base + UK_VENUS_RING_TAIL_OFFSET);
	TEST_CHECK(&t, "head zero", *head_ptr == 0);
	TEST_CHECK(&t, "tail zero", *tail_ptr == 0);

	TEST_CHECK(&t, "encoder init", uk_venus_encoder_init(&enc, enc_buf, sizeof(enc_buf)) == 0);
	uk_venus_encode_vkQueueSubmit_empty(&enc, 0x400000001ull, 0);
	TEST_CHECK(&t, "encoder produced bytes", uk_venus_encoder_size(&enc) > 0);
	TEST_CHECK(&t, "encoder no overflow", !uk_venus_encoder_overflow(&enc));

	rc = uk_venus_ring_cmd_write(&ring, enc_buf, (uint32_t)uk_venus_encoder_size(&enc));
	TEST_CHECK(&t, "ring write", rc == 0);
	TEST_CHECK(&t, "tail advanced", ring.cur_tail == uk_venus_encoder_size(&enc));
	TEST_CHECK(&t, "buffer payload copied",
		   memcmp(ring.base + UK_VENUS_RING_BUFFER_OFFSET,
			  enc_buf,
			  uk_venus_encoder_size(&enc)) == 0);

	TEST_CHECK(&t, "ring flush", uk_venus_ring_cmd_flush(dev, &ring) == 0);
	TEST_CHECK(&t, "shared tail stored", *tail_ptr == ring.cur_tail);
	*head_ptr = ring.cur_tail;
	TEST_CHECK(&t, "ring wait", uk_venus_ring_cmd_wait(&ring, 1u) == 0);

	TEST_CHECK(&t, "ring unregister", uk_venus_ring_unregister(dev, &ring) == 0);
	TEST_CHECK(&t, "protocol cleared", !ring.protocol_ready && ring.ring_id == 0);
	uk_venus_ring_destroy(dev, &ring);
	test_device_cleanup(dev);
	return test_finish(&t);
}
