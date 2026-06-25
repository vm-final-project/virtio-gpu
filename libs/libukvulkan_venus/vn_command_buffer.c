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

/* ── Command pool and buffers ───────────────────────────────────────────── */

void uk_venus_encode_vkCreateCommandPool(struct uk_venus_encoder *enc,
					 uint64_t device,
					 uint64_t pool_handle,
					 uint32_t queue_family_index)
{
	ENC(enc);
	VkCommandPoolCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = queue_family_index,
	};
	VkCommandPool out = H(VkCommandPool, pool_handle);
	vn_encode_vkCreateCommandPool(&_vn, 0, H(VkDevice, device), &ci, NULL, &out);
}

void uk_venus_encode_vkDestroyCommandPool(struct uk_venus_encoder *enc,
					  uint64_t device, uint64_t pool)
{
	ENC(enc);
	vn_encode_vkDestroyCommandPool(&_vn, 0, H(VkDevice, device),
				       H(VkCommandPool, pool), NULL);
}

void uk_venus_encode_vkAllocateCommandBuffers(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pool,
					      uint32_t n_bufs,
					      const uint64_t *cmd_buf_handles)
{
	ENC(enc);
	VkCommandBuffer out[UK_VENUS_MAX_BINDINGS];
	if (n_bufs > UK_VENUS_MAX_BINDINGS)
		n_bufs = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_bufs; i++)
		out[i] = H(VkCommandBuffer, cmd_buf_handles[i]);
	VkCommandBufferAllocateInfo ai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = H(VkCommandPool, pool),
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = n_bufs,
	};
	vn_encode_vkAllocateCommandBuffers(&_vn, 0, H(VkDevice, device), &ai, out);
}

void uk_venus_encode_vkFreeCommandBuffers(struct uk_venus_encoder *enc,
					  uint64_t device,
					  uint64_t pool,
					  uint32_t n_bufs,
					  const uint64_t *cmd_bufs)
{
	ENC(enc);
	VkCommandBuffer cb[UK_VENUS_MAX_BINDINGS];
	if (n_bufs > UK_VENUS_MAX_BINDINGS)
		n_bufs = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_bufs; i++)
		cb[i] = H(VkCommandBuffer, cmd_bufs[i]);
	vn_encode_vkFreeCommandBuffers(&_vn, 0, H(VkDevice, device),
				       H(VkCommandPool, pool), n_bufs,
				       n_bufs ? cb : NULL);
}

void uk_venus_encode_vkBeginCommandBuffer(struct uk_venus_encoder *enc,
					  uint64_t cmd_buf)
{
	ENC(enc);
	VkCommandBufferBeginInfo bi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	vn_encode_vkBeginCommandBuffer(&_vn, 0, H(VkCommandBuffer, cmd_buf), &bi);
}

void uk_venus_encode_vkEndCommandBuffer(struct uk_venus_encoder *enc,
					uint64_t cmd_buf)
{
	ENC(enc);
	vn_encode_vkEndCommandBuffer(&_vn, 0, H(VkCommandBuffer, cmd_buf));
}

/* ── Command recording ──────────────────────────────────────────────────── */

void uk_venus_encode_vkCmdBindPipeline(struct uk_venus_encoder *enc,
				       uint64_t cmd_buf,
				       uint64_t pipeline)
{
	ENC(enc);
	vn_encode_vkCmdBindPipeline(&_vn, 0, H(VkCommandBuffer, cmd_buf),
				    VK_PIPELINE_BIND_POINT_COMPUTE,
				    H(VkPipeline, pipeline));
}

void uk_venus_encode_vkCmdBindDescriptorSets(struct uk_venus_encoder *enc,
					     uint64_t cmd_buf,
					     uint64_t pipeline_layout,
					     uint32_t first_set,
					     uint32_t n_sets,
					     const uint64_t *sets)
{
	ENC(enc);
	VkDescriptorSet ds[UK_VENUS_MAX_BINDINGS];
	if (n_sets > UK_VENUS_MAX_BINDINGS)
		n_sets = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_sets; i++)
		ds[i] = H(VkDescriptorSet, sets[i]);
	vn_encode_vkCmdBindDescriptorSets(&_vn, 0, H(VkCommandBuffer, cmd_buf),
					  VK_PIPELINE_BIND_POINT_COMPUTE,
					  H(VkPipelineLayout, pipeline_layout),
					  first_set, n_sets,
					  n_sets ? ds : NULL, 0, NULL);
}

void uk_venus_encode_vkCmdPushConstants(struct uk_venus_encoder *enc,
					uint64_t cmd_buf,
					uint64_t pipeline_layout,
					uint32_t stage_flags,
					uint32_t offset,
					uint32_t size,
					const void *values)
{
	ENC(enc);
	vn_encode_vkCmdPushConstants(&_vn, 0, H(VkCommandBuffer, cmd_buf),
				     H(VkPipelineLayout, pipeline_layout),
				     stage_flags, offset, size, values);
}

void uk_venus_encode_vkCmdDispatch(struct uk_venus_encoder *enc,
				   uint64_t cmd_buf,
				   uint32_t x, uint32_t y, uint32_t z)
{
	ENC(enc);
	vn_encode_vkCmdDispatch(&_vn, 0, H(VkCommandBuffer, cmd_buf), x, y, z);
}

void uk_venus_encode_vkCmdCopyBuffer(struct uk_venus_encoder *enc,
				     uint64_t cmd_buf,
				     uint64_t src_buf, uint64_t dst_buf,
				     uint64_t size)
{
	ENC(enc);
	VkBufferCopy rg = { .srcOffset = 0, .dstOffset = 0, .size = size };
	vn_encode_vkCmdCopyBuffer(&_vn, 0, H(VkCommandBuffer, cmd_buf),
				  H(VkBuffer, src_buf), H(VkBuffer, dst_buf), 1, &rg);
}

void uk_venus_encode_vkCmdFillBuffer(struct uk_venus_encoder *enc,
				     uint64_t cmd_buf, uint64_t buffer,
				     uint64_t offset, uint64_t size,
				     uint32_t data)
{
	ENC(enc);
	vn_encode_vkCmdFillBuffer(&_vn, 0, H(VkCommandBuffer, cmd_buf),
				  H(VkBuffer, buffer), offset, size, data);
}

void uk_venus_encode_vkCmdPipelineBarrier(struct uk_venus_encoder *enc,
					  uint64_t cmd_buf,
					  uint32_t src_stage,
					  uint32_t dst_stage)
{
	ENC(enc);
	vn_encode_vkCmdPipelineBarrier(&_vn, 0, H(VkCommandBuffer, cmd_buf),
				       src_stage, dst_stage, 0,
				       0, NULL, 0, NULL, 0, NULL);
}

