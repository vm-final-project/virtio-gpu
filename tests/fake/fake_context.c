/* SPDX-License-Identifier: BSD-3-Clause */
#include "virtio_gpu_fake.h"

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
