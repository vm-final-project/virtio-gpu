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

/* ── Shader modules ─────────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateShaderModule(struct uk_venus_encoder *enc,
					  uint64_t device,
					  uint64_t shader_handle,
					  const uint32_t *spirv,
					  uint32_t spirv_words)
{
	ENC(enc);
	VkShaderModuleCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = (size_t)spirv_words * 4u,
		.pCode = spirv,
	};
	VkShaderModule out = H(VkShaderModule, shader_handle);
	vn_encode_vkCreateShaderModule(&_vn, 0, H(VkDevice, device), &ci, NULL, &out);
}

void uk_venus_encode_vkDestroyShaderModule(struct uk_venus_encoder *enc,
					   uint64_t device,
					   uint64_t shader_module)
{
	ENC(enc);
	vn_encode_vkDestroyShaderModule(&_vn, 0, H(VkDevice, device),
					H(VkShaderModule, shader_module), NULL);
}

/* ── Pipeline layout ────────────────────────────────────────────────────── */

void uk_venus_encode_vkCreatePipelineLayout(struct uk_venus_encoder *enc,
					    uint64_t device,
					    uint64_t layout_handle,
					    uint32_t n_set_layouts,
					    const uint64_t *set_layout_handles,
					    uint32_t n_push_ranges,
					    uint32_t push_stage_flags,
					    uint32_t push_offset,
					    uint32_t push_size)
{
	ENC(enc);
	VkDescriptorSetLayout sl[UK_VENUS_MAX_BINDINGS];
	if (n_set_layouts > UK_VENUS_MAX_BINDINGS)
		n_set_layouts = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_set_layouts; i++)
		sl[i] = H(VkDescriptorSetLayout, set_layout_handles[i]);
	VkPushConstantRange pcr = {
		.stageFlags = push_stage_flags,
		.offset = push_offset,
		.size = push_size,
	};
	VkPipelineLayoutCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = n_set_layouts,
		.pSetLayouts = n_set_layouts ? sl : NULL,
		.pushConstantRangeCount = n_push_ranges,
		.pPushConstantRanges = n_push_ranges ? &pcr : NULL,
	};
	VkPipelineLayout out = H(VkPipelineLayout, layout_handle);
	vn_encode_vkCreatePipelineLayout(&_vn, 0, H(VkDevice, device), &ci, NULL, &out);
}

void uk_venus_encode_vkDestroyPipelineLayout(struct uk_venus_encoder *enc,
					     uint64_t device,
					     uint64_t pipeline_layout)
{
	ENC(enc);
	vn_encode_vkDestroyPipelineLayout(&_vn, 0, H(VkDevice, device),
					  H(VkPipelineLayout, pipeline_layout), NULL);
}

/* ── Compute pipelines ──────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateComputePipelines(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pipeline_handle,
					      uint64_t pipeline_layout,
					      uint64_t shader_module,
					      const char *entry_point)
{
	ENC(enc);
	VkComputePipelineCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = H(VkShaderModule, shader_module),
			.pName = entry_point ? entry_point : "main",
			.pSpecializationInfo = NULL,
		},
		.layout = H(VkPipelineLayout, pipeline_layout),
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = -1,
	};
	VkPipeline out = H(VkPipeline, pipeline_handle);
	vn_encode_vkCreateComputePipelines(&_vn, 0, H(VkDevice, device),
					   VK_NULL_HANDLE, 1, &ci, NULL, &out);
}

void uk_venus_encode_vkDestroyPipeline(struct uk_venus_encoder *enc,
				       uint64_t device, uint64_t pipeline)
{
	ENC(enc);
	vn_encode_vkDestroyPipeline(&_vn, 0, H(VkDevice, device),
				    H(VkPipeline, pipeline), NULL);
}

