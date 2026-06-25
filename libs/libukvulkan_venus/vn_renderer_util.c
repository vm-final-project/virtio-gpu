/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Mesa alignment:
 *   Analogue: src/virtio/virtio-gpu/venus_hw.h and
 *             src/virtio/vulkan/vn_renderer_virtgpu.c capset decode.
 *   Same: decode virgl_renderer_capset_venus as a versioned capability block.
 *   VOGUE adaptation: expose the decoded subset through uk_venus_caps.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <uk/venus.h>

void uk_venus_decode_capset(struct uk_venus_caps *caps,
			    const uint8_t *raw, size_t actual)
{
	const uint32_t *f = (const uint32_t *)raw;

	if (!caps)
		return;

	memset(caps, 0, sizeof(*caps));
	if (!raw)
		return;

	if (actual >= 4)
		caps->wire_format_version = f[0];
	if (actual >= 8)
		caps->vk_xml_version = f[1];
	if (actual >= 12)
		caps->vk_ext_command_serialization_spec_version = f[2];
	if (actual >= 16)
		caps->vk_mesa_venus_protocol_spec_version = f[3];
	if (actual >= 20)
		caps->supports_blob_id_0 = f[4];
	if (actual >= 148)
		memcpy(caps->vk_extension_mask1, &f[5],
		       sizeof(caps->vk_extension_mask1));
	if (actual >= 152)
		caps->allow_vk_wait_syncs = f[37];
	if (actual >= 156)
		caps->supports_multiple_timelines = f[38];
	if (actual >= 160)
		caps->use_guest_vram = f[39];
}
