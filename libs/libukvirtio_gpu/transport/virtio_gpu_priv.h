#pragma once
/*
 * Mesa alignment:
 *   Analogue: private state behind src/virtio/vulkan/vn_renderer_virtgpu.c.
 *   Same: keep transport-only device/resource/context state private.
 *   VOGUE adaptation: Unikraft virtqueue and allocator state.
 */
#include <stdint.h>
#include <stddef.h>
#include <uk/virtio_gpu.h>
#include "../protocol/virtio_gpu_proto.h"

#define UKVGPU_MAX_RESOURCES 128u
#define UKVGPU_MAX_CONTEXTS 32u
#define UKVGPU_CTRLQ 0u
#define UKVGPU_CURSORQ 1u
#define UKVGPU_NR_QUEUES 2u

struct ukvgpu_resource_state {
	uk_gpu_res_id id;
	uint8_t live;
	uint8_t is_3d;
	uint8_t backing_attached;
	uint8_t is_blob;
	uint8_t mapped;
	uint8_t reserved;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t format;
	uint32_t blob_mem;
	uint32_t blob_flags;
	uint32_t map_info;
	uint32_t padding;
	uint64_t size;
	uint64_t blob_id;
	uint64_t host_visible_offset;
};

struct ukvgpu_context_state {
	uk_gpu_ctx_id id;
	uint32_t capset_id;
	uint8_t live;
};

struct uk_virtio_gpu_dev {
	struct virtio_dev *vdev;
	struct virtqueue *ctrlq;
	struct virtqueue *cursorq;
	struct uk_alloc *alloc;
	uint64_t host_features;
	uint64_t negotiated_features;
	uint32_t num_scanouts;
	uint32_t num_capsets;
	uint32_t events_read;
	uint32_t blob_alignment;
	uint64_t next_blob_map_offset;
	struct uk_virtio_gpu_shm_region host_visible;
	struct ukvgpu_display_one scanouts[UKVGPU_MAX_SCANOUTS];
	uk_gpu_res_id next_res;
	uk_gpu_ctx_id next_ctx;
	uk_gpu_fence_id next_fence;
	uk_gpu_fence_id completed_fence;
	struct ukvgpu_resource_state resources[UKVGPU_MAX_RESOURCES];
	struct ukvgpu_context_state contexts[UKVGPU_MAX_CONTEXTS];
	struct uk_virtio_gpu_metrics metrics;
};
