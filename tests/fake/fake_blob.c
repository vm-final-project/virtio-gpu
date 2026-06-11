/* SPDX-License-Identifier: BSD-3-Clause */
#include "virtio_gpu_fake.h"

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
