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

/* ── Descriptor set layout ──────────────────────────────────────────────── */

void uk_venus_encode_vkCreateDescriptorSetLayout(struct uk_venus_encoder *enc,
						 uint64_t device,
						 uint64_t layout_handle,
						 uint32_t n_bindings,
						 const uint32_t *binding_nums,
						 const uint32_t *desc_types,
						 const uint32_t *desc_counts,
						 const uint32_t *stage_flags)
{
	ENC(enc);
	VkDescriptorSetLayoutBinding b[UK_VENUS_MAX_BINDINGS];
	if (n_bindings > UK_VENUS_MAX_BINDINGS)
		n_bindings = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_bindings; i++) {
		b[i] = (VkDescriptorSetLayoutBinding){
			.binding = binding_nums[i],
			.descriptorType = desc_types[i],
			.descriptorCount = desc_counts[i],
			.stageFlags = stage_flags[i],
			.pImmutableSamplers = NULL,
		};
	}
	VkDescriptorSetLayoutCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = n_bindings,
		.pBindings = n_bindings ? b : NULL,
	};
	VkDescriptorSetLayout out = H(VkDescriptorSetLayout, layout_handle);
	vn_encode_vkCreateDescriptorSetLayout(&_vn, 0, H(VkDevice, device), &ci, NULL, &out);
}

void uk_venus_encode_vkDestroyDescriptorSetLayout(struct uk_venus_encoder *enc,
						  uint64_t device,
						  uint64_t layout)
{
	ENC(enc);
	vn_encode_vkDestroyDescriptorSetLayout(&_vn, 0, H(VkDevice, device),
					       H(VkDescriptorSetLayout, layout), NULL);
}

/* ── Descriptor pool and sets ───────────────────────────────────────────── */

void uk_venus_encode_vkCreateDescriptorPool(struct uk_venus_encoder *enc,
					    uint64_t device,
					    uint64_t pool_handle,
					    uint32_t max_sets,
					    uint32_t pool_size_count,
					    const uint32_t *desc_types,
					    const uint32_t *desc_counts)
{
	ENC(enc);
	VkDescriptorPoolSize ps[UK_VENUS_MAX_BINDINGS];
	if (pool_size_count > UK_VENUS_MAX_BINDINGS)
		pool_size_count = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < pool_size_count; i++) {
		ps[i] = (VkDescriptorPoolSize){
			.type = desc_types[i],
			.descriptorCount = desc_counts[i],
		};
	}
	VkDescriptorPoolCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = max_sets,
		.poolSizeCount = pool_size_count,
		.pPoolSizes = pool_size_count ? ps : NULL,
	};
	VkDescriptorPool out = H(VkDescriptorPool, pool_handle);
	vn_encode_vkCreateDescriptorPool(&_vn, 0, H(VkDevice, device), &ci, NULL, &out);
}

void uk_venus_encode_vkDestroyDescriptorPool(struct uk_venus_encoder *enc,
					     uint64_t device, uint64_t pool)
{
	ENC(enc);
	vn_encode_vkDestroyDescriptorPool(&_vn, 0, H(VkDevice, device),
					  H(VkDescriptorPool, pool), NULL);
}

void uk_venus_encode_vkAllocateDescriptorSets(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pool,
					      uint32_t n_sets,
					      const uint64_t *set_layout_handles,
					      const uint64_t *set_handles)
{
	ENC(enc);
	VkDescriptorSetLayout sl[UK_VENUS_MAX_BINDINGS];
	VkDescriptorSet out[UK_VENUS_MAX_BINDINGS];
	if (n_sets > UK_VENUS_MAX_BINDINGS)
		n_sets = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_sets; i++) {
		sl[i] = H(VkDescriptorSetLayout, set_layout_handles[i]);
		out[i] = H(VkDescriptorSet, set_handles[i]);
	}
	VkDescriptorSetAllocateInfo ai = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = H(VkDescriptorPool, pool),
		.descriptorSetCount = n_sets,
		.pSetLayouts = n_sets ? sl : NULL,
	};
	vn_encode_vkAllocateDescriptorSets(&_vn, 0, H(VkDevice, device), &ai, out);
}

/* Simplified update: single storage buffer binding */
void uk_venus_encode_vkUpdateDescriptorSets_storage(struct uk_venus_encoder *enc,
						    uint64_t device,
						    uint64_t set,
						    uint32_t binding,
						    uint64_t buffer,
						    uint64_t buf_offset,
						    uint64_t buf_range)
{
	ENC(enc);
	VkDescriptorBufferInfo bi = {
		.buffer = H(VkBuffer, buffer),
		.offset = buf_offset,
		.range = buf_range,
	};
	VkWriteDescriptorSet w = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = H(VkDescriptorSet, set),
		.dstBinding = binding,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pImageInfo = NULL,
		.pBufferInfo = &bi,
		.pTexelBufferView = NULL,
	};
	vn_encode_vkUpdateDescriptorSets(&_vn, 0, H(VkDevice, device), 1, &w, 0, NULL);
}

