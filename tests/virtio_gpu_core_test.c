/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <uk/alloc.h>
#include <uk/sglist.h>
#include <uk/virtio_gpu.h>

#include "test_harness.h"

#define CORE_WIDTH  64u
#define CORE_HEIGHT 64u
#define CORE_BPP    4u

static void fill_frame(uint32_t *pixels, uint32_t width, uint32_t height)
{
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++)
			pixels[y * width + x] = 0xff000000u | (x << 8) | y;
	}
}

int main(void)
{
	struct test_state t = { .suite = "virtio_gpu_core_test" };
	struct uk_alloc *alloc = uk_alloc_get_default();
	struct uk_virtio_gpu_dev *dev = NULL;
	struct uk_virtio_gpu_caps caps;
	struct uk_virtio_gpu_capset_info cap;
	struct uk_virtio_gpu_metrics metrics;
	struct uk_gpu_rect rect = { 0, 0, CORE_WIDTH, CORE_HEIGHT };
	struct uk_sglist sg;
	struct uk_sglist_seg seg[1];
	struct uk_virtio_gpu_apir_msg msg = {
		.command_type = UK_VIRTIO_GPU_APIR_COMMAND_FORWARD,
		.flags = 3,
		.reply_resource_id = 17,
	};
	struct uk_virtio_gpu_apir_msg decoded = { 0 };
	struct uk_virtio_gpu_apir_handshake reply = { 0 };
	size_t used = 0;
	size_t frame_len = (size_t)CORE_WIDTH * CORE_HEIGHT * CORE_BPP;
	void *frame = NULL;
	uk_gpu_res_id res = 0;
	uk_gpu_fence_id fence = 0;
	uint8_t edid[128];
	uint8_t uuid[16] = { 0 };
	uint8_t apir_hdr[32];
	uint8_t apir_reply_buf[8] = { 1, 0, 0, 0, 2, 0, 0, 0 };

	TEST_CHECK(&t, "alloc default available", alloc != NULL);
	TEST_CHECK(&t, "probe fake device", uk_virtio_gpu_probe(&dev) == 0 && dev != NULL);
	if (!dev)
		return test_finish(&t);

	TEST_CHECK(&t, "metrics reset", uk_virtio_gpu_gl_metrics_reset(dev) == 0);
	TEST_CHECK(&t, "caps get", uk_virtio_gpu_gl_caps_get(dev, &caps) == 0);
	TEST_CHECK(&t, "virgl supported", caps.has_virgl);
	TEST_CHECK(&t, "resource blob supported", caps.has_resource_blob);
	TEST_CHECK(&t, "context init supported", caps.has_context_init);
	TEST_CHECK(&t, "venus capset present", caps.num_capsets >= 3);
	TEST_CHECK(&t, "capset info get", uk_virtio_gpu_gl_capset_info_get(dev, 0, &cap) == 0);
	TEST_CHECK(&t, "capset name virgl", strcmp(uk_virtio_gpu_gl_capset_name(cap.id), "virgl") == 0);
	TEST_CHECK(&t, "edid get", uk_virtio_gpu_gl_get_edid(dev, 0, edid, sizeof(edid), &used) == 0);
	TEST_CHECK(&t, "edid length", used == sizeof(edid));
	TEST_CHECK(&t, "edid header", edid[0] == 0x00 && edid[1] == 0xff);

	TEST_CHECK(&t, "aligned frame alloc",
		   uk_posix_memalign(alloc, &frame, 4096, frame_len) == 0 && frame != NULL);
	if (!frame) {
		free(dev);
		return test_finish(&t);
	}
	TEST_CHECK(&t, "frame alignment", ((uintptr_t)frame % 4096u) == 0);

	uk_sglist_init(&sg, 1, seg);
	TEST_CHECK(&t, "sg append frame", uk_sglist_append(&sg, frame, frame_len) == 0);
	TEST_CHECK(&t, "sg single segment", sg.sg_nseg == 1);
	fill_frame((uint32_t *)frame, CORE_WIDTH, CORE_HEIGHT);

	TEST_CHECK(&t, "resource create 2d",
		   uk_virtio_gpu_resource_create_2d(dev, CORE_WIDTH, CORE_HEIGHT, 1, &res) == 0 && res != 0);
	TEST_CHECK(&t, "attach backing", uk_virtio_gpu_resource_attach_backing(dev, res, &sg) == 0);
	TEST_CHECK(&t, "transfer+flush",
		   uk_virtio_gpu_transfer_and_flush_2d(dev, res, &rect, &fence) == 0 && fence != 0);
	TEST_CHECK(&t, "wait fence", uk_virtio_gpu_fence_wait(dev, fence, 1000000u) == 0);
	TEST_CHECK(&t, "set scanout", uk_virtio_gpu_gl_set_scanout(dev, 0, res, &rect) == 0);
	TEST_CHECK(&t, "assign uuid", uk_virtio_gpu_gl_resource_assign_uuid(dev, res, uuid) == 0);
	TEST_CHECK(&t, "uuid non-zero", uuid[0] != 0);

	TEST_CHECK(&t, "metrics get", uk_virtio_gpu_gl_metrics_get(dev, &metrics) == 0);
	TEST_CHECK(&t, "resource counted", metrics.resources_created >= 1);
	TEST_CHECK(&t, "transfer counted", metrics.transfers_to_host >= 1);
	TEST_CHECK(&t, "flush counted", metrics.flushes >= 1);
	TEST_CHECK(&t, "fence counted", metrics.fence_waits >= 1);

	TEST_CHECK(&t, "apir encode header",
		   uk_virtio_gpu_apir_encode_header(apir_hdr, sizeof(apir_hdr), &msg, &used) == 0);
	TEST_CHECK(&t, "apir header size", used == 12);
	TEST_CHECK(&t, "apir decode header",
		   uk_virtio_gpu_apir_decode_header(apir_hdr, used, &decoded, NULL) == 0);
	TEST_CHECK(&t, "apir decode round-trip",
		   decoded.command_type == msg.command_type &&
		   decoded.flags == msg.flags &&
		   decoded.reply_resource_id == msg.reply_resource_id);
	TEST_CHECK(&t, "apir decode handshake reply",
		   uk_virtio_gpu_apir_decode_handshake_reply(apir_reply_buf,
							 sizeof(apir_reply_buf),
							 UK_VIRTIO_GPU_APIR_HANDSHAKE_MAGIC,
							 &reply) == 0);
	TEST_CHECK(&t, "apir reply version", reply.host_major == 1 && reply.host_minor == 2);

	TEST_CHECK(&t, "detach backing", uk_virtio_gpu_gl_resource_detach_backing(dev, res) == 0);
	TEST_CHECK(&t, "resource unref", uk_virtio_gpu_gl_resource_unref(dev, res) == 0);

	uk_free(alloc, frame);
	free(dev);
	return test_finish(&t);
}
