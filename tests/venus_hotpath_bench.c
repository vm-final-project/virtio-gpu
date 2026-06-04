/* tests/venus_hotpath_bench.c
 *
 * Deterministic A/B microbench justifying the Venus transport optimizations.
 * Runs an identical "inference step" command sequence (N vkCmd-equivalent
 * encodes per command buffer, repeated over S steps) through three transport
 * modes against the fake VirtIO-GPU backend, and reports the resulting
 * virtqueue SUBMIT_3D count (`submits_3d`) — the per-step host round-trips.
 *
 *   A) per-call SUBMIT_3D   : one SUBMIT_3D per command (pre-optimization)
 *   B) batched SUBMIT_3D    : one SUBMIT_3D per command buffer (P1.3 batch)
 *   C) ring stream          : commands written to the ring; one flush/step
 *
 * The win is purely transport-level and backend-independent, so the fake
 * backend's deterministic submit counter is a faithful proxy for the real
 * virtqueue-kick count on QEMU/Venus.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uk/venus.h>
#include <uk/virtio_gpu.h>

#define CMDS_PER_BUF   8u    /* bind+bind+push+dispatch+barrier+copy+... */
#define STEPS          64u   /* inference steps (token iterations) */
#define ENC_BUF        4096u

static uint64_t submits(struct uk_virtio_gpu_dev *dev)
{
	struct uk_virtio_gpu_metrics m;
	if (uk_virtio_gpu_gl_metrics_get(dev, &m) != 0)
		return 0;
	return m.submits_3d;
}

/* Encode one representative command-buffer worth of commands into enc. */
static void encode_cmdbuf(struct uk_venus_encoder *enc, uint64_t cb)
{
	uk_venus_encode_vkBeginCommandBuffer(enc, cb);
	uk_venus_encode_vkCmdBindPipeline(enc, cb, 0x40);
	uk_venus_encode_vkCmdBindDescriptorSets(enc, cb, 0x13, 0, 0, NULL);
	uint32_t pc[4] = { 1, 2, 3, 4 };
	uk_venus_encode_vkCmdPushConstants(enc, cb, 0x13, 0x20, 0, sizeof pc, pc);
	uk_venus_encode_vkCmdDispatch(enc, cb, 4, 4, 1);
	uk_venus_encode_vkCmdPipelineBarrier(enc, cb, 0x800, 0x800);
	uk_venus_encode_vkCmdCopyBuffer(enc, cb, 0x20, 0x30, 256);
	uk_venus_encode_vkEndCommandBuffer(enc, cb);
}

int main(int argc, char **argv)
{
	const char *json_path = (argc > 1) ? argv[1] : NULL;
	struct uk_virtio_gpu_dev *dev = NULL;
	uint32_t ctx_id = 0;
	uint8_t buf[ENC_BUF];
	struct uk_venus_encoder enc;
	uint64_t base, a, b, c;
	int fail = 0;

	if (uk_virtio_gpu_probe(&dev) != 0 || !dev) {
		printf("venus_hotpath_bench: FAIL probe\n");
		return 1;
	}
	if (uk_venus_context_create(dev, &ctx_id) != 0) {
		printf("venus_hotpath_bench: FAIL ctx\n");
		return 1;
	}
	struct uk_virtio_gpu_context ctx = { .id = ctx_id, .created = 1 };

	/* ── A) per-call SUBMIT_3D: one submit per command ──────────────── */
	base = submits(dev);
	for (uint32_t s = 0; s < STEPS; s++) {
		uint64_t cb = 0x1000 + s;
		/* Each command its own encode+submit (worst case, pre-batch). */
		for (uint32_t i = 0; i < CMDS_PER_BUF; i++) {
			uk_venus_encoder_init(&enc, buf, sizeof buf);
			uk_venus_encode_vkCmdDispatch(&enc, cb, 4, 4, 1);
			uk_venus_submit(dev, &ctx, &enc);
		}
		/* QueueSubmit + (old) WaitForFences each a submit too */
		uk_venus_encoder_init(&enc, buf, sizeof buf);
		uk_venus_encode_vkQueueSubmit(&enc, 0x77, 0, NULL, 0x55);
		uk_venus_submit(dev, &ctx, &enc);
		uk_venus_encoder_init(&enc, buf, sizeof buf);
		uk_venus_encode_vkWaitForFences(&enc, 0x03, 1, (uint64_t[]){0x55}, ~0ull);
		uk_venus_submit(dev, &ctx, &enc);
	}
	a = submits(dev) - base;

	/* ── B) batched SUBMIT_3D: one submit per command buffer + submit ─ */
	base = submits(dev);
	for (uint32_t s = 0; s < STEPS; s++) {
		uint64_t cb = 0x2000 + s;
		uk_venus_encoder_init(&enc, buf, sizeof buf);
		encode_cmdbuf(&enc, cb);            /* whole cmd buffer in one enc */
		uk_venus_submit(dev, &ctx, &enc);   /* 1 submit (EndCmdBuf flush) */
		uk_venus_encoder_init(&enc, buf, sizeof buf);
		uk_venus_encode_vkQueueSubmit(&enc, 0x77, 0, NULL, 0x55);
		uk_venus_submit(dev, &ctx, &enc);   /* 1 submit (QueueSubmit) */
		/* WaitForFences: opt #2 → poll completed_fence, NO submit */
	}
	b = submits(dev) - base;

	/* ── C) ring stream: commands streamed; one flush per step ───────── */
	base = submits(dev);
	{
		struct uk_venus_ring ring;
		memset(&ring, 0, sizeof ring);
		int rc = uk_venus_ring_create_on_ctx(dev, &ring, &ctx,
				UK_VENUS_RING_CTRL_SIZE + UK_VENUS_RING_DEFAULT_SIZE,
				UK_VENUS_RING_DEFAULT_BLOB_ID + 7);
		if (rc == 0)
			rc = uk_venus_ring_register(dev, &ring,
						    UK_VENUS_RING_DEFAULT_BLOB_ID + 7);
		if (rc != 0) {
			printf("venus_hotpath_bench: WARN ring unavailable rc=%d "
			       "(fake backend has no ring_thread); skipping C\n", rc);
			c = 0;
		} else {
			for (uint32_t s = 0; s < STEPS; s++) {
				uint64_t cb = 0x3000 + s;
				uk_venus_encoder_init(&enc, buf, sizeof buf);
				encode_cmdbuf(&enc, cb);
				uk_venus_ring_cmd_write(&ring, enc.buf, enc.pos);
				uk_venus_encoder_init(&enc, buf, sizeof buf);
				uk_venus_encode_vkQueueSubmit(&enc, 0x77, 0, NULL, 0x55);
				uk_venus_ring_cmd_write(&ring, enc.buf, enc.pos);
				uk_venus_ring_cmd_flush(dev, &ring); /* 1 submit/step (notify) */
			}
			c = submits(dev) - base;
			uk_venus_ring_destroy(dev, &ring);
		}
	}

	printf("venus_hotpath_bench: steps=%u cmds_per_buf=%u\n", STEPS, CMDS_PER_BUF);
	printf("  A per-call SUBMIT_3D : submits_3d=%llu  (%.2f /step)\n",
	       (unsigned long long)a, (double)a / STEPS);
	printf("  B batched SUBMIT_3D  : submits_3d=%llu  (%.2f /step)\n",
	       (unsigned long long)b, (double)b / STEPS);
	if (c)
		printf("  C ring stream        : submits_3d=%llu  (%.2f /step)\n",
		       (unsigned long long)c, (double)c / STEPS);

	/* Justification asserts: batch must beat per-call; ring (when available)
	 * must not exceed batch and removes the per-command round-trips. */
	if (!(b < a)) {
		printf("  FAIL: batch (%llu) did not reduce vs per-call (%llu)\n",
		       (unsigned long long)b, (unsigned long long)a);
		fail = 1;
	}
	if (c && !(c <= b)) {
		printf("  FAIL: ring (%llu) exceeded batch (%llu)\n",
		       (unsigned long long)c, (unsigned long long)b);
		fail = 1;
	}
	printf("venus_hotpath_bench: %s\n", fail ? "FAIL" : "PASS");

	if (json_path) {
		FILE *f = fopen(json_path, "w");
		if (f) {
			fprintf(f,
			    "{\n"
			    "  \"schema\": \"venus/hotpath-submit-ab.v1\",\n"
			    "  \"env\": \"native-fake-backend\",\n"
			    "  \"steps\": %u, \"cmds_per_buf\": %u,\n"
			    "  \"submits_per_step\": {\n"
			    "    \"A_per_call_submit3d\": %.2f,\n"
			    "    \"B_batched_submit3d\": %.2f,\n"
			    "    \"C_ring_stream\": %.2f\n"
			    "  },\n"
			    "  \"reduction\": { \"A_to_B\": %.2f, \"A_to_C\": %.2f },\n"
			    "  \"pass\": %s\n"
			    "}\n",
			    STEPS, CMDS_PER_BUF,
			    (double)a / STEPS, (double)b / STEPS,
			    c ? (double)c / STEPS : 0.0,
			    b ? (double)a / b : 0.0,
			    c ? (double)a / c : 0.0,
			    fail ? "false" : "true");
			fclose(f);
			printf("venus_hotpath_bench: wrote %s\n", json_path);
		}
	}
	return fail;
}
