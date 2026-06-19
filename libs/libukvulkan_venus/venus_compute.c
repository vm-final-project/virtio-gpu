/* SPDX-License-Identifier: BSD-2-Clause */
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

/* ── Buffers ─────────────────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateBuffer(struct uk_venus_encoder *enc,
				    uint64_t device, uint64_t buffer_handle,
				    uint64_t size, uint32_t usage)
{
	ENC(enc);
	VkBufferCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkBuffer out = H(VkBuffer, buffer_handle);
	vn_encode_vkCreateBuffer(&_vn, 0, H(VkDevice, device), &ci, NULL, &out);
}

void uk_venus_encode_vkDestroyBuffer(struct uk_venus_encoder *enc,
				     uint64_t device, uint64_t buffer)
{
	ENC(enc);
	vn_encode_vkDestroyBuffer(&_vn, 0, H(VkDevice, device),
				  H(VkBuffer, buffer), NULL);
}

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
					      const char *entry_point,
					      const void *spec_info)
{
	ENC(enc);
	VkComputePipelineCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = H(VkShaderModule, shader_module),
			.pName = entry_point ? entry_point : "main",
			.pSpecializationInfo =
				(const VkSpecializationInfo *)spec_info,
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

/* ── Command pool and buffers ───────────────────────────────────────────── */

void uk_venus_encode_vkCreateCommandPool(struct uk_venus_encoder *enc,
					 uint64_t device,
					 uint64_t pool_handle,
					 uint32_t queue_family_index,
					 uint32_t flags)
{
	ENC(enc);
	VkCommandPoolCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = flags,
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

void uk_venus_encode_vkResetCommandPool(struct uk_venus_encoder *enc,
					uint64_t device, uint64_t pool,
					uint32_t flags)
{
	ENC(enc);
	vn_encode_vkResetCommandPool(&_vn, 0, H(VkDevice, device),
				     H(VkCommandPool, pool), flags);
}

void uk_venus_encode_vkResetCommandBuffer(struct uk_venus_encoder *enc,
					  uint64_t cmd_buf, uint32_t flags)
{
	ENC(enc);
	vn_encode_vkResetCommandBuffer(&_vn, 0, H(VkCommandBuffer, cmd_buf),
				       flags);
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
				     uint64_t src_offset, uint64_t dst_offset,
				     uint64_t size)
{
	ENC(enc);
	VkBufferCopy rg = { .srcOffset = src_offset, .dstOffset = dst_offset,
			    .size = size };
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
	/* Emit a global VkMemoryBarrier, not just an execution dependency. ggml
	 * inserts a barrier between dependent compute dispatches; on a real GPU the
	 * memory access masks are what make the previous shader's writes visible to
	 * the next (cache flush/invalidate). Encoding zero memory barriers gives only
	 * execution ordering, so on the A30 each stage reads stale data and the model
	 * computes garbage (harmless on coherent software rasterisers). MEMORY_READ|
	 * MEMORY_WRITE covers every access type, so this is conservatively correct for
	 * every barrier ggml issues. */
	VkMemoryBarrier mb = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
	};
	vn_encode_vkCmdPipelineBarrier(&_vn, 0, H(VkCommandBuffer, cmd_buf),
				       src_stage, dst_stage, 0,
				       1, &mb, 0, NULL, 0, NULL);
}

/* ── Fences ─────────────────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateFence(struct uk_venus_encoder *enc,
				   uint64_t device, uint64_t fence_handle,
				   int signaled)
{
	ENC(enc);
	VkFenceCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u,
	};
	VkFence out = H(VkFence, fence_handle);
	vn_encode_vkCreateFence(&_vn, 0, H(VkDevice, device), &ci, NULL, &out);
}

void uk_venus_encode_vkResetFences(struct uk_venus_encoder *enc,
				   uint64_t device,
				   uint32_t n_fences,
				   const uint64_t *fences)
{
	ENC(enc);
	VkFence f[UK_VENUS_MAX_BINDINGS];
	if (n_fences > UK_VENUS_MAX_BINDINGS)
		n_fences = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_fences; i++)
		f[i] = H(VkFence, fences[i]);
	vn_encode_vkResetFences(&_vn, 0, H(VkDevice, device), n_fences,
				n_fences ? f : NULL);
}

void uk_venus_encode_vkWaitForFences(struct uk_venus_encoder *enc,
				     uint64_t device,
				     uint32_t n_fences,
				     const uint64_t *fences,
				     uint64_t timeout_ns)
{
	ENC(enc);
	VkFence f[UK_VENUS_MAX_BINDINGS];
	if (n_fences > UK_VENUS_MAX_BINDINGS)
		n_fences = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_fences; i++)
		f[i] = H(VkFence, fences[i]);
	vn_encode_vkWaitForFences(&_vn, 0, H(VkDevice, device), n_fences,
				  n_fences ? f : NULL, VK_TRUE, timeout_ns);
}

void uk_venus_encode_vkGetFenceStatus(struct uk_venus_encoder *enc,
				      uint64_t device, uint64_t fence)
{
	ENC(enc);
	vn_encode_vkGetFenceStatus(&_vn, VK_COMMAND_GENERATE_REPLY_BIT_EXT,
				   H(VkDevice, device), H(VkFence, fence));
}

/* ── Queue submit with command buffers ─────────────────────────────────── */

void uk_venus_encode_vkQueueSubmit(struct uk_venus_encoder *enc,
				   uint64_t queue,
				   uint32_t n_cmd_bufs,
				   const uint64_t *cmd_bufs,
				   uint64_t fence)
{
	ENC(enc);
	VkCommandBuffer cb[UK_VENUS_MAX_BINDINGS];
	if (n_cmd_bufs > UK_VENUS_MAX_BINDINGS)
		n_cmd_bufs = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_cmd_bufs; i++)
		cb[i] = H(VkCommandBuffer, cmd_bufs[i]);
	VkSubmitInfo si = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = n_cmd_bufs,
		.pCommandBuffers = n_cmd_bufs ? cb : NULL,
	};
	vn_encode_vkQueueSubmit(&_vn, 0, H(VkQueue, queue), 1, &si,
				H(VkFence, fence));
}

void uk_venus_encode_vkQueueWaitIdle(struct uk_venus_encoder *enc,
				     uint64_t queue)
{
	ENC(enc);
	vn_encode_vkQueueWaitIdle(&_vn, 0, H(VkQueue, queue));
}
