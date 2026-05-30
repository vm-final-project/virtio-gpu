/*
 * libukvenus — Venus capset query, context management, ring-buffer protocol,
 * and SUBMIT_3D transport.
 *
 * Bootstraps the Venus Vulkan-over-VirtIO-GPU path:
 *   1. GET_CAPSET (capset_id=4) → decode wire-format capabilities
 *   2. CTX_CREATE with context_init=4 → Venus rendering context
 *   3. uk_venus_ring_register → vkCreateRingMESA → Mesa-compatible ring
 *   4. uk_venus_ring_cmd_write/flush → circular buffer + vkNotifyRingMESA
 *   5. uk_venus_ring_cmd_wait → poll shared head for command completion
 *
 * Ring protocol reference:
 *   mesa/src/virtio/vulkan/vn_ring.c (vn_ring_get_layout, vn_ring_write_buffer)
 *   mesa/src/virtio/venus-protocol/vn_protocol_driver_transport.h (vkCreateRingMESA)
 *   virglrenderer/src/venus/vkr_ring.c (host side consumer)
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uk/venus.h>

/* Minimum capset data to consider Venus available. */
#define VENUS_CAPS_MIN_SIZE 8u

/* Max raw capset payload (matches UKVGPU_MAX_CAPSET_PAYLOAD from proto header). */
#define VENUS_CAPSET_BUF_MAX 4096u

int uk_venus_capset_get(struct uk_virtio_gpu_dev *dev,
			struct uk_venus_caps *caps)
{
	struct uk_virtio_gpu_caps  dev_caps;
	struct uk_virtio_gpu_capset_info info;
	uint8_t  raw[VENUS_CAPSET_BUF_MAX];
	size_t   actual = 0;
	uint32_t i;
	int rc;

	if (!dev)
		return -EINVAL;

	rc = uk_virtio_gpu_gl_caps_get(dev, &dev_caps);
	if (rc)
		return rc;

	/* Scan capset list for Venus (id=4). */
	for (i = 0; i < dev_caps.num_capsets; i++) {
		rc = uk_virtio_gpu_gl_capset_info_get(dev, i, &info);
		if (rc)
			return rc;
		if (info.id == UK_VENUS_CAPSET_ID)
			goto found;
	}
	return -ENOTSUP;

found:
	if (!info.max_size || info.max_size < VENUS_CAPS_MIN_SIZE)
		return -ENOTSUP;

	memset(raw, 0, sizeof(raw));
	size_t req_len = info.max_size < sizeof(raw) ? info.max_size : sizeof(raw);
	rc = uk_virtio_gpu_gl_capset_get(dev, UK_VENUS_CAPSET_ID,
					 info.max_version, raw, req_len,
					 &actual);
	if (rc)
		return rc;

	if (caps) {
		memset(caps, 0, sizeof(*caps));
		/*
		 * Venus capset layout (little-endian uint32_t fields):
		 *   [0] wire_format_version
		 *   [1] vk_xml_version
		 *   [2] vk_ext_command_serialization_spec_version
		 *   [3] vk_mesa_venus_protocol_spec_version
		 *   [4] supports_blob_id_0  (when max_size >= 20)
		 */
		const uint32_t *f = (const uint32_t *)raw;
		caps->wire_format_version = actual >= 4  ? f[0] : 0;
		caps->vk_xml_version      = actual >= 8  ? f[1] : 0;
		caps->vk_ext_command_serialization_spec_version
					  = actual >= 12 ? f[2] : 0;
		caps->vk_mesa_venus_protocol_spec_version
					  = actual >= 16 ? f[3] : 0;
		caps->supports_blob_id_0  = actual >= 20 ? f[4] : 0;
	}
	return 0;
}

const char *uk_venus_probe(struct uk_virtio_gpu_dev *dev)
{
	struct uk_venus_caps caps;
	int rc;

	if (!dev)
		return "blocked:no-device";

	rc = uk_venus_capset_get(dev, &caps);
	if (rc == -ENOTSUP)
		return "blocked:capset-not-advertised";
	if (rc)
		return "blocked:capset-query-failed";

	if (caps.wire_format_version == 0)
		return "blocked:invalid-wire-format-version";

	return "pass";
}

int uk_venus_context_create(struct uk_virtio_gpu_dev *dev, uint32_t *ctx_id_out)
{
	struct uk_virtio_gpu_context ctx = { 0 };
	int rc;

	if (!dev)
		return -EINVAL;

	rc = uk_virtio_gpu_gl_context_create(dev, UK_VENUS_CAPSET_ID,
					     "unikraft-venus", &ctx);
	if (rc)
		return rc;

	if (ctx_id_out)
		*ctx_id_out = ctx.id;
	return 0;
}

int uk_venus_submit(struct uk_virtio_gpu_dev *dev,
		    const struct uk_virtio_gpu_context *ctx,
		    const struct uk_venus_encoder *enc)
{
	if (!dev || !ctx || !enc)
		return -EINVAL;
	if (enc->overflow)
		return -EOVERFLOW;
	if (!enc->pos)
		return -EINVAL;

	uk_gpu_fence_id fence;
	return uk_virtio_gpu_gl_context_submit(dev, ctx,
					       enc->buf, enc->pos, &fence);
}
const char *uk_venus_ring_status(struct uk_virtio_gpu_dev *dev)
{
	struct uk_virtio_gpu_caps caps;
	int rc;

	if (!dev)
		return "blocked:no-device";
	rc = uk_virtio_gpu_gl_caps_get(dev, &caps);
	if (rc)
		return "blocked:caps-query-failed";
	if (!caps.has_context_init)
		return "blocked:context-init-missing";
	if (!caps.has_resource_blob)
		return "blocked:resource-blob-missing";
	if (!caps.has_host_visible)
		return "blocked:host-visible-missing";
	return "pass";
}

int uk_venus_ring_create(struct uk_virtio_gpu_dev *dev,
			 struct uk_venus_ring *ring,
			 size_t size, uint64_t blob_id)
{
	struct uk_virtio_gpu_caps caps;
	int rc;

	if (!dev || !ring)
		return -EINVAL;
	if (!size)
		return -EINVAL;
	if (ring->ready || ring->ctx.created || ring->blob.created)
		return -EINVAL;

	rc = uk_virtio_gpu_gl_caps_get(dev, &caps);
	if (rc)
		return rc;
	if (!caps.has_context_init || !caps.has_resource_blob || !caps.has_host_visible)
		return -ENOTSUP;

	memset(ring, 0, sizeof(*ring));
	rc = uk_virtio_gpu_gl_context_create(dev, UK_VENUS_CAPSET_ID,
					     "unikraft-venus-ring", &ring->ctx);
	if (rc) {
		printf("uk-venus: ring create stage=context rc=%d\n", rc);
		return rc;
	}

	rc = uk_virtio_gpu_gl_blob_create_with_ctx(dev, ring->ctx.id,
					  (uint64_t)size,
					  /*
					   * QEMU/virgl's Venus shmem path follows
					   * Mesa's virtgpu_init_shmem_blob_mem():
					   * HOST3D + MAPPABLE + blob_id 0 allocates
					   * host shmem.  HOST3D_GUEST requires guest
					   * backing entries and fails closed here.
					   */
					  UK_VIRTIO_GPU_BLOB_MEM_HOST3D,
					  UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE,
					  blob_id,
					  &ring->blob);
	if (rc) {
		printf("uk-venus: ring create stage=blob_create rc=%d mem=%u flags=%u blob_id=%llu size=%llu\n",
			   rc, UK_VIRTIO_GPU_BLOB_MEM_HOST3D,
			   UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE,
			   (unsigned long long)blob_id,
			   (unsigned long long)size);
		goto err_ctx;
	}

	rc = uk_virtio_gpu_gl_blob_map(dev, &ring->blob);
	if (rc) {
		printf("uk-venus: ring create stage=blob_map rc=%d res=%u off=%llu\n",
			   rc, ring->blob.resource_id,
			   (unsigned long long)ring->blob.host_visible_offset);
		goto err_blob;
	}
	if (!ring->blob.mapped_addr || ring->blob.mapped_size < (uint64_t)size) {
		rc = -ENOTSUP;
		printf("uk-venus: ring create stage=blob_map_empty mapped=%p size=%llu need=%llu\n",
			   ring->blob.mapped_addr,
			   (unsigned long long)ring->blob.mapped_size,
			   (unsigned long long)size);
		goto err_unmap;
	}

	rc = uk_virtio_gpu_gl_context_attach_resource(dev, &ring->ctx,
					       ring->blob.resource_id);
	if (rc) {
		printf("uk-venus: ring create stage=ctx_attach rc=%d res=%u ctx=%u\n",
			   rc, ring->blob.resource_id, ring->ctx.id);
		goto err_unmap;
	}

	ring->base = (uint8_t *)ring->blob.mapped_addr;
	ring->size = size;
	ring->write_pos = 0;
	ring->bytes_written = 0;
	ring->commands_submitted = 0;
	ring->ready = 1;
	return 0;

err_unmap:
	(void)uk_virtio_gpu_gl_blob_unmap(dev, &ring->blob);
err_blob:
	(void)uk_virtio_gpu_gl_blob_destroy(dev, &ring->blob);
err_ctx:
	(void)uk_virtio_gpu_gl_context_destroy(dev, &ring->ctx);
	memset(ring, 0, sizeof(*ring));
	return rc;
}

void uk_venus_ring_destroy(struct uk_virtio_gpu_dev *dev,
			   struct uk_venus_ring *ring)
{
	if (!dev || !ring)
		return;
	if (ring->ctx.created && ring->blob.created)
		(void)uk_virtio_gpu_gl_context_detach_resource(dev, &ring->ctx,
							ring->blob.resource_id);
	if (ring->blob.mapped)
		(void)uk_virtio_gpu_gl_blob_unmap(dev, &ring->blob);
	if (ring->blob.created)
		(void)uk_virtio_gpu_gl_blob_destroy(dev, &ring->blob);
	if (ring->ctx.created)
		(void)uk_virtio_gpu_gl_context_destroy(dev, &ring->ctx);
	memset(ring, 0, sizeof(*ring));
}

int uk_venus_ring_write(struct uk_venus_ring *ring, const void *data,
			size_t len, size_t *offset_out)
{
	if (!ring || !ring->ready || !ring->base)
		return -EINVAL;
	if (!data || !len)
		return -EINVAL;
	if (len > ring->size || ring->write_pos > ring->size ||
	    len > ring->size - ring->write_pos)
		return -ENOSPC;
	if (offset_out)
		*offset_out = ring->write_pos;
	memcpy(ring->base + ring->write_pos, data, len);
	ring->write_pos += len;
	ring->bytes_written += (uint64_t)len;
	return 0;
}

int uk_venus_ring_submit(struct uk_virtio_gpu_dev *dev,
			 struct uk_venus_ring *ring,
			 const struct uk_venus_encoder *enc,
			 uk_gpu_fence_id *fence_out)
{
	size_t off = 0;
	uk_gpu_fence_id fence = 0;
	int rc;

	if (!dev || !ring || !enc)
		return -EINVAL;
	if (!ring->ready || !ring->ctx.created)
		return -EINVAL;
	if (enc->overflow)
		return -EOVERFLOW;
	if (!enc->buf || !enc->pos)
		return -EINVAL;

	rc = uk_venus_ring_write(ring, enc->buf, enc->pos, &off);
	if (rc)
		return rc;

	/* SUBMIT_3D remains the portable transport path; the ring proves the
	 * host-visible blob staging required by Venus without pretending to be a
	 * complete Mesa timeline implementation.
	 */
	rc = uk_virtio_gpu_gl_context_submit(dev, &ring->ctx, ring->base + off,
					       enc->pos, &fence);
	if (rc)
		return rc;
	ring->commands_submitted++;
	if (fence_out)
		*fence_out = fence;
	return 0;
}

/* -------------------------------------------------------------------------
 * Venus ring-buffer protocol (Mesa-compatible circular ring).
 * Reference: mesa/src/virtio/vulkan/vn_ring.c
 *            mesa/src/virtio/venus-protocol/vn_protocol_driver_transport.h
 * -------------------------------------------------------------------------
 */

/* Largest power of 2 that is <= n. */
static uint32_t venus_prev_pow2(uint32_t n)
{
	if (!n)
		return 0;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	return n - (n >> 1);
}

uint32_t uk_venus_ring_load_head(const struct uk_venus_ring *ring)
{
	if (!ring || !ring->base || !ring->protocol_ready)
		return 0;
	/* Acquire-load: host stores head with release ordering (Mesa vn_ring.c). */
	volatile const uint32_t *head_ptr =
		(volatile const uint32_t *)(ring->base + UK_VENUS_RING_HEAD_OFFSET);
	return *head_ptr;
}

/*
 * uk_venus_ring_register — initialize ring control fields and register the
 * ring with the host via vkCreateRingMESA submitted over SUBMIT_3D.
 *
 * Must be called after uk_venus_ring_create().  On success, ring->protocol_ready
 * is set and ring->buf_size/buf_mask/cur_tail are initialized.
 */
int uk_venus_ring_register(struct uk_virtio_gpu_dev *dev,
			   struct uk_venus_ring *ring,
			   uint64_t ring_id)
{
	uint8_t cmd_buf[256];
	struct uk_venus_encoder enc;
	uint32_t buf_size;
	uk_gpu_fence_id fence;
	int rc;

	if (!dev || !ring)
		return -EINVAL;
	if (!ring->ready || !ring->ctx.created || !ring->base)
		return -EINVAL;
	if (ring->protocol_ready)
		return -EINVAL;
	if (ring->size <= UK_VENUS_RING_CTRL_SIZE)
		return -EINVAL;

	/* Compute buf_size: largest power of 2 that fits in the data area. */
	buf_size = venus_prev_pow2((uint32_t)(ring->size - UK_VENUS_RING_CTRL_SIZE));
	if (!buf_size)
		return -EINVAL;

	/* Initialize shared control fields: head=0, tail=0, status=0. */
	volatile uint32_t *head_ptr =
		(volatile uint32_t *)(ring->base + UK_VENUS_RING_HEAD_OFFSET);
	volatile uint32_t *tail_ptr =
		(volatile uint32_t *)(ring->base + UK_VENUS_RING_TAIL_OFFSET);
	volatile uint32_t *status_ptr =
		(volatile uint32_t *)(ring->base + UK_VENUS_RING_STATUS_OFFSET);
	*head_ptr   = 0;
	*tail_ptr   = 0;
	*status_ptr = 0;

	/* Encode vkCreateRingMESA and submit via SUBMIT_3D bootstrap. */
	rc = uk_venus_encoder_init(&enc, cmd_buf, sizeof(cmd_buf));
	if (rc)
		return rc;

	uk_venus_encode_vkCreateRingMESA(&enc, ring_id,
					 ring->blob.resource_id,
					 (uint64_t)ring->size,
					 (uint64_t)buf_size);
	if (enc.overflow)
		return -ENOSPC;

	fence = 0;
	rc = uk_virtio_gpu_gl_context_submit(dev, &ring->ctx,
					     enc.buf, enc.pos, &fence);
	if (rc)
		return rc;

	ring->ring_id       = ring_id;
	ring->buf_size      = buf_size;
	ring->buf_mask      = buf_size - 1u;
	ring->cur_tail      = 0;
	ring->protocol_ready = 1;
	return 0;
}

/*
 * uk_venus_ring_unregister — send vkDestroyRingMESA and clear protocol state.
 */
int uk_venus_ring_unregister(struct uk_virtio_gpu_dev *dev,
			     struct uk_venus_ring *ring)
{
	uint8_t cmd_buf[64];
	struct uk_venus_encoder enc;
	uk_gpu_fence_id fence;
	int rc;

	if (!dev || !ring)
		return -EINVAL;
	if (!ring->protocol_ready)
		return 0; /* nothing to do */

	rc = uk_venus_encoder_init(&enc, cmd_buf, sizeof(cmd_buf));
	if (rc)
		return rc;

	uk_venus_encode_vkDestroyRingMESA(&enc, ring->ring_id);
	if (!enc.overflow) {
		fence = 0;
		(void)uk_virtio_gpu_gl_context_submit(dev, &ring->ctx,
						      enc.buf, enc.pos, &fence);
	}

	ring->protocol_ready = 0;
	ring->ring_id        = 0;
	ring->buf_size       = 0;
	ring->buf_mask       = 0;
	ring->cur_tail       = 0;
	return 0;
}

/*
 * uk_venus_ring_cmd_write — write bytes to the circular data buffer.
 *
 * Handles wrap-around exactly as Mesa's vn_ring_write_buffer():
 * if the data crosses the end of the buffer, the write is split into two
 * contiguous copies.  cur_tail is advanced but NOT stored to shared memory
 * yet; call uk_venus_ring_cmd_flush() to make the write visible to the host.
 *
 * Returns -ENOSPC if there is not enough space (host has not consumed enough).
 * For the basic bootstrap path the ring is large enough that this never fires.
 */
int uk_venus_ring_cmd_write(struct uk_venus_ring *ring,
			    const void *data, uint32_t len)
{
	uint8_t *buf;
	uint32_t offset;
	uint32_t avail;

	if (!ring || !ring->protocol_ready || !ring->base)
		return -EINVAL;
	if (!data || !len)
		return -EINVAL;

	/* Check available space: avail = buf_size - (cur_tail - head). */
	avail = ring->buf_size - (ring->cur_tail - uk_venus_ring_load_head(ring));
	if (len > avail)
		return -ENOSPC;

	buf    = ring->base + UK_VENUS_RING_BUFFER_OFFSET;
	offset = ring->cur_tail & ring->buf_mask;

	if (offset + len <= ring->buf_size) {
		memcpy(buf + offset, data, len);
	} else {
		uint32_t first = ring->buf_size - offset;
		memcpy(buf + offset, data, first);
		memcpy(buf, (const uint8_t *)data + first, len - first);
	}

	ring->cur_tail += len;
	return 0;
}

/*
 * uk_venus_ring_cmd_flush — publish the tail and notify the host.
 *
 * Stores cur_tail to the shared tail field with sequential-consistency
 * ordering (matching Mesa vn_ring_store_tail), then sends vkNotifyRingMESA
 * via SUBMIT_3D.  The host ring thread wakes on the notification and begins
 * consuming commands from the circular buffer.
 */
int uk_venus_ring_cmd_flush(struct uk_virtio_gpu_dev *dev,
			    struct uk_venus_ring *ring)
{
	volatile uint32_t *tail_ptr;
	uint8_t cmd_buf[64];
	struct uk_venus_encoder enc;
	uk_gpu_fence_id fence;
	int rc;

	if (!dev || !ring)
		return -EINVAL;
	if (!ring->protocol_ready || !ring->base)
		return -EINVAL;

	/* Store tail (seq_cst — must be visible before the notify). */
	tail_ptr  = (volatile uint32_t *)(ring->base + UK_VENUS_RING_TAIL_OFFSET);
	*tail_ptr = ring->cur_tail;

	/* Send vkNotifyRingMESA via SUBMIT_3D. */
	rc = uk_venus_encoder_init(&enc, cmd_buf, sizeof(cmd_buf));
	if (rc)
		return rc;

	uk_venus_encode_vkNotifyRingMESA(&enc, ring->ring_id,
					 ring->cur_tail /* seqno */);
	if (enc.overflow)
		return -ENOSPC;

	fence = 0;
	return uk_virtio_gpu_gl_context_submit(dev, &ring->ctx,
					       enc.buf, enc.pos, &fence);
}

/*
 * uk_venus_ring_cmd_wait — spin-poll the shared head until the host has
 * consumed all written commands (head == cur_tail).
 *
 * timeout_iters: maximum polling iterations before returning -ETIMEDOUT.
 *   0 = check once only; ~0u = spin indefinitely (not recommended in kernel).
 *
 * Returns 0 when head == cur_tail, -ETIMEDOUT if the loop expires.
 *
 * The host stores head with memory_order_release (Mesa vn_ring_load_head);
 * reading with a volatile pointer provides the acquire side on x86.
 */
int uk_venus_ring_cmd_wait(struct uk_venus_ring *ring, uint32_t timeout_iters)
{
	volatile const uint32_t *head_ptr;
	uint32_t iters = 0;

	if (!ring || !ring->protocol_ready || !ring->base)
		return -EINVAL;

	head_ptr = (volatile const uint32_t *)(ring->base + UK_VENUS_RING_HEAD_OFFSET);

	do {
		if (*head_ptr == ring->cur_tail)
			return 0;
		if (timeout_iters != (uint32_t)~0u && iters >= timeout_iters)
			return -ETIMEDOUT;
		iters++;
	} while (1);
}
