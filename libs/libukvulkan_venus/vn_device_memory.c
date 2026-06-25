/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Mesa alignment:
 *   Analogue: src/virtio/venus-protocol/vn_protocol_driver*.h compute/object
 *             command encoders.
 *   Same: build real Vk* structs and call generated vn_encode_vk* helpers.
 *   VOGUE adaptation: expose a compact scalar API for ggml-vulkan.
 */
/*
 * libukvulkan_venus — Venus compute dispatch encoder (venus_compute.c)
 *
 * Thin bridge from VOGUE's scalar `uk_venus_encode_*` API onto the encoders
 * generated from the pinned Mesa `../venus-protocol` (see GENERATOR.md). Each
 * function builds the real `Vk*` struct(s) from its scalar arguments and calls
 * the generated `vn_encode_vk*`, so the in-image wire format IS the Mesa Venus
 * format (no hand-rolled byte layout here). Vulkan handles are bare uint64
 * guest IDs (`id == (uintptr_t)handle`, per the vn_cs.h shim).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <uk/venus.h>
#include <uk/vn_cs.h>
#include "vn_protocol_driver.h"

/* ggml descriptor sets / pipeline layouts are small; these caps bound the
 * temporary handle/struct arrays used to marshal scalar inputs. */
#define UK_VENUS_MAX_BINDINGS 64u

#define H(T, id) ((T)(uintptr_t)(id))
#define ENC(arg) struct vn_cs_encoder _vn = { .e = (arg) }

/* ── Memory ─────────────────────────────────────────────────────────────── */

void uk_venus_encode_vkAllocateMemory(struct uk_venus_encoder *enc,
				      uint64_t device, uint64_t mem_handle,
				      uint64_t alloc_size, uint32_t mem_type_index)
{
	ENC(enc);
	VkMemoryAllocateInfo ai = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = alloc_size,
		.memoryTypeIndex = mem_type_index,
	};
	VkDeviceMemory out = H(VkDeviceMemory, mem_handle);
	vn_encode_vkAllocateMemory(&_vn, 0, H(VkDevice, device), &ai, NULL, &out);
}

void uk_venus_encode_vkFreeMemory(struct uk_venus_encoder *enc,
				  uint64_t device, uint64_t memory)
{
	ENC(enc);
	vn_encode_vkFreeMemory(&_vn, 0, H(VkDevice, device),
			       H(VkDeviceMemory, memory), NULL);
}

void uk_venus_encode_vkBindBufferMemory(struct uk_venus_encoder *enc,
					uint64_t device, uint64_t buffer,
					uint64_t memory, uint64_t offset)
{
	ENC(enc);
	vn_encode_vkBindBufferMemory(&_vn, 0, H(VkDevice, device),
				     H(VkBuffer, buffer),
				     H(VkDeviceMemory, memory), offset);
}

void uk_venus_encode_vkGetBufferMemoryRequirements(struct uk_venus_encoder *enc,
						   uint64_t device,
						   uint64_t buffer)
{
	ENC(enc);
	VkMemoryRequirements req = {0};
	vn_encode_vkGetBufferMemoryRequirements(&_vn,
		VK_COMMAND_GENERATE_REPLY_BIT_EXT, H(VkDevice, device),
		H(VkBuffer, buffer), &req);
}

