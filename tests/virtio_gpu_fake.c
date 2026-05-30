#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <uk/virtio_gpu.h>

#define MAX_RESOURCES 64u
#define MAX_CONTEXTS 16u
#define MAX_CAPSETS 5u
#define MAX_FAKE_BLOB_SIZE (16u * 1024u * 1024u)
#define WEAK __attribute__((weak))

struct fake_resource {
	uk_gpu_res_id id;
	uint8_t live;
	uint8_t is_3d;
	uint8_t backing_attached;
	uint8_t uuid_assigned;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t format;
	uint64_t size;
	uint8_t uuid[16];
};

struct fake_context {
	uk_gpu_ctx_id id;
	uint32_t capset_id;
	uint8_t live;
	uk_gpu_res_id attached[MAX_RESOURCES];
	size_t attached_count;
};

struct uk_virtio_gpu_dev {
	uk_gpu_res_id next_res;
	uk_gpu_ctx_id next_ctx;
	uk_gpu_fence_id next_fence;
	uk_gpu_fence_id completed_fence;
	struct fake_resource resources[MAX_RESOURCES];
	struct fake_context contexts[MAX_CONTEXTS];
	struct uk_virtio_gpu_metrics metrics;
};

static const struct uk_virtio_gpu_capset_info fake_capsets[MAX_CAPSETS] = {
	{ UK_VIRTIO_GPU_CAPSET_VIRGL, 2, 256 },
	{ UK_VIRTIO_GPU_CAPSET_VIRGL2, 2, 256 },
	{ UK_VIRTIO_GPU_CAPSET_VENUS, 1, 512 },
	{ UK_VIRTIO_GPU_CAPSET_DRM, 1, 128 },
	{ UK_VIRTIO_GPU_CAPSET_APIR, 1, 64 },
};

static struct fake_resource *find_res(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res)
{
	if (!dev || !res)
		return NULL;
	for (size_t i = 0; i < MAX_RESOURCES; i++)
		if (dev->resources[i].live && dev->resources[i].id == res)
			return &dev->resources[i];
	return NULL;
}

static struct fake_resource *alloc_res(struct uk_virtio_gpu_dev *dev)
{
	if (!dev)
		return NULL;
	for (size_t i = 0; i < MAX_RESOURCES; i++) {
		if (!dev->resources[i].live) {
			memset(&dev->resources[i], 0, sizeof(dev->resources[i]));
			dev->resources[i].id = dev->next_res++;
			dev->resources[i].live = 1;
			dev->metrics.resources_created++;
			return &dev->resources[i];
		}
	}
	return NULL;
}

static struct fake_context *find_ctx(struct uk_virtio_gpu_dev *dev, uk_gpu_ctx_id id)
{
	if (!dev || !id)
		return NULL;
	for (size_t i = 0; i < MAX_CONTEXTS; i++)
		if (dev->contexts[i].live && dev->contexts[i].id == id)
			return &dev->contexts[i];
	return NULL;
}

static int capset_supported(uint32_t capset_id)
{
	for (size_t i = 0; i < MAX_CAPSETS; i++)
		if (fake_capsets[i].id == capset_id)
			return 1;
	return 0;
}

static int checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out)
{
	if (!out)
		return -EINVAL;
	if (a && b > UINT64_MAX / a)
		return -EOVERFLOW;
	*out = a * b;
	return 0;
}

static int resource_size(uint32_t width, uint32_t height, uint32_t depth,
				 uint64_t *size)
{
	uint64_t wh;
	int rc;

	rc = checked_mul_u64(width, height, &wh);
	if (rc)
		return rc;
	rc = checked_mul_u64(wh, depth ? depth : 1u, &wh);
	if (rc)
		return rc;
	return checked_mul_u64(wh, 4u, size);
}

static int validate_xfer(const struct fake_resource *r,
			 const struct uk_virtio_gpu_transfer_3d *xfer,
			 uint64_t *bytes)
{
	uint64_t end;
	uint64_t row_bytes;
	uint64_t layer_bytes;
	uint64_t transfer_bytes;
	uint64_t depth = xfer && xfer->box.d ? xfer->box.d : 1u;

	if (!r || !xfer || !xfer->box.w || !xfer->box.h)
		return -EINVAL;
	if (xfer->box.x > r->width || xfer->box.y > r->height ||
	    xfer->box.z > r->depth)
		return -EINVAL;
	if (xfer->box.w > r->width - xfer->box.x ||
	    xfer->box.h > r->height - xfer->box.y ||
	    depth > r->depth - xfer->box.z)
		return -EINVAL;
	if (checked_mul_u64(xfer->box.w, 4u, &row_bytes))
		return -EOVERFLOW;
	if (xfer->stride && (uint64_t)xfer->stride < row_bytes)
		return -EINVAL;
	if (checked_mul_u64(xfer->stride ? xfer->stride : row_bytes,
				    xfer->box.h, &layer_bytes))
		return -EOVERFLOW;
	if (xfer->layer_stride && depth > 1 &&
	    (uint64_t)xfer->layer_stride < layer_bytes)
		return -EINVAL;
	if (checked_mul_u64(layer_bytes, depth, &transfer_bytes))
		return -EOVERFLOW;
	if (xfer->offset > UINT64_MAX - transfer_bytes)
		return -EOVERFLOW;
	end = xfer->offset + transfer_bytes;
	if (end > r->size)
		return -EINVAL;
	if (bytes)
		*bytes = transfer_bytes;
	return 0;
}

static int complete_fence(struct uk_virtio_gpu_dev *dev, uk_gpu_fence_id *fence)
{
	if (!dev)
		return -EINVAL;
	/* NULL fence => caller asked for fire-and-forget (no FLAG_FENCE). The
	 * real driver still advances its internal completion counter so a later
	 * fenced command sees the un-fenced one as already complete in order. */
	uk_gpu_fence_id id = dev->next_fence++;
	dev->completed_fence = id;
	dev->metrics.fences_issued++;
	if (fence)
		*fence = id;
	return 0;
}

static uint64_t rect_bytes(const struct uk_gpu_rect *r)
{
	return r ? (uint64_t)r->w * (uint64_t)r->h * 4u : 0;
}

int uk_virtio_gpu_probe(struct uk_virtio_gpu_dev **dev)
{
	if (!dev)
		return -EINVAL;
	*dev = calloc(1, sizeof(**dev));
	if (!*dev)
		return -ENOMEM;
	(*dev)->next_res = 1;
	(*dev)->next_ctx = 1;
	(*dev)->next_fence = 1;
	(*dev)->metrics.probes = 1;
	return 0;
}

int uk_virtio_gpu_get_display_info(struct uk_virtio_gpu_dev *dev)
{
	return dev ? 0 : -EINVAL;
}

int uk_virtio_gpu_resource_create_2d(struct uk_virtio_gpu_dev *dev, uint32_t width, uint32_t height, uint32_t format, uk_gpu_res_id *res)
{
	struct fake_resource *r;
	if (!dev || !width || !height || !res)
		return -EINVAL;
	r = alloc_res(dev);
	if (!r)
		return -ENOMEM;
	r->width = width;
	r->height = height;
	r->depth = 1;
	r->format = format;
	if (resource_size(width, height, 1, &r->size)) {
		memset(r, 0, sizeof(*r));
		return -EOVERFLOW;
	}
	*res = r->id;
	return 0;
}

int uk_virtio_gpu_resource_attach_backing(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_dma_sg *sg, size_t nr_sg)
{
	struct fake_resource *r = find_res(dev, res);
	if (!r || !sg || nr_sg == 0 || sg[0].len == 0)
		return -EINVAL;
	r->backing_attached = 1;
	dev->metrics.attach_calls++;
	return 0;
}

int uk_virtio_gpu_transfer_to_host_2d(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_gpu_rect *r, uk_gpu_fence_id *fence)
{
	struct fake_resource *fr = find_res(dev, res);
	if (!fr || !r || !r->w || !r->h || !fr->backing_attached)
		return -EINVAL;
	dev->metrics.transfers_to_host++;
	dev->metrics.bytes_to_host += rect_bytes(r);
	return complete_fence(dev, fence);
}

int uk_virtio_gpu_resource_flush(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_gpu_rect *r, uk_gpu_fence_id *fence)
{
	if (!find_res(dev, res) || !r || !r->w || !r->h)
		return -EINVAL;
	dev->metrics.flushes++;
	return complete_fence(dev, fence);
}

int uk_virtio_gpu_transfer_and_flush_2d(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_gpu_rect *r, uk_gpu_fence_id *fence)
{
	int rc;
	if (!fence)
		return -EINVAL;
	rc = uk_virtio_gpu_transfer_to_host_2d(dev, res, r, NULL);
	if (rc)
		return rc;
	return uk_virtio_gpu_resource_flush(dev, res, r, fence);
}

int uk_virtio_gpu_fence_wait(struct uk_virtio_gpu_dev *dev, uk_gpu_fence_id fence, uint64_t timeout_ns)
{
	(void)timeout_ns;
	if (!dev || !fence)
		return -EINVAL;
	dev->metrics.fence_waits++;
	return fence <= dev->completed_fence ? 0 : -ETIMEDOUT;
}

int uk_virtio_gpu_gl_caps_get(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_caps *caps)
{
	if (!dev || !caps)
		return -EINVAL;
	*caps = (struct uk_virtio_gpu_caps) {
		.num_scanouts = 1,
		.num_capsets = MAX_CAPSETS,
		.device_features = UK_VIRTIO_GPU_F_VIRGL | UK_VIRTIO_GPU_F_EDID |
			UK_VIRTIO_GPU_F_RESOURCE_UUID | UK_VIRTIO_GPU_F_RESOURCE_BLOB |
			UK_VIRTIO_GPU_F_CONTEXT_INIT,
		.negotiated_features = UK_VIRTIO_GPU_F_VIRGL | UK_VIRTIO_GPU_F_EDID |
			UK_VIRTIO_GPU_F_RESOURCE_UUID | UK_VIRTIO_GPU_F_RESOURCE_BLOB |
			UK_VIRTIO_GPU_F_CONTEXT_INIT,
		.has_virgl = 1,
		.has_edid = 1,
		.has_resource_uuid = 1,
		.has_resource_blob = 1,
		.has_context_init = 1,
		.uses_blob_scanout = 0,
		.has_host_visible = 1,
	};
	return 0;
}

int uk_virtio_gpu_gl_capset_info_get(struct uk_virtio_gpu_dev *dev, uint32_t index, struct uk_virtio_gpu_capset_info *info)
{
	if (!dev || !info)
		return -EINVAL;
	if (index >= MAX_CAPSETS)
		return -ENOENT;
	*info = fake_capsets[index];
	return 0;
}

int uk_virtio_gpu_gl_capset_get(struct uk_virtio_gpu_dev *dev, uint32_t capset_id, uint32_t version, void *buf, size_t len, size_t *actual_len)
{
	const char *payload = "unikraft-fake-virgl-capset";
	size_t need = strlen(payload) + 1;
	if (!dev || !capset_supported(capset_id) || version == 0)
		return -EINVAL;
	if (actual_len)
		*actual_len = need;
	if (!buf || len < need)
		return -ENOSPC;
	memcpy(buf, payload, need);
	return 0;
}

int uk_virtio_gpu_gl_get_edid(struct uk_virtio_gpu_dev *dev, uint32_t scanout_id, void *buf, size_t len, size_t *actual_len)
{
	uint8_t edid[128];
	if (!dev || scanout_id != 0)
		return -EINVAL;
	memset(edid, 0, sizeof(edid));
	edid[0] = 0x00; edid[1] = 0xff; edid[2] = 0xff; edid[3] = 0xff;
	edid[4] = 0xff; edid[5] = 0xff; edid[6] = 0xff; edid[7] = 0x00;
	if (actual_len)
		*actual_len = sizeof(edid);
	if (!buf || len < sizeof(edid))
		return -ENOSPC;
	memcpy(buf, edid, sizeof(edid));
	return 0;
}

int uk_virtio_gpu_gl_set_scanout(struct uk_virtio_gpu_dev *dev, uint32_t scanout_id, uk_gpu_res_id res, const struct uk_gpu_rect *r)
{
	if (!find_res(dev, res) || scanout_id != 0 || !r || !r->w || !r->h)
		return -EINVAL;
	return 0;
}

int uk_virtio_gpu_gl_resource_detach_backing(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res)
{
	struct fake_resource *r = find_res(dev, res);
	if (!r || !r->backing_attached)
		return -EINVAL;
	r->backing_attached = 0;
	dev->metrics.detach_calls++;
	return 0;
}

int uk_virtio_gpu_gl_resource_unref(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res)
{
	struct fake_resource *r = find_res(dev, res);
	if (!r)
		return -EINVAL;
	memset(r, 0, sizeof(*r));
	dev->metrics.resources_unrefed++;
	return 0;
}

int uk_virtio_gpu_gl_resource_assign_uuid(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, uint8_t uuid[16])
{
	struct fake_resource *r = find_res(dev, res);
	if (!r || !uuid)
		return -EINVAL;
	for (size_t i = 0; i < 16; i++)
		uuid[i] = (uint8_t)(res + i);
	memcpy(r->uuid, uuid, 16);
	r->uuid_assigned = 1;
	return 0;
}

int uk_virtio_gpu_gl_resource_create_3d(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_resource_3d *desc, uk_gpu_res_id *res)
{
	struct fake_resource *r;
	if (!dev || !desc || !desc->width || !desc->height || !desc->depth || !res)
		return -EINVAL;
	r = alloc_res(dev);
	if (!r)
		return -ENOMEM;
	r->is_3d = 1;
	r->width = desc->width;
	r->height = desc->height;
	r->depth = desc->depth;
	r->format = desc->format;
	if (resource_size(desc->width, desc->height, desc->depth, &r->size)) {
		memset(r, 0, sizeof(*r));
		return -EOVERFLOW;
	}
	*res = r->id;
	return 0;
}

int uk_virtio_gpu_gl_transfer_to_host_3d(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_virtio_gpu_transfer_3d *xfer, uk_gpu_fence_id *fence)
{
	struct fake_resource *r = find_res(dev, res);
	uint64_t bytes = 0;
	int rc;
	if (!r || !r->is_3d)
		return -EINVAL;
	rc = validate_xfer(r, xfer, &bytes);
	if (rc)
		return rc;
	if (UINT64_MAX - dev->metrics.bytes_to_host < bytes)
		return -EOVERFLOW;
	dev->metrics.transfers_to_host++;
	dev->metrics.bytes_to_host += bytes;
	return complete_fence(dev, fence);
}

int uk_virtio_gpu_gl_transfer_from_host_3d(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_virtio_gpu_transfer_3d *xfer, uk_gpu_fence_id *fence)
{
	struct fake_resource *r = find_res(dev, res);
	uint64_t bytes = 0;
	int rc;
	if (!r || !r->is_3d)
		return -EINVAL;
	rc = validate_xfer(r, xfer, &bytes);
	if (rc)
		return rc;
	if (UINT64_MAX - dev->metrics.bytes_from_host < bytes)
		return -EOVERFLOW;
	dev->metrics.transfers_from_host++;
	dev->metrics.bytes_from_host += bytes;
	return complete_fence(dev, fence);
}

int uk_virtio_gpu_gl_context_create(struct uk_virtio_gpu_dev *dev, uint32_t capset_id, const char *debug_name, struct uk_virtio_gpu_context *ctx)
{
	(void)debug_name;
	if (!dev || !ctx || ctx->created || !capset_supported(capset_id))
		return -EINVAL;
	for (size_t i = 0; i < MAX_CONTEXTS; i++) {
		if (!dev->contexts[i].live) {
			dev->contexts[i].id = dev->next_ctx++;
			dev->contexts[i].capset_id = capset_id;
			dev->contexts[i].live = 1;
			*ctx = (struct uk_virtio_gpu_context) { dev->contexts[i].id, capset_id, 1 };
			dev->metrics.contexts_created++;
			return 0;
		}
	}
	return -ENOMEM;
}

int uk_virtio_gpu_gl_context_destroy(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_context *ctx)
{
	struct fake_context *fc;
	if (!dev || !ctx)
		return -EINVAL;
	if (!ctx->created)
		return 0;
	fc = find_ctx(dev, ctx->id);
	if (!fc)
		return -EINVAL;
	memset(fc, 0, sizeof(*fc));
	ctx->created = 0;
	dev->metrics.contexts_destroyed++;
	return 0;
}

int uk_virtio_gpu_gl_context_attach_resource(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, uk_gpu_res_id res)
{
	struct fake_context *fc = ctx ? find_ctx(dev, ctx->id) : NULL;
	if (!fc || !find_res(dev, res))
		return -EINVAL;
	for (size_t i = 0; i < fc->attached_count; i++)
		if (fc->attached[i] == res)
			return 0;
	if (fc->attached_count >= MAX_RESOURCES)
		return -ENOMEM;
	fc->attached[fc->attached_count++] = res;
	return 0;
}

int uk_virtio_gpu_gl_context_detach_resource(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, uk_gpu_res_id res)
{
	struct fake_context *fc = ctx ? find_ctx(dev, ctx->id) : NULL;
	if (!fc || !find_res(dev, res))
		return -EINVAL;
	for (size_t i = 0; i < fc->attached_count; i++) {
		if (fc->attached[i] == res) {
			fc->attached[i] = fc->attached[--fc->attached_count];
			return 0;
		}
	}
	return -EINVAL;
}

int uk_virtio_gpu_gl_context_submit(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const void *cmd, size_t cmd_len, uk_gpu_fence_id *fence)
{
	if (!dev || !ctx || !find_ctx(dev, ctx->id) || !cmd || !cmd_len)
		return -EINVAL;
	dev->metrics.submits_3d++;
	return complete_fence(dev, fence);
}

int uk_virtio_gpu_gl_blob_create(struct uk_virtio_gpu_dev *dev, uint64_t size, uint32_t blob_mem, uint32_t blob_flags, uint64_t blob_id, struct uk_virtio_gpu_blob *blob)
{
	struct fake_resource *r;
	if (!dev || !size || !blob || blob->created)
		return -EINVAL;
	if (size > MAX_FAKE_BLOB_SIZE || size > (uint64_t)SIZE_MAX)
		return -EOVERFLOW;
	if (blob_mem != UK_VIRTIO_GPU_BLOB_MEM_GUEST && blob_mem != UK_VIRTIO_GPU_BLOB_MEM_HOST3D && blob_mem != UK_VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST)
		return -EINVAL;
	r = alloc_res(dev);
	if (!r)
		return -ENOMEM;
	r->is_3d = 1;
	r->size = size;
	*blob = (struct uk_virtio_gpu_blob) {
		.resource_id = r->id,
		.blob_mem = blob_mem,
		.blob_flags = blob_flags,
		.map_info = 0,
		.blob_id = blob_id,
		.size = size,
		.host_visible_offset = 0,
		.mapped_addr = NULL,
		.mapped_size = 0,
		.reply_notif = NULL,
		.created = 1,
		.mapped = 0,
	};
	dev->metrics.blobs_created++;
	return 0;
}

int uk_virtio_gpu_gl_blob_create_with_ctx(struct uk_virtio_gpu_dev *dev, uint32_t ctx_id, uint64_t size, uint32_t blob_mem, uint32_t blob_flags, uint64_t blob_id, struct uk_virtio_gpu_blob *blob)
{
	if (ctx_id && !find_ctx(dev, ctx_id))
		return -EINVAL;
	return uk_virtio_gpu_gl_blob_create(dev, size, blob_mem, blob_flags,
					    blob_id, blob);
}

int uk_virtio_gpu_gl_blob_map(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob)
{
	if (!dev || !blob || !blob->created || blob->mapped || !find_res(dev, blob->resource_id))
		return -EINVAL;
	blob->mapped_addr = calloc(1, (size_t)blob->size);
	if (!blob->mapped_addr)
		return -ENOMEM;
	blob->mapped_size = blob->size;
	blob->reply_notif = (volatile uint32_t *)blob->mapped_addr;
	blob->map_info = 1;
	blob->mapped = 1;
	dev->metrics.blobs_mapped++;
	return 0;
}

int uk_virtio_gpu_gl_blob_unmap(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob)
{
	if (!dev || !blob || !blob->created || !blob->mapped)
		return -EINVAL;
	free(blob->mapped_addr);
	blob->mapped_addr = NULL;
	blob->mapped_size = 0;
	blob->reply_notif = NULL;
	blob->mapped = 0;
	dev->metrics.blobs_unmapped++;
	return 0;
}

int uk_virtio_gpu_gl_blob_destroy(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob)
{
	if (!dev || !blob || !blob->created || blob->mapped)
		return -EINVAL;
	if (uk_virtio_gpu_gl_resource_unref(dev, blob->resource_id))
		return -EINVAL;
	memset(blob, 0, sizeof(*blob));
	return 0;
}

int uk_virtio_gpu_gl_metrics_get(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_metrics *metrics)
{
	if (!dev || !metrics)
		return -EINVAL;
	*metrics = dev->metrics;
	return 0;
}

int uk_virtio_gpu_gl_metrics_reset(struct uk_virtio_gpu_dev *dev)
{
	if (!dev)
		return -EINVAL;
	memset(&dev->metrics, 0, sizeof(dev->metrics));
	return 0;
}


struct uk_virtio_gpu_dev *WEAK uk_virtio_gpu_default_dev(void)
{
	return NULL;
}

struct uk_virtio_gpu_dev *WEAK uk_virtio_gpu_from_fbdev(struct uk_fbdev *fbdev)
{
	(void)fbdev;
	return NULL;
}

int WEAK uk_virtio_gpu_dev_caps_get(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_caps *caps)
{
	return uk_virtio_gpu_gl_caps_get(dev, caps);
}

int WEAK uk_virtio_gpu_dev_capset_info_get(struct uk_virtio_gpu_dev *dev, uint32_t index, struct uk_virtio_gpu_capset_info *info)
{
	return uk_virtio_gpu_gl_capset_info_get(dev, index, info);
}

int WEAK uk_virtio_gpu_dev_shm_region_get(struct uk_virtio_gpu_dev *dev, uint32_t id, struct uk_virtio_gpu_shm_region *region)
{
	(void)dev;
	(void)id;
	(void)region;
	return -ENOTSUP;
}

int WEAK uk_virtio_gpu_caps_get(struct uk_fbdev *fbdev, struct uk_virtio_gpu_caps *caps)
{
	return uk_virtio_gpu_dev_caps_get(uk_virtio_gpu_from_fbdev(fbdev), caps);
}

int WEAK uk_virtio_gpu_capset_info_get(struct uk_fbdev *fbdev, uint32_t index, struct uk_virtio_gpu_capset_info *info)
{
	return uk_virtio_gpu_dev_capset_info_get(uk_virtio_gpu_from_fbdev(fbdev), index, info);
}

const char *WEAK uk_virtio_gpu_capset_name(uint32_t capset_id)
{
	return uk_virtio_gpu_gl_capset_name(capset_id);
}

int WEAK uk_virtio_gpu_context_create(struct uk_virtio_gpu_dev *dev, uint32_t capset_id, const char *debug_name, struct uk_virtio_gpu_context *ctx)
{
	return uk_virtio_gpu_gl_context_create(dev, capset_id, debug_name, ctx);
}

int WEAK uk_virtio_gpu_context_destroy(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_context *ctx)
{
	return uk_virtio_gpu_gl_context_destroy(dev, ctx);
}

int WEAK uk_virtio_gpu_context_attach_blob(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *blob)
{
	return blob ? uk_virtio_gpu_gl_context_attach_resource(dev, ctx, blob->resource_id) : -EINVAL;
}

int WEAK uk_virtio_gpu_context_detach_blob(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *blob)
{
	return blob ? uk_virtio_gpu_gl_context_detach_resource(dev, ctx, blob->resource_id) : -EINVAL;
}

int WEAK uk_virtio_gpu_context_submit(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const void *cmd, size_t cmd_len)
{
	uk_gpu_fence_id fence = 0;
	return uk_virtio_gpu_gl_context_submit(dev, ctx, cmd, cmd_len, &fence);
}

int WEAK uk_virtio_gpu_blob_create(struct uk_virtio_gpu_dev *dev, uint64_t size, uint32_t blob_mem, uint32_t blob_flags, uint64_t blob_id, struct uk_virtio_gpu_blob *blob)
{
	return uk_virtio_gpu_gl_blob_create(dev, size, blob_mem, blob_flags, blob_id, blob);
}

int WEAK uk_virtio_gpu_blob_map(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob)
{
	return uk_virtio_gpu_gl_blob_map(dev, blob);
}

int WEAK uk_virtio_gpu_blob_unmap(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob)
{
	return uk_virtio_gpu_gl_blob_unmap(dev, blob);
}

int WEAK uk_virtio_gpu_blob_destroy(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob)
{
	return uk_virtio_gpu_gl_blob_destroy(dev, blob);
}

static int apir_put_u32(uint8_t *buf, size_t len, size_t *off, uint32_t value)
{
	if (!buf || !off || *off > len || len - *off < sizeof(value))
		return -ENOSPC;
	memcpy(buf + *off, &value, sizeof(value));
	*off += sizeof(value);
	return 0;
}

static int apir_get_u32(const uint8_t *buf, size_t len, size_t *off, uint32_t *value)
{
	if (!buf || !off || !value || *off > len || len - *off < sizeof(*value))
		return -EINVAL;
	memcpy(value, buf + *off, sizeof(*value));
	*off += sizeof(*value);
	return 0;
}

int WEAK uk_virtio_gpu_apir_encode_header(void *buf, size_t len, const struct uk_virtio_gpu_apir_msg *msg, size_t *used)
{
	size_t off = 0;
	int rc;
	if (!buf || !msg || msg->command_type >= UK_VIRTIO_GPU_APIR_COMMAND_LENGTH)
		return -EINVAL;
	rc = apir_put_u32(buf, len, &off, msg->command_type);
	if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, msg->flags);
	if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, msg->reply_resource_id);
	if (rc) return rc;
	if (used) *used = off;
	return 0;
}

int WEAK uk_virtio_gpu_apir_decode_header(const void *buf, size_t len, struct uk_virtio_gpu_apir_msg *msg, size_t *used)
{
	size_t off = 0;
	int rc;
	if (!buf || !msg)
		return -EINVAL;
	rc = apir_get_u32(buf, len, &off, &msg->command_type);
	if (rc) return rc;
	rc = apir_get_u32(buf, len, &off, &msg->flags);
	if (rc) return rc;
	rc = apir_get_u32(buf, len, &off, &msg->reply_resource_id);
	if (rc) return rc;
	if (msg->command_type >= UK_VIRTIO_GPU_APIR_COMMAND_LENGTH)
		return -EINVAL;
	if (used) *used = off;
	return 0;
}

int WEAK uk_virtio_gpu_apir_encode_handshake(void *buf, size_t len, uint32_t reply_resource_id, size_t *used)
{
	struct uk_virtio_gpu_apir_msg msg = {
		.command_type = UK_VIRTIO_GPU_APIR_COMMAND_HANDSHAKE,
		.flags = 0,
		.reply_resource_id = reply_resource_id,
	};
	size_t off = 0;
	int rc = uk_virtio_gpu_apir_encode_header(buf, len, &msg, &off);
	if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, UK_VIRTIO_GPU_APIR_PROTOCOL_MAJOR);
	if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, UK_VIRTIO_GPU_APIR_PROTOCOL_MINOR);
	if (rc) return rc;
	if (used) *used = off;
	return 0;
}

int WEAK uk_virtio_gpu_apir_decode_handshake_reply(const void *buf, size_t len, uint32_t host_rc, struct uk_virtio_gpu_apir_handshake *reply)
{
	size_t off = 0;
	if (!reply || host_rc != UK_VIRTIO_GPU_APIR_HANDSHAKE_MAGIC)
		return -EINVAL;
	reply->guest_major = UK_VIRTIO_GPU_APIR_PROTOCOL_MAJOR;
	reply->guest_minor = UK_VIRTIO_GPU_APIR_PROTOCOL_MINOR;
	if (apir_get_u32(buf, len, &off, &reply->host_major))
		return -EINVAL;
	if (apir_get_u32(buf, len, &off, &reply->host_minor))
		return -EINVAL;
	return 0;
}

int WEAK uk_virtio_gpu_apir_handshake(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, struct uk_virtio_gpu_apir_handshake *reply, struct uk_virtio_gpu_apir_status *status)
{
	(void)dev; (void)ctx; (void)reply_blob; (void)reply;
	if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP, .host_rc = (uint32_t)-1, .reply_size = 0 };
	return -ENOTSUP;
}

int WEAK uk_virtio_gpu_apir_load_library(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, struct uk_virtio_gpu_apir_status *status)
{
	(void)dev; (void)ctx; (void)reply_blob;
	if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP, .host_rc = (uint32_t)-1, .reply_size = 0 };
	return -ENOTSUP;
}

int WEAK uk_virtio_gpu_apir_forward(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, uint32_t flags, const void *payload, size_t payload_len, struct uk_virtio_gpu_apir_status *status)
{
	(void)dev; (void)ctx; (void)reply_blob; (void)flags; (void)payload; (void)payload_len;
	if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP, .host_rc = (uint32_t)-1, .reply_size = 0 };
	return -ENOTSUP;
}

const char *uk_virtio_gpu_gl_capset_name(uint32_t capset_id)
{
	switch (capset_id) {
	case UK_VIRTIO_GPU_CAPSET_VIRGL: return "virgl";
	case UK_VIRTIO_GPU_CAPSET_VIRGL2: return "virgl2";
	case UK_VIRTIO_GPU_CAPSET_GFXSTREAM: return "gfxstream";
	case UK_VIRTIO_GPU_CAPSET_VENUS: return "venus";
	case UK_VIRTIO_GPU_CAPSET_CROSS_DOMAIN: return "cross-domain";
	case UK_VIRTIO_GPU_CAPSET_DRM: return "drm-native-context";
	case UK_VIRTIO_GPU_CAPSET_APIR: return "apir";
	default: return "unknown";
	}
}
