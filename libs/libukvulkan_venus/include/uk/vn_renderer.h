/* SPDX-License-Identifier: MIT */
#pragma once
/*
 * Mesa alignment:
 *   Analogue: src/virtio/vulkan/vn_renderer.h.
 *   Same: central renderer info for params, capset data, context status.
 *   VOGUE adaptation: native libukvirtio_gpu handles replace DRM fd state.
 */
#include <stdint.h>
#include <uk/venus.h>
#include <uk/virtio_gpu.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UK_VENUS_EXTENSION_MASK_DWORDS 32u

enum uk_venus_open_mode {
	UK_VENUS_OPEN_PROBE = 0,
	UK_VENUS_OPEN_STRICT = 1,
};

struct uk_venus_renderer_info {
	uint32_t wire_format_version;
	uint32_t vk_xml_version;
	uint32_t vk_ext_command_serialization_spec_version;
	uint32_t vk_mesa_venus_protocol_spec_version;
	uint32_t vk_extension_mask[UK_VENUS_EXTENSION_MASK_DWORDS];
	uint32_t supports_blob_id_0;
	uint32_t allow_vk_wait_syncs;
	uint32_t supports_multiple_timelines;
	uint32_t use_guest_vram;
	uint32_t max_timeline_count;
	uint8_t has_resource_blob;
	uint8_t has_host_visible;
	uint8_t has_guest_vram;
	uint8_t has_context_init;
	uint8_t context_ready;
	uint64_t supported_capsets;
	const char *status;
};

struct uk_venus_renderer {
	struct uk_virtio_gpu_dev *gpu;
	struct uk_virtio_gpu_context ctx;
	struct uk_venus_renderer_info info;
	enum uk_venus_open_mode mode;
	uint8_t opened;
};

int uk_venus_renderer_open(struct uk_venus_renderer *r,
			   uint32_t gpu_idx,
			   enum uk_venus_open_mode mode);
void uk_venus_renderer_close(struct uk_venus_renderer *r);

#ifdef __cplusplus
}
#endif
