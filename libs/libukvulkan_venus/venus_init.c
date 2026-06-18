/*
 * libukvulkan_venus — Venus capset query, context management, ring-buffer protocol,
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

int uk_venus_ring_create_on_ctx(struct uk_virtio_gpu_dev *dev,
				struct uk_venus_ring *ring,
				const struct uk_virtio_gpu_context *ctx,
				size_t size, uint64_t blob_id)
{
	struct uk_virtio_gpu_caps caps;
	int rc;

	if (!dev || !ring || !ctx)
		return -EINVAL;
	if (!size)
		return -EINVAL;
	if (ring->ready || ring->blob.created)
		return -EINVAL;

	rc = uk_virtio_gpu_gl_caps_get(dev, &caps);
	if (rc)
		return rc;
	if (!caps.has_context_init || !caps.has_resource_blob || !caps.has_host_visible)
		return -ENOTSUP;

	memset(ring, 0, sizeof(*ring));
	/* Borrow the caller's Venus context: the ring's commands must share the
	 * same host-side context as the Vulkan objects it drives. */
	ring->ctx = *ctx;
	ring->borrowed_ctx = 1;

	/* The ring buffer is a host-shmem blob. virglrenderer's Venus host-visible
	 * path (Mesa virtgpu_init_shmem_blob_mem) allocates host shmem only for
	 * HOST3D + MAPPABLE + blob_id 0; a non-zero blob_id is treated as an import
	 * of an existing Venus object and fails closed (EIO). The caller's blob_id
	 * is used as the ring_id at register time, not for the shmem allocation. */
	(void)blob_id;
	rc = uk_virtio_gpu_gl_blob_create_with_ctx(dev, ring->ctx.id,
					  (uint64_t)size,
					  UK_VIRTIO_GPU_BLOB_MEM_HOST3D,
					  UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE,
					  0 /* blob_id 0 = allocate host shmem */,
					  &ring->blob);
	if (rc) {
		printf("uk-venus: ring(on_ctx) blob_create rc=%d ctx=%u blob_id=%llu size=%llu\n",
		       rc, ring->ctx.id, (unsigned long long)blob_id,
		       (unsigned long long)size);
		memset(ring, 0, sizeof(*ring));
		return rc;
	}
	goto map_common;

map_common:
	rc = uk_virtio_gpu_gl_blob_map(dev, &ring->blob);
	if (rc)
		goto err_blob;
	if (!ring->blob.mapped_addr || ring->blob.mapped_size < (uint64_t)size) {
		rc = -ENOTSUP;
		goto err_unmap;
	}
	rc = uk_virtio_gpu_gl_context_attach_resource(dev, &ring->ctx,
						      ring->blob.resource_id);
	if (rc)
		goto err_unmap;
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
	memset(ring, 0, sizeof(*ring));
	return rc;
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
	/* Only destroy the context if we created it; borrowed contexts are owned
	 * by the caller (the dispatch layer's main g_ctx). */
	if (ring->ctx.created && !ring->borrowed_ctx)
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
		/* Pause hint on x86; elsewhere keep only the compiler barrier so
		 * the polling loop remains portable on host-native builds. */
#if defined(__i386__) || defined(__x86_64__)
		__asm__ volatile("pause" ::: "memory");
#else
		__asm__ volatile("" ::: "memory");
#endif
		iters++;
	} while (1);
}

/* -------------------------------------------------------------------------
 * Real Venus query round-trip (reply-read path).
 *
 * Submits [vkSetReplyCommandStreamMESA][vkGetPhysicalDeviceProperties] over
 * SUBMIT_3D into a host-visible reply blob, then reads the host-written
 * VkPhysicalDeviceProperties back. This is a real bidirectional Venus exchange
 * with the host virglrenderer Venus backend (vkr) — not a fabricated reply.
 *
 * Reply wire layout (host vn_encode_vkGetPhysicalDeviceProperties_reply ->
 * vn_encode_VkPhysicalDeviceProperties), packed:
 *   [u32 cmd_type=6][u64 ptr=1]
 *   [u32 apiVersion][u32 driverVersion][u32 vendorID][u32 deviceID]
 *   [u32 deviceType][u64 array_size=256][char deviceName[256]] ...
 * => deviceName begins at byte offset 40.
 * -------------------------------------------------------------------------
 */
#define UK_VENUS_REPLY_BLOB_SIZE 4096ull
#define UK_VENUS_PROPS_NAME_OFF  40u

static int uk_venus_extract_name(const uint8_t *buf, size_t len,
				 char *out, unsigned int cap)
{
	size_t i, start, best_start = 0, best_len = 0, run = 0;

	if (!buf || !out || cap < 2)
		return -1;

	/* Validate the reply header: cmd_type must be vkGetPhysicalDeviceProperties
	 * (6) and the output pointer present (1). This proves the host actually
	 * wrote a well-formed reply rather than leaving the zeroed blob. */
	if (len < UK_VENUS_PROPS_NAME_OFF + 2)
		return -1;
	{
		uint32_t cmd_type;
		uint64_t ptr;
		memcpy(&cmd_type, buf, sizeof(cmd_type));
		memcpy(&ptr, buf + 4, sizeof(ptr));
		if (cmd_type != (uint32_t)VN_CMD_vkGetPhysicalDeviceProperties || ptr != 1ull)
			return -1;
	}

	/* Prefer the spec offset; require a printable, letter-containing name. */
	{
		const char *n = (const char *)(buf + UK_VENUS_PROPS_NAME_OFF);
		size_t max = len - UK_VENUS_PROPS_NAME_OFF;
		size_t l = 0;
		int has_alpha = 0;
		while (l < max && l < 255 && n[l] >= 0x20 && n[l] < 0x7f) {
			if ((n[l] >= 'A' && n[l] <= 'Z') || (n[l] >= 'a' && n[l] <= 'z'))
				has_alpha = 1;
			l++;
		}
		if (l >= 2 && has_alpha) {
			if (l >= cap)
				l = cap - 1;
			memcpy(out, n, l);
			out[l] = '\0';
			return 0;
		}
	}

	/* Fallback: longest printable run containing a letter. */
	for (i = 0; i <= len; i++) {
		int printable = (i < len) && buf[i] >= 0x20 && buf[i] < 0x7f;
		if (printable) {
			if (run == 0)
				start = i;
			run++;
		} else {
			if (run > best_len) {
				int alpha = 0;
				for (size_t k = start; k < start + run; k++)
					if ((buf[k] | 0x20) >= 'a' && (buf[k] | 0x20) <= 'z')
						alpha = 1;
				if (alpha) { best_len = run; best_start = start; }
			}
			run = 0;
		}
	}
	if (best_len >= 2) {
		if (best_len >= cap)
			best_len = cap - 1;
		memcpy(out, buf + best_start, best_len);
		out[best_len] = '\0';
		return 0;
	}
	return -1;
}

/*
 * uk_venus_query_roundtrip — generic Venus reply round-trip on an existing
 * context. The caller pre-encodes ONE reply-bearing query command (flagged
 * VK_COMMAND_GENERATE_REPLY_BIT) into `query`; this prepends
 * vkSetReplyCommandStreamMESA, submits both over SUBMIT_3D into a fresh
 * host-visible reply blob, fence-waits, and copies the host-written reply into
 * `reply_out`. Returns the number of reply bytes available (capped to
 * reply_cap) on success, or <0 on failure.
 */
/* A single persistent host-visible reply blob, created on first use and reused
 * for every query round-trip. Creating/destroying a fresh blob per call churned
 * virtio-gpu resource ids (reused ids with stale host state) and corrupted the
 * Venus context for stateful commands like vkCreateDevice. */
static struct uk_virtio_gpu_blob g_reply_blob;
static int                       g_reply_blob_ready;

static int uk_venus_query_roundtrip(struct uk_virtio_gpu_dev *dev,
				    struct uk_virtio_gpu_context *ctx,
				    const void *query, uint32_t query_len,
				    void *reply_out, uint32_t reply_cap)
{
	struct uk_virtio_gpu_blob *reply = &g_reply_blob;
	struct uk_venus_encoder enc;
	uint8_t cmd_buf[320];
	uk_gpu_fence_id fence = 0;
	int rc;

	if (!dev || !ctx || !ctx->created || !query || !query_len
	    || !reply_out || !reply_cap)
		return -EINVAL;

	if (!g_reply_blob_ready) {
		memset(reply, 0, sizeof(*reply));
		rc = uk_virtio_gpu_gl_blob_create_with_ctx(dev, ctx->id,
							   UK_VENUS_REPLY_BLOB_SIZE,
							   UK_VIRTIO_GPU_BLOB_MEM_HOST3D,
							   UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE,
							   0, reply);
		if (rc)
			return rc;
		rc = uk_virtio_gpu_gl_blob_map(dev, reply);
		if (rc || !reply->mapped_addr || reply->mapped_size < 16) {
			if (reply->created)
				uk_virtio_gpu_gl_blob_destroy(dev, reply);
			memset(reply, 0, sizeof(*reply));
			return rc ? rc : -ENOTSUP;
		}
		rc = uk_virtio_gpu_gl_context_attach_resource(dev, ctx,
							      reply->resource_id);
		if (rc) {
			uk_virtio_gpu_gl_blob_unmap(dev, reply);
			uk_virtio_gpu_gl_blob_destroy(dev, reply);
			memset(reply, 0, sizeof(*reply));
			return rc;
		}
		g_reply_blob_ready = 1;
	}

	memset(reply->mapped_addr, 0, (size_t)reply->mapped_size);

	rc = uk_venus_encoder_init(&enc, cmd_buf, sizeof(cmd_buf));
	if (rc)
		return rc;
	uk_venus_encode_vkSetReplyCommandStreamMESA(&enc, reply->resource_id, 0,
						    reply->mapped_size);
	if (enc.pos + query_len > sizeof(cmd_buf))
		return -ENOSPC;
	memcpy(enc.buf + enc.pos, query, query_len);
	enc.pos += query_len;

	rc = uk_virtio_gpu_gl_context_submit(dev, ctx, enc.buf, enc.pos, &fence);
	if (rc)
		return rc;
	(void)uk_virtio_gpu_fence_wait(dev, fence, 2000000000ull);

	/* Spin until the host reply write lands (host writes only for commands
	 * flagged VK_COMMAND_GENERATE_REPLY_BIT). */
	{
		volatile const uint32_t *ctp = (volatile const uint32_t *)reply->mapped_addr;
		uint32_t spin = 0;
		while (*ctp == 0u && spin < 2000000u)
			spin++;
	}

	{
		uint32_t n = reply_cap;
		if (n > reply->mapped_size)
			n = (uint32_t)reply->mapped_size;
		memcpy(reply_out, reply->mapped_addr, n);
		rc = (int)n;
	}
	return rc;
}

int uk_venus_query_device_name(struct uk_virtio_gpu_dev *dev,
			       struct uk_virtio_gpu_context *ctx,
			       uint64_t physdev_handle,
			       char *name_out, unsigned int name_cap)
{
	struct uk_venus_encoder q;
	uint8_t qbuf[64];
	uint8_t reply[1024];
	int n;

	if (!name_out || name_cap < 2)
		return -EINVAL;
	if (uk_venus_encoder_init(&q, qbuf, sizeof(qbuf)))
		return -EINVAL;
	uk_venus_encode_vkGetPhysicalDeviceProperties(&q, physdev_handle);
	if (q.overflow)
		return -ENOSPC;
	n = uk_venus_query_roundtrip(dev, ctx, q.buf, q.pos, reply, sizeof(reply));
	if (n < (int)(UK_VENUS_PROPS_NAME_OFF + 2))
		return -1;
	return uk_venus_extract_name(reply, (size_t)n, name_out, name_cap);
}

/*
 * uk_venus_query_memory_properties — real Venus round-trip filling a
 * VkPhysicalDeviceMemoryProperties (520-byte LP64 struct) with the host's real
 * memory types/heaps. Reply wire layout (host
 * vn_encode_VkPhysicalDeviceMemoryProperties, packed):
 *   [u32 cmd=8][u64 ptr=1]
 *   [u32 memoryTypeCount][u64 asize=32] 32×{u32 propertyFlags,u32 heapIndex}
 *   [u32 memoryHeapCount][u64 asize=16] 16×{u64 size,u32 flags}
 * Struct out (offsets): typeCount@0, types[32]@4 (8B each: flags@0,heapIdx@4),
 *   heapCount@260, heaps[16]@264 (16B each: size@0,flags@8).
 * Returns 0 on success (out filled), <0 on failure (caller keeps its default).
 */
/*
 * uk_venus_query_buffer_requirements — REAL Venus round-trip returning the host
 * VkMemoryRequirements for a buffer (so allocations are the right size/alignment
 * and use a memory type the host actually allows). Reply:
 *   [u32 cmd=30][u64 ptr=1][u64 size][u64 alignment][u32 memoryTypeBits]
 * Returns 0 and fills the outputs on success, <0 otherwise.
 */
int uk_venus_query_buffer_requirements(struct uk_virtio_gpu_dev *dev,
				       struct uk_virtio_gpu_context *ctx,
				       uint64_t device_handle,
				       uint64_t buffer_handle,
				       uint64_t *size_out,
				       uint64_t *align_out,
				       uint32_t *type_bits_out)
{
	struct uk_venus_encoder q;
	uint8_t qbuf[64];
	uint8_t r[64];
	uint32_t cmd_type;
	int n;

	if (uk_venus_encoder_init(&q, qbuf, sizeof(qbuf)))
		return -EINVAL;
	uk_venus_encode_vkGetBufferMemoryRequirements(&q, device_handle, buffer_handle);
	if (q.overflow)
		return -ENOSPC;
	n = uk_venus_query_roundtrip(dev, ctx, q.buf, q.pos, r, sizeof(r));
	if (n < 32)
		return -1;
	memcpy(&cmd_type, r, 4);
	if (cmd_type != (uint32_t)VN_CMD_vkGetBufferMemoryRequirements)
		return -1;
	/* header u32 cmd + u64 ptr = 12; size@12, alignment@20, typeBits@28 */
	if (size_out)      memcpy(size_out, r + 12, 8);
	if (align_out)     memcpy(align_out, r + 20, 8);
	if (type_bits_out) memcpy(type_bits_out, r + 28, 4);
	return 0;
}

/*
 * uk_venus_create_device_checked — create the device via Venus and read the
 * host VkResult back (reply round-trip). The host only registers the device
 * object (so later vkGetDeviceQueue/allocations can look it up) when
 * vkCreateDevice returns VK_SUCCESS; this returns that result so the caller can
 * detect/diagnose a failing device creation. Returns 0 on a completed
 * round-trip (vk_result_out set) or <0 if the round-trip itself failed.
 */
int uk_venus_create_device_checked(struct uk_virtio_gpu_dev *dev,
				   struct uk_virtio_gpu_context *ctx,
				   uint64_t physdev_handle,
				   uint64_t device_handle,
				   uint32_t queue_family_index,
				   int32_t *vk_result_out)
{
	struct uk_venus_encoder q;
	uint8_t qbuf[256];
	uint8_t r[64];
	uint32_t cmd_type;
	int n;

	if (uk_venus_encoder_init(&q, qbuf, sizeof(qbuf)))
		return -EINVAL;
	uk_venus_encode_vkCreateDevice(&q, physdev_handle, device_handle,
				       queue_family_index, 1.0f, 0,
				       (const char * const *)0);
	if (q.overflow)
		return -ENOSPC;
	/* Patch the command flags word (offset 4) to request a reply. */
	if (q.pos >= 8)
		*(uint32_t *)(q.buf + 4) = 0x1u; /* VK_COMMAND_GENERATE_REPLY_BIT_EXT */
	n = uk_venus_query_roundtrip(dev, ctx, q.buf, q.pos, r, sizeof(r));
	if (n < 8)
		return -1;
	memcpy(&cmd_type, r, 4);
	if (cmd_type != (uint32_t)VN_CMD_vkCreateDevice)
		return -1;
	if (vk_result_out)
		memcpy(vk_result_out, r + 4, 4);  /* VkResult at reply offset 4 */
	return 0;
}

/*
 * uk_venus_wait_queue_idle — block until the host GPU queue is idle, via a
 * reply-bearing vkQueueWaitIdle round-trip. The host vkr executes vkQueueWaitIdle
 * IN-STREAM (it blocks the context's command decode until the GPU has drained
 * all previously-submitted work) and only then writes the reply into the
 * host-visible reply blob; uk_venus_query_roundtrip spins on that blob, so this
 * returns precisely when the GPU is done. Unlike the per-ring CONTEXT fence
 * (host sync thread), this path does not depend on the wedge-prone async
 * fence-delivery timeline — it is the reliable GPU-completion signal on this
 * stack. Returns 0 on a completed round-trip, <0 otherwise.
 */
int uk_venus_wait_queue_idle(struct uk_virtio_gpu_dev *dev,
			     struct uk_virtio_gpu_context *ctx,
			     uint64_t queue_handle)
{
	struct uk_venus_encoder q;
	uint8_t qbuf[64];
	uint8_t r[64];
	int n;

	if (uk_venus_encoder_init(&q, qbuf, sizeof(qbuf)))
		return -EINVAL;
	uk_venus_encode_vkQueueWaitIdle(&q, queue_handle);
	if (q.overflow)
		return -ENOSPC;
	/* Patch the command flags word (offset 4) to request a reply, so the host
	 * writes back only AFTER vkQueueWaitIdle returns (GPU drained). */
	if (q.pos >= 8)
		*(uint32_t *)(q.buf + 4) = 0x1u; /* VK_COMMAND_GENERATE_REPLY_BIT_EXT */
	n = uk_venus_query_roundtrip(dev, ctx, q.buf, q.pos, r, sizeof(r));
	return n < 0 ? n : 0;
}

int uk_venus_query_memory_properties(struct uk_virtio_gpu_dev *dev,
				     struct uk_virtio_gpu_context *ctx,
				     uint64_t physdev_handle,
				     void *props_out)
{
	struct uk_venus_encoder q;
	uint8_t qbuf[64];
	uint8_t r[1024];
	uint8_t *out = (uint8_t *)props_out;
	uint32_t cmd_type, type_count, heap_count, i;
	size_t off;
	int n;

	if (!out)
		return -EINVAL;
	if (uk_venus_encoder_init(&q, qbuf, sizeof(qbuf)))
		return -EINVAL;
	uk_venus_encode_vkGetPhysicalDeviceMemoryProperties(&q, physdev_handle);
	if (q.overflow)
		return -ENOSPC;
	n = uk_venus_query_roundtrip(dev, ctx, q.buf, q.pos, r, sizeof(r));
	if (n < 24)
		return -1;
	memcpy(&cmd_type, r, 4);
	if (cmd_type != (uint32_t)VN_CMD_vkGetPhysicalDeviceMemoryProperties)
		return -1;
	/* header: u32 cmd + u64 ptr = 12 bytes */
	off = 12;
	memcpy(&type_count, r + off, 4); off += 4;   /* memoryTypeCount */
	off += 8;                                      /* array_size(32) */
	if (type_count > 32u)
		return -1;
	memset(out, 0, 520);
	*(uint32_t *)(out + 0) = type_count;
	for (i = 0; i < 32u; i++) {
		uint32_t flags, heap;
		if (off + 8 > (size_t)n)
			return -1;
		memcpy(&flags, r + off, 4);
		memcpy(&heap,  r + off + 4, 4);
		off += 8;
		if (i < type_count) {
			*(uint32_t *)(out + 4 + i * 8 + 0) = flags;
			*(uint32_t *)(out + 4 + i * 8 + 4) = heap;
		}
	}
	if (off + 4 > (size_t)n)
		return -1;
	memcpy(&heap_count, r + off, 4); off += 4;     /* memoryHeapCount */
	off += 8;                                       /* array_size(16) */
	if (heap_count > 16u)
		return -1;
	*(uint32_t *)(out + 260) = heap_count;
	for (i = 0; i < 16u; i++) {
		uint64_t size;
		uint32_t flags;
		if (off + 12 > (size_t)n)
			return -1;
		memcpy(&size,  r + off, 8);
		memcpy(&flags, r + off + 8, 4);
		off += 12;
		if (i < heap_count) {
			*(uint64_t *)(out + 264 + i * 16 + 0) = size;
			*(uint32_t *)(out + 264 + i * 16 + 8) = flags;
		}
	}
	return 0;
}
