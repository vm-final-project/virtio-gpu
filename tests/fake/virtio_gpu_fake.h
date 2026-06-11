/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef VIRTIO_GPU_FAKE_H
#define VIRTIO_GPU_FAKE_H

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

extern const struct uk_virtio_gpu_capset_info fake_capsets[MAX_CAPSETS];

/* Helper functions shared across fake backend files */
struct fake_resource *find_res(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res);
struct fake_resource *alloc_res(struct uk_virtio_gpu_dev *dev);
struct fake_context *find_ctx(struct uk_virtio_gpu_dev *dev, uk_gpu_ctx_id id);
int capset_supported(uint32_t capset_id);
int checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out);
int resource_size(uint32_t width, uint32_t height, uint32_t depth, uint64_t *size);
int complete_fence(struct uk_virtio_gpu_dev *dev, uk_gpu_fence_id *fence);
uint64_t rect_bytes(const struct uk_gpu_rect *r);

#endif /* VIRTIO_GPU_FAKE_H */
