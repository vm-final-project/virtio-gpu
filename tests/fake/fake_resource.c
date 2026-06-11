/* SPDX-License-Identifier: BSD-3-Clause */
#include "virtio_gpu_fake.h"

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

int uk_virtio_gpu_resource_attach_backing(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_sglist *sg)
{
	struct fake_resource *r = find_res(dev, res);
	if (!r || !sg || sg->sg_nseg == 0 || sg->sg_segs[0].ss_len == 0)
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

