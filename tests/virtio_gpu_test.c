#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <uk/virtio_gpu.h>

#define FAIL_IF(cond, code) do { if (cond) return (code); } while (0)

int main(void)
{
	struct uk_virtio_gpu_dev *dev = 0;
	struct uk_virtio_gpu_caps caps;
	struct uk_virtio_gpu_capset_info cap;
	struct uk_virtio_gpu_context ctx = {0};
	struct uk_virtio_gpu_blob blob = {0};
	struct uk_virtio_gpu_metrics metrics;
	struct uk_virtio_gpu_apir_msg apir_msg, apir_decoded;
	struct uk_virtio_gpu_apir_handshake apir_reply;
	struct uk_virtio_gpu_apir_status apir_status;
	struct uk_virtio_gpu_resource_3d desc = {
		.target = 2, .format = 1, .bind = 0x40, .width = 32,
		.height = 32, .depth = 1, .array_size = 1,
		.last_level = 0, .nr_samples = 1, .flags = 0,
	};
	struct uk_virtio_gpu_transfer_3d xfer = {
		.box = { .x = 0, .y = 0, .z = 0, .w = 32, .h = 32, .d = 1 },
		.offset = 0, .level = 0, .stride = 128, .layer_stride = 4096,
	};
	struct uk_gpu_rect rect = {0, 0, 64, 64};
	struct uk_dma_sg sg = { .paddr_or_iova = 0x1000, .len = 4096 };
	uk_gpu_res_id res2d = 0, res3d = 0;
	uk_gpu_fence_id fence = 0;
	uint8_t uuid[16];
	uint8_t edid[128];
	char cap_payload[256];
	uint8_t cmd_stream[16] = {0x76, 0x69, 0x72, 0x67};
	uint8_t apir_buf[32];
	uint8_t apir_reply_buf[8] = {1, 0, 0, 0, 2, 0, 0, 0};
	size_t actual = 0;

	FAIL_IF(uk_virtio_gpu_probe(&dev) != 0, 1);
	FAIL_IF(uk_virtio_gpu_get_display_info(dev) != 0, 2);
	FAIL_IF(uk_virtio_gpu_gl_metrics_reset(dev) != 0, 44);
	FAIL_IF(uk_virtio_gpu_gl_caps_get(dev, &caps) != 0, 3);
	FAIL_IF(!caps.has_virgl || !caps.has_resource_blob || !caps.has_context_init, 4);
	FAIL_IF(caps.num_capsets < 3, 5);
	FAIL_IF(uk_virtio_gpu_gl_capset_info_get(dev, 0, &cap) != 0, 6);
	FAIL_IF(cap.id != UK_VIRTIO_GPU_CAPSET_VIRGL, 7);
	FAIL_IF(strcmp(uk_virtio_gpu_gl_capset_name(cap.id), "virgl") != 0, 8);
	FAIL_IF(uk_virtio_gpu_gl_capset_get(dev, cap.id, 1, cap_payload, sizeof(cap_payload), &actual) != 0, 9);
	FAIL_IF(actual == 0 || strstr(cap_payload, "virgl") == NULL, 10);
	memset(apir_buf, 0, sizeof(apir_buf));
	apir_msg = (struct uk_virtio_gpu_apir_msg) {
		.command_type = UK_VIRTIO_GPU_APIR_COMMAND_FORWARD,
		.flags = 7,
		.reply_resource_id = 99,
	};
	FAIL_IF(uk_virtio_gpu_apir_encode_header(apir_buf, sizeof(apir_buf), &apir_msg, &actual) != 0, 45);
	FAIL_IF(actual != 12, 46);
	FAIL_IF(uk_virtio_gpu_apir_decode_header(apir_buf, actual, &apir_decoded, NULL) != 0, 47);
	FAIL_IF(apir_decoded.command_type != apir_msg.command_type || apir_decoded.flags != 7 || apir_decoded.reply_resource_id != 99, 48);
	FAIL_IF(uk_virtio_gpu_apir_encode_handshake(apir_buf, sizeof(apir_buf), 123, &actual) != 0, 49);
	FAIL_IF(actual != 20, 50);
	FAIL_IF(uk_virtio_gpu_apir_decode_handshake_reply(apir_reply_buf, sizeof(apir_reply_buf), UK_VIRTIO_GPU_APIR_HANDSHAKE_MAGIC, &apir_reply) != 0, 51);
	FAIL_IF(apir_reply.host_major != 1 || apir_reply.host_minor != 2, 52);
	FAIL_IF(uk_virtio_gpu_apir_handshake(dev, &ctx, &blob, &apir_reply, &apir_status) != -ENOTSUP, 53);

	FAIL_IF(uk_virtio_gpu_gl_get_edid(dev, 0, edid, sizeof(edid), &actual) != 0, 11);
	FAIL_IF(actual != sizeof(edid) || edid[1] != 0xff, 12);

	FAIL_IF(uk_virtio_gpu_resource_create_2d(dev, 64, 64, 1, &res2d) != 0 || !res2d, 13);
	FAIL_IF(uk_virtio_gpu_resource_attach_backing(dev, res2d, &sg, 1) != 0, 14);
	FAIL_IF(uk_virtio_gpu_gl_set_scanout(dev, 0, res2d, &rect) != 0, 15);
	FAIL_IF(uk_virtio_gpu_transfer_to_host_2d(dev, res2d, &rect, &fence) != 0, 16);
	FAIL_IF(uk_virtio_gpu_fence_wait(dev, fence, 1000000) != 0, 17);
	FAIL_IF(uk_virtio_gpu_resource_flush(dev, res2d, &rect, &fence) != 0, 18);
	FAIL_IF(uk_virtio_gpu_fence_wait(dev, fence, 1000000) != 0, 19);

	/* P1.1: the coalesced helper issues both commands with a single fence.
	 * After the call only ONE additional fence_wait should be required. */
	{
		uint64_t baseline_waits;
		struct uk_virtio_gpu_metrics m0;
		FAIL_IF(uk_virtio_gpu_gl_metrics_get(dev, &m0) != 0, 200);
		baseline_waits = m0.fence_waits;
		fence = 0;
		FAIL_IF(uk_virtio_gpu_transfer_and_flush_2d(dev, res2d, &rect, &fence) != 0, 201);
		FAIL_IF(uk_virtio_gpu_fence_wait(dev, fence, 1000000) != 0, 202);
		FAIL_IF(uk_virtio_gpu_gl_metrics_get(dev, &m0) != 0, 203);
		FAIL_IF(m0.fence_waits != baseline_waits + 1u, 204);
	}
	FAIL_IF(uk_virtio_gpu_gl_resource_assign_uuid(dev, res2d, uuid) != 0, 20);
	FAIL_IF(uuid[0] == 0, 21);
	FAIL_IF(uk_virtio_gpu_gl_resource_detach_backing(dev, res2d) != 0, 22);
	FAIL_IF(uk_virtio_gpu_gl_resource_unref(dev, res2d) != 0, 23);

	FAIL_IF(uk_virtio_gpu_gl_resource_create_3d(dev, &desc, &res3d) != 0 || !res3d, 24);
	{
		struct uk_virtio_gpu_resource_3d wide_desc = desc;
		uk_gpu_res_id wide_res = 0;
		wide_desc.width = 0x40000000u;
		wide_desc.height = 1;
		wide_desc.depth = 1;
		FAIL_IF(uk_virtio_gpu_gl_resource_create_3d(dev, &wide_desc, &wide_res) != 0, 56);
		if (wide_res) {
			struct uk_virtio_gpu_transfer_3d wide_xfer = xfer;
			wide_xfer.box.w = wide_desc.width;
			wide_xfer.box.h = 1;
			wide_xfer.stride = 1;
			FAIL_IF(uk_virtio_gpu_gl_transfer_to_host_3d(dev, wide_res, &wide_xfer, &fence) != -EINVAL, 57);
			FAIL_IF(uk_virtio_gpu_gl_resource_unref(dev, wide_res) != 0, 58);
		}
	}
	FAIL_IF(uk_virtio_gpu_gl_context_create(dev, UK_VIRTIO_GPU_CAPSET_VIRGL, "full-api-test", &ctx) != 0, 25);
	FAIL_IF(uk_virtio_gpu_gl_context_attach_resource(dev, &ctx, res3d) != 0, 26);
	{
		struct uk_virtio_gpu_transfer_3d bad_xfer = xfer;
		bad_xfer.box.w = 4096;
		FAIL_IF(uk_virtio_gpu_gl_transfer_to_host_3d(dev, res3d, &bad_xfer, &fence) != -EINVAL, 54);
	}
	FAIL_IF(uk_virtio_gpu_gl_transfer_to_host_3d(dev, res3d, &xfer, &fence) != 0, 27);
	FAIL_IF(uk_virtio_gpu_fence_wait(dev, fence, 1000000) != 0, 28);
	FAIL_IF(uk_virtio_gpu_gl_transfer_from_host_3d(dev, res3d, &xfer, &fence) != 0, 29);
	FAIL_IF(uk_virtio_gpu_fence_wait(dev, fence, 1000000) != 0, 30);
	FAIL_IF(uk_virtio_gpu_gl_context_submit(dev, &ctx, cmd_stream, sizeof(cmd_stream), &fence) != 0, 31);
	FAIL_IF(uk_virtio_gpu_fence_wait(dev, fence, 1000000) != 0, 32);
	FAIL_IF(uk_virtio_gpu_gl_context_detach_resource(dev, &ctx, res3d) != 0, 33);
	FAIL_IF(uk_virtio_gpu_gl_context_destroy(dev, &ctx) != 0, 34);
	FAIL_IF(uk_virtio_gpu_gl_resource_unref(dev, res3d) != 0, 35);

	FAIL_IF(uk_virtio_gpu_gl_blob_create(dev, UINT64_MAX, UK_VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST, UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE, 0xabc, &blob) != -EOVERFLOW, 55);
	FAIL_IF(uk_virtio_gpu_gl_blob_create(dev, 4096, UK_VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST, UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE, 0xabc, &blob) != 0, 36);
	FAIL_IF(uk_virtio_gpu_gl_blob_map(dev, &blob) != 0 || !blob.mapped_addr, 37);
	memset(blob.mapped_addr, 0xa5, (size_t)blob.mapped_size);
	FAIL_IF(uk_virtio_gpu_gl_blob_destroy(dev, &blob) != -EINVAL, 38);
	FAIL_IF(uk_virtio_gpu_gl_blob_unmap(dev, &blob) != 0, 39);
	FAIL_IF(uk_virtio_gpu_gl_blob_destroy(dev, &blob) != 0, 40);
	FAIL_IF(uk_virtio_gpu_gl_context_create(dev, 9999, "bad", &ctx) != -EINVAL, 41);
	FAIL_IF(uk_virtio_gpu_gl_metrics_get(dev, &metrics) != 0, 42);
	FAIL_IF(metrics.submits_3d != 1 || metrics.blobs_created != 1 || metrics.fence_waits < 4, 43);

	printf("virtio_gpu_full_api_test passed capsets=%u fences=%llu submits_3d=%llu blobs=%llu bytes_to_host=%llu bytes_from_host=%llu\n",
	       caps.num_capsets,
	       (unsigned long long)metrics.fences_issued,
	       (unsigned long long)metrics.submits_3d,
	       (unsigned long long)metrics.blobs_created,
	       (unsigned long long)metrics.bytes_to_host,
	       (unsigned long long)metrics.bytes_from_host);
	return 0;
}
