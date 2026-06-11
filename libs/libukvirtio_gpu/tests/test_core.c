/* SPDX-License-Identifier: BSD-3-Clause */
#include <string.h>

#include <uk/test.h>
#include <uk/virtio_gpu.h>

UK_TESTCASE(libukvirtio_gpu, capset_name_lookup)
{
	UK_TEST_EXPECT_ZERO(strcmp(uk_virtio_gpu_capset_name(UK_VIRTIO_GPU_CAPSET_VIRGL),
				   "virgl"));
	UK_TEST_EXPECT_ZERO(strcmp(uk_virtio_gpu_capset_name(UK_VIRTIO_GPU_CAPSET_VENUS),
				   "venus"));
	UK_TEST_EXPECT_ZERO(strcmp(uk_virtio_gpu_capset_name(0xffffffffu), "unknown"));
}

UK_TESTCASE(libukvirtio_gpu, apir_header_round_trip)
{
	struct uk_virtio_gpu_apir_msg msg = {
		.command_type = UK_VIRTIO_GPU_APIR_COMMAND_FORWARD,
		.flags = 3u,
		.reply_resource_id = 17u,
	};
	struct uk_virtio_gpu_apir_msg decoded = { 0 };
	struct uk_virtio_gpu_apir_handshake reply = { 0 };
	uint8_t hdr[32];
	uint8_t reply_buf[8] = { 1, 0, 0, 0, 2, 0, 0, 0 };
	size_t used = 0;

	UK_TEST_EXPECT_ZERO(uk_virtio_gpu_apir_encode_header(hdr, sizeof(hdr), &msg, &used));
	UK_TEST_EXPECT_SNUM_EQ((long)used, 12L);
	UK_TEST_EXPECT_ZERO(uk_virtio_gpu_apir_decode_header(hdr, used, &decoded, NULL));
	UK_TEST_EXPECT_SNUM_EQ((long)decoded.command_type, (long)msg.command_type);
	UK_TEST_EXPECT_SNUM_EQ((long)decoded.flags, (long)msg.flags);
	UK_TEST_EXPECT_SNUM_EQ((long)decoded.reply_resource_id,
			       (long)msg.reply_resource_id);
	UK_TEST_EXPECT_ZERO(uk_virtio_gpu_apir_decode_handshake_reply(
		reply_buf, sizeof(reply_buf), UK_VIRTIO_GPU_APIR_HANDSHAKE_MAGIC, &reply));
	UK_TEST_EXPECT_SNUM_EQ((long)reply.host_major, 1L);
	UK_TEST_EXPECT_SNUM_EQ((long)reply.host_minor, 2L);
}

uk_testsuite_register(libukvirtio_gpu, NULL);
