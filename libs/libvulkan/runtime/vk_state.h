/* SPDX-License-Identifier: MIT */
#pragma once
/*
 * Mesa alignment:
 *   Analogue: src/vulkan/runtime object/device state helpers.
 *   Same: keep object ids and lifetime flags in a state object.
 *   VOGUE adaptation: singleton state for the static ggml-vulkan ABI subset.
 */

#include <stdint.h>
#include <uk/virtio_gpu.h>
#include <uk/vulkan_venus.h>
#include <uk/vn_renderer.h>

#define UK_VULKAN_STATE_HANDLE_FIRST_DYNAMIC 0x0002000000000100ULL

struct uk_vulkan_state {
	struct uk_venus_renderer renderer;
	struct uk_vulkan_venus_dev venus;
	struct uk_virtio_gpu_dev *gpu;
	struct uk_virtio_gpu_context *ctx;
	uint64_t next_handle;
	uint64_t instance;
	uint64_t physical_device;
	uint64_t device;
	uint64_t queue;
	uint8_t initialized;
	uint8_t dispatch_initialized;
	uint8_t device_created;
	uint8_t ring_enabled;
};

uint64_t uk_vulkan_state_alloc_handle(struct uk_vulkan_state *s);
int uk_vulkan_state_init(struct uk_vulkan_state *s);
void uk_vulkan_state_fini(struct uk_vulkan_state *s);
