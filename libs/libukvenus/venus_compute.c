/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * libukvenus — Venus compute dispatch encoder (venus_compute.c)
 *
 * Implements Venus wire-format encoders for the Vulkan compute dispatch
 * subset needed by ggml-vulkan: buffer/memory management, shader modules,
 * compute pipelines, descriptor sets, command buffers, fences, and dispatch.
 *
 * Wire format follows Mesa's generated vn_protocol_driver_* headers:
 *   [cmd_type: uint32][cmd_flags: uint32][args...]
 * All handles are uint64_t guest-side IDs.  Pointer fields are encoded with
 * uk_venus_encode_pointer_flag(1=present / 0=NULL).  Arrays use
 * uk_venus_encode_array_size(n) (uint32_t) before the elements.
 *
 * Source lineage: Mesa src/virtio/venus-protocol/vn_protocol_driver_*.h
 * License: MIT (matches VOGUE per-file SPDX headers).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <uk/venus.h>

/* ── Vulkan structure type constants ────────────────────────────────────── */
#define VK_STYPE_APPLICATION_INFO             2u
#define VK_STYPE_DEVICE_QUEUE_CREATE_INFO     2u
#define VK_STYPE_DEVICE_CREATE_INFO           3u
#define VK_STYPE_MEMORY_ALLOCATE_INFO         5u
#define VK_STYPE_MAPPED_MEMORY_RANGE          6u
#define VK_STYPE_SUBMIT_INFO                  4u  /* VK_STRUCTURE_TYPE_SUBMIT_INFO */
#define VK_STYPE_FENCE_CREATE_INFO           8u
#define VK_STYPE_BUFFER_CREATE_INFO          12u
#define VK_STYPE_SHADER_MODULE_CREATE_INFO   16u
#define VK_STYPE_PIPELINE_LAYOUT_CREATE_INFO 30u
#define VK_STYPE_COMPUTE_PIPELINE_CREATE_INFO 29u
#define VK_STYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32u
#define VK_STYPE_DESCRIPTOR_POOL_CREATE_INFO  33u
#define VK_STYPE_DESCRIPTOR_SET_ALLOCATE_INFO 34u
#define VK_STYPE_WRITE_DESCRIPTOR_SET         35u
#define VK_STYPE_COMMAND_POOL_CREATE_INFO     39u
#define VK_STYPE_COMMAND_BUFFER_ALLOCATE_INFO 40u
#define VK_STYPE_COMMAND_BUFFER_BEGIN_INFO    42u
#define VK_STYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18u

/* VkDescriptorType */
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 7u

/* VkCommandBufferLevel */
#define VK_COMMAND_BUFFER_LEVEL_PRIMARY 0u

/* VkPipelineBindPoint */
#define VK_PIPELINE_BIND_POINT_COMPUTE 1u

/* VkImageLayout */
#define VK_IMAGE_LAYOUT_UNDEFINED 0u

/* VkAccessFlags */
#define VK_ACCESS_NONE 0u
#define VK_ACCESS_SHADER_READ_BIT  0x00000020u
#define VK_ACCESS_SHADER_WRITE_BIT 0x00000040u
#define VK_ACCESS_TRANSFER_READ_BIT  0x00000800u
#define VK_ACCESS_TRANSFER_WRITE_BIT 0x00001000u
#define VK_ACCESS_HOST_READ_BIT  0x00002000u
#define VK_ACCESS_HOST_WRITE_BIT 0x00004000u
#define VK_ACCESS_MEMORY_READ_BIT  0x00008000u
#define VK_ACCESS_MEMORY_WRITE_BIT 0x00010000u

/* VkShaderStageFlags */
#define VK_SHADER_STAGE_COMPUTE_BIT 0x20u

/* VkPipelineStageFlags */
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 0x00000800u
#define VK_PIPELINE_STAGE_TRANSFER_BIT       0x00001000u
#define VK_PIPELINE_STAGE_HOST_BIT           0x00004000u
#define VK_PIPELINE_STAGE_ALL_COMMANDS_BIT   0x00010000u

/* VkFenceCreateFlagBits */
#define VK_FENCE_CREATE_SIGNALED_BIT 0x00000001u

/* All vkDestroy<Handle>(device, handle, pAllocator) commands share one wire
 * shape: [cmd header][device][handle][pAllocator=NULL]. */
static void encode_destroy_dev_handle(struct uk_venus_encoder *enc, uint32_t cmd,
				      uint64_t device, uint64_t handle)
{
	uk_venus_encode_command_header(enc, cmd, VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint64(enc, handle);
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
}

/* ── Memory ─────────────────────────────────────────────────────────────── */

void uk_venus_encode_vkAllocateMemory(struct uk_venus_encoder *enc,
				      uint64_t device, uint64_t mem_handle,
				      uint64_t alloc_size, uint32_t mem_type_index)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkAllocateMemory,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkMemoryAllocateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_MEMORY_ALLOCATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint64(enc, alloc_size);
	uk_venus_encode_uint32(enc, mem_type_index);
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pMemory output handle */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, mem_handle);
}

void uk_venus_encode_vkFreeMemory(struct uk_venus_encoder *enc,
				  uint64_t device, uint64_t memory)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkFreeMemory,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint64(enc, memory);
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
}

void uk_venus_encode_vkBindBufferMemory(struct uk_venus_encoder *enc,
					uint64_t device, uint64_t buffer,
					uint64_t memory, uint64_t offset)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkBindBufferMemory,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint64(enc, buffer);
	uk_venus_encode_uint64(enc, memory);
	uk_venus_encode_uint64(enc, offset);
}

void uk_venus_encode_vkGetBufferMemoryRequirements(struct uk_venus_encoder *enc,
						   uint64_t device,
						   uint64_t buffer)
{
	/* VK_COMMAND_GENERATE_REPLY_BIT_EXT (0x1): the host writes the
	 * VkMemoryRequirements reply stream only when this flag is set. */
	uk_venus_encode_command_header(enc,
				       VN_CMD_vkGetBufferMemoryRequirements,
				       0x1u);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint64(enc, buffer);
	/* pMemoryRequirements: present (output — host fills it in reply) */
	uk_venus_encode_pointer_flag(enc, 1);
}

/* ── Buffers ─────────────────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateBuffer(struct uk_venus_encoder *enc,
				    uint64_t device, uint64_t buffer_handle,
				    uint64_t size, uint32_t usage)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCreateBuffer,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkBufferCreateInfo: present; sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_BUFFER_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */
	uk_venus_encode_uint64(enc, size);
	uk_venus_encode_uint32(enc, usage);
	uk_venus_encode_uint32(enc, 0u); /* sharingMode = EXCLUSIVE */
	uk_venus_encode_uint32(enc, 0u); /* queueFamilyIndexCount */
	uk_venus_encode_array_size(enc, 0); /* pQueueFamilyIndices */
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pBuffer output handle */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, buffer_handle);
}

void uk_venus_encode_vkDestroyBuffer(struct uk_venus_encoder *enc,
				     uint64_t device, uint64_t buffer)
{
	encode_destroy_dev_handle(enc, VN_CMD_vkDestroyBuffer, device, buffer);
}

/* ── Shader modules ─────────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateShaderModule(struct uk_venus_encoder *enc,
					  uint64_t device,
					  uint64_t shader_handle,
					  const uint32_t *spirv,
					  uint32_t spirv_words)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCreateShaderModule,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkShaderModuleCreateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_SHADER_MODULE_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */
	uk_venus_encode_size(enc, (uint64_t)spirv_words * 4u); /* codeSize in bytes */
	if (spirv && spirv_words) {
		uk_venus_encode_array_size(enc, spirv_words);
		uk_venus_encode_bytes(enc, spirv, spirv_words * 4u);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pShaderModule output handle */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, shader_handle);
}

void uk_venus_encode_vkDestroyShaderModule(struct uk_venus_encoder *enc,
					   uint64_t device,
					   uint64_t shader_module)
{
	encode_destroy_dev_handle(enc, VN_CMD_vkDestroyShaderModule, device,
				  shader_module);
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
	uk_venus_encode_command_header(enc, VN_CMD_vkCreatePipelineLayout,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkPipelineLayoutCreateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_PIPELINE_LAYOUT_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */
	uk_venus_encode_uint32(enc, n_set_layouts);
	if (n_set_layouts && set_layout_handles) {
		uk_venus_encode_array_size(enc, n_set_layouts);
		for (uint32_t i = 0; i < n_set_layouts; i++)
			uk_venus_encode_uint64(enc, set_layout_handles[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	uk_venus_encode_uint32(enc, n_push_ranges);
	if (n_push_ranges) {
		uk_venus_encode_array_size(enc, n_push_ranges);
		/* VkPushConstantRange: stageFlags, offset, size */
		uk_venus_encode_uint32(enc, push_stage_flags);
		uk_venus_encode_uint32(enc, push_offset);
		uk_venus_encode_uint32(enc, push_size);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pPipelineLayout output */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, layout_handle);
}

void uk_venus_encode_vkDestroyPipelineLayout(struct uk_venus_encoder *enc,
					     uint64_t device,
					     uint64_t pipeline_layout)
{
	encode_destroy_dev_handle(enc, VN_CMD_vkDestroyPipelineLayout, device,
				  pipeline_layout);
}

/* ── Compute pipelines ──────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateComputePipelines(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pipeline_handle,
					      uint64_t pipeline_layout,
					      uint64_t shader_module,
					      const char *entry_point)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCreateComputePipelines,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint64(enc, 0u); /* pipelineCache = VK_NULL_HANDLE */
	uk_venus_encode_uint32(enc, 1u); /* createInfoCount = 1 */
	/* pCreateInfos: array size (uint64) + one VkComputePipelineCreateInfo */
	uk_venus_encode_array_size(enc, 1u);

	/* VkComputePipelineCreateInfo */
	uk_venus_encode_uint32(enc, VK_STYPE_COMPUTE_PIPELINE_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */

	/* stage: VkPipelineShaderStageCreateInfo */
	uk_venus_encode_uint32(enc, VK_STYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */
	uk_venus_encode_uint32(enc, VK_SHADER_STAGE_COMPUTE_BIT); /* stage */
	uk_venus_encode_uint64(enc, shader_module); /* module */
	uk_venus_encode_cstring(enc, entry_point ? entry_point : "main");
	uk_venus_encode_pointer_flag(enc, 0); /* pSpecializationInfo */

	uk_venus_encode_uint64(enc, pipeline_layout);
	uk_venus_encode_uint64(enc, 0u); /* basePipelineHandle */
	uk_venus_encode_uint32(enc, (uint32_t)-1); /* basePipelineIndex = -1 */

	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pPipelines output: array size + one handle */
	uk_venus_encode_array_size(enc, 1u);
	uk_venus_encode_uint64(enc, pipeline_handle);
}

void uk_venus_encode_vkDestroyPipeline(struct uk_venus_encoder *enc,
				       uint64_t device, uint64_t pipeline)
{
	encode_destroy_dev_handle(enc, VN_CMD_vkDestroyPipeline, device, pipeline);
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
	uk_venus_encode_command_header(enc, VN_CMD_vkCreateDescriptorSetLayout,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkDescriptorSetLayoutCreateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */
	uk_venus_encode_uint32(enc, n_bindings);
	if (n_bindings && binding_nums) {
		uk_venus_encode_array_size(enc, n_bindings);
		for (uint32_t i = 0; i < n_bindings; i++) {
			/* VkDescriptorSetLayoutBinding */
			uk_venus_encode_uint32(enc, binding_nums[i]);
			uk_venus_encode_uint32(enc, desc_types[i]);
			uk_venus_encode_uint32(enc, desc_counts[i]);
			uk_venus_encode_uint32(enc, stage_flags[i]);
			uk_venus_encode_array_size(enc, 0); /* pImmutableSamplers */
		}
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pSetLayout output */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, layout_handle);
}

void uk_venus_encode_vkDestroyDescriptorSetLayout(struct uk_venus_encoder *enc,
						  uint64_t device,
						  uint64_t layout)
{
	encode_destroy_dev_handle(enc, VN_CMD_vkDestroyDescriptorSetLayout,
				  device, layout);
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
	uk_venus_encode_command_header(enc, VN_CMD_vkCreateDescriptorPool,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkDescriptorPoolCreateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_DESCRIPTOR_POOL_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */
	uk_venus_encode_uint32(enc, max_sets);
	uk_venus_encode_uint32(enc, pool_size_count);
	if (pool_size_count && desc_types) {
		uk_venus_encode_array_size(enc, pool_size_count);
		for (uint32_t i = 0; i < pool_size_count; i++) {
			uk_venus_encode_uint32(enc, desc_types[i]);
			uk_venus_encode_uint32(enc, desc_counts[i]);
		}
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pDescriptorPool output */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, pool_handle);
}

void uk_venus_encode_vkDestroyDescriptorPool(struct uk_venus_encoder *enc,
					     uint64_t device, uint64_t pool)
{
	encode_destroy_dev_handle(enc, VN_CMD_vkDestroyDescriptorPool, device, pool);
}

void uk_venus_encode_vkAllocateDescriptorSets(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pool,
					      uint32_t n_sets,
					      const uint64_t *set_layout_handles,
					      const uint64_t *set_handles)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkAllocateDescriptorSets,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkDescriptorSetAllocateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint64(enc, pool);
	uk_venus_encode_uint32(enc, n_sets);
	if (n_sets && set_layout_handles) {
		uk_venus_encode_array_size(enc, n_sets);
		for (uint32_t i = 0; i < n_sets; i++)
			uk_venus_encode_uint64(enc, set_layout_handles[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	/* pDescriptorSets output: array size + handles */
	uk_venus_encode_array_size(enc, n_sets);
	for (uint32_t i = 0; i < n_sets; i++)
		uk_venus_encode_uint64(enc, set_handles[i]);
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
	uk_venus_encode_command_header(enc, VN_CMD_vkUpdateDescriptorSets,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint32(enc, 1u); /* descriptorWriteCount */
	/* pDescriptorWrites: array size + one VkWriteDescriptorSet */
	uk_venus_encode_array_size(enc, 1u);
	uk_venus_encode_uint32(enc, VK_STYPE_WRITE_DESCRIPTOR_SET);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint64(enc, set);
	uk_venus_encode_uint32(enc, binding);
	uk_venus_encode_uint32(enc, 0u);  /* dstArrayElement */
	uk_venus_encode_uint32(enc, 1u);  /* descriptorCount */
	uk_venus_encode_uint32(enc, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	/* pImageInfo */
	uk_venus_encode_array_size(enc, 0);
	/* pBufferInfo: 1 VkDescriptorBufferInfo */
	uk_venus_encode_array_size(enc, 1u);
	uk_venus_encode_uint64(enc, buffer);
	uk_venus_encode_uint64(enc, buf_offset);
	uk_venus_encode_uint64(enc, buf_range);
	/* pTexelBufferView */
	uk_venus_encode_array_size(enc, 0);
	uk_venus_encode_uint32(enc, 0u); /* descriptorCopyCount */
	uk_venus_encode_array_size(enc, 0);
}

/* ── Command pool and buffers ───────────────────────────────────────────── */

void uk_venus_encode_vkCreateCommandPool(struct uk_venus_encoder *enc,
					 uint64_t device,
					 uint64_t pool_handle,
					 uint32_t queue_family_index)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCreateCommandPool,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkCommandPoolCreateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_COMMAND_POOL_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */
	uk_venus_encode_uint32(enc, queue_family_index);
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pCommandPool output */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, pool_handle);
}

void uk_venus_encode_vkDestroyCommandPool(struct uk_venus_encoder *enc,
					  uint64_t device, uint64_t pool)
{
	encode_destroy_dev_handle(enc, VN_CMD_vkDestroyCommandPool, device, pool);
}

void uk_venus_encode_vkAllocateCommandBuffers(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pool,
					      uint32_t n_bufs,
					      const uint64_t *cmd_buf_handles)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkAllocateCommandBuffers,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkCommandBufferAllocateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_COMMAND_BUFFER_ALLOCATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint64(enc, pool);
	uk_venus_encode_uint32(enc, VK_COMMAND_BUFFER_LEVEL_PRIMARY); /* level */
	uk_venus_encode_uint32(enc, n_bufs);
	/* pCommandBuffers output */
	uk_venus_encode_array_size(enc, n_bufs);
	for (uint32_t i = 0; i < n_bufs; i++)
		uk_venus_encode_uint64(enc, cmd_buf_handles[i]);
}

void uk_venus_encode_vkFreeCommandBuffers(struct uk_venus_encoder *enc,
					  uint64_t device,
					  uint64_t pool,
					  uint32_t n_bufs,
					  const uint64_t *cmd_bufs)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkFreeCommandBuffers,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint64(enc, pool);
	uk_venus_encode_uint32(enc, n_bufs);
	if (n_bufs && cmd_bufs) {
		uk_venus_encode_array_size(enc, n_bufs);
		for (uint32_t i = 0; i < n_bufs; i++)
			uk_venus_encode_uint64(enc, cmd_bufs[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
}

void uk_venus_encode_vkBeginCommandBuffer(struct uk_venus_encoder *enc,
					  uint64_t cmd_buf)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkBeginCommandBuffer,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
	/* VkCommandBufferBeginInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_COMMAND_BUFFER_BEGIN_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags: ONE_TIME_SUBMIT optional */
	uk_venus_encode_pointer_flag(enc, 0); /* pInheritanceInfo */
}

void uk_venus_encode_vkEndCommandBuffer(struct uk_venus_encoder *enc,
					uint64_t cmd_buf)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkEndCommandBuffer,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
}

/* ── Command recording ──────────────────────────────────────────────────── */

void uk_venus_encode_vkCmdBindPipeline(struct uk_venus_encoder *enc,
				       uint64_t cmd_buf,
				       uint64_t pipeline)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCmdBindPipeline,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
	uk_venus_encode_uint32(enc, VK_PIPELINE_BIND_POINT_COMPUTE);
	uk_venus_encode_uint64(enc, pipeline);
}

void uk_venus_encode_vkCmdBindDescriptorSets(struct uk_venus_encoder *enc,
					     uint64_t cmd_buf,
					     uint64_t pipeline_layout,
					     uint32_t first_set,
					     uint32_t n_sets,
					     const uint64_t *sets)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCmdBindDescriptorSets,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
	uk_venus_encode_uint32(enc, VK_PIPELINE_BIND_POINT_COMPUTE);
	uk_venus_encode_uint64(enc, pipeline_layout);
	uk_venus_encode_uint32(enc, first_set);
	uk_venus_encode_uint32(enc, n_sets);
	if (n_sets && sets) {
		uk_venus_encode_array_size(enc, n_sets);
		for (uint32_t i = 0; i < n_sets; i++)
			uk_venus_encode_uint64(enc, sets[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	uk_venus_encode_uint32(enc, 0u); /* dynamicOffsetCount */
	uk_venus_encode_array_size(enc, 0); /* pDynamicOffsets */
}

void uk_venus_encode_vkCmdPushConstants(struct uk_venus_encoder *enc,
					uint64_t cmd_buf,
					uint64_t pipeline_layout,
					uint32_t stage_flags,
					uint32_t offset,
					uint32_t size,
					const void *values)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCmdPushConstants,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
	uk_venus_encode_uint64(enc, pipeline_layout);
	uk_venus_encode_uint32(enc, stage_flags);
	uk_venus_encode_uint32(enc, offset);
	uk_venus_encode_uint32(enc, size);
	if (size && values) {
		uk_venus_encode_array_size(enc, size);
		uk_venus_encode_bytes(enc, values, size);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
}

void uk_venus_encode_vkCmdDispatch(struct uk_venus_encoder *enc,
				   uint64_t cmd_buf,
				   uint32_t x, uint32_t y, uint32_t z)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCmdDispatch,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
	uk_venus_encode_uint32(enc, x);
	uk_venus_encode_uint32(enc, y);
	uk_venus_encode_uint32(enc, z);
}

void uk_venus_encode_vkCmdCopyBuffer(struct uk_venus_encoder *enc,
				     uint64_t cmd_buf,
				     uint64_t src_buf, uint64_t dst_buf,
				     uint64_t size)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCmdCopyBuffer,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
	uk_venus_encode_uint64(enc, src_buf);
	uk_venus_encode_uint64(enc, dst_buf);
	uk_venus_encode_uint32(enc, 1u); /* regionCount */
	/* VkBufferCopy: srcOffset, dstOffset, size */
	uk_venus_encode_array_size(enc, 1u);
	uk_venus_encode_uint64(enc, 0u); /* srcOffset */
	uk_venus_encode_uint64(enc, 0u); /* dstOffset */
	uk_venus_encode_uint64(enc, size);
}

void uk_venus_encode_vkCmdFillBuffer(struct uk_venus_encoder *enc,
				     uint64_t cmd_buf, uint64_t buffer,
				     uint64_t offset, uint64_t size,
				     uint32_t data)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCmdFillBuffer,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
	uk_venus_encode_uint64(enc, buffer);
	uk_venus_encode_uint64(enc, offset);
	uk_venus_encode_uint64(enc, size);
	uk_venus_encode_uint32(enc, data);
}

void uk_venus_encode_vkCmdPipelineBarrier(struct uk_venus_encoder *enc,
					  uint64_t cmd_buf,
					  uint32_t src_stage,
					  uint32_t dst_stage)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCmdPipelineBarrier,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, cmd_buf);
	uk_venus_encode_uint32(enc, src_stage);
	uk_venus_encode_uint32(enc, dst_stage);
	uk_venus_encode_uint32(enc, 0u); /* dependencyFlags */
	uk_venus_encode_uint32(enc, 0u); /* memoryBarrierCount */
	uk_venus_encode_array_size(enc, 0); /* pMemoryBarriers */
	uk_venus_encode_uint32(enc, 0u); /* bufferMemoryBarrierCount */
	uk_venus_encode_array_size(enc, 0); /* pBufferMemoryBarriers */
	uk_venus_encode_uint32(enc, 0u); /* imageMemoryBarrierCount */
	uk_venus_encode_array_size(enc, 0); /* pImageMemoryBarriers */
}

/* ── Fences ─────────────────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateFence(struct uk_venus_encoder *enc,
				   uint64_t device, uint64_t fence_handle,
				   int signaled)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkCreateFence,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	/* VkFenceCreateInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, VK_STYPE_FENCE_CREATE_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u);
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	/* pFence output */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, fence_handle);
}

void uk_venus_encode_vkResetFences(struct uk_venus_encoder *enc,
				   uint64_t device,
				   uint32_t n_fences,
				   const uint64_t *fences)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkResetFences,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint32(enc, n_fences);
	if (n_fences && fences) {
		uk_venus_encode_array_size(enc, n_fences);
		for (uint32_t i = 0; i < n_fences; i++)
			uk_venus_encode_uint64(enc, fences[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
}

void uk_venus_encode_vkWaitForFences(struct uk_venus_encoder *enc,
				     uint64_t device,
				     uint32_t n_fences,
				     const uint64_t *fences,
				     uint64_t timeout_ns)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkWaitForFences,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device);
	uk_venus_encode_uint32(enc, n_fences);
	if (n_fences && fences) {
		uk_venus_encode_array_size(enc, n_fences);
		for (uint32_t i = 0; i < n_fences; i++)
			uk_venus_encode_uint64(enc, fences[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	uk_venus_encode_uint32(enc, 1u); /* waitAll = VK_TRUE */
	uk_venus_encode_uint64(enc, timeout_ns);
}

/* ── Queue submit with command buffers ─────────────────────────────────── */

void uk_venus_encode_vkQueueSubmit(struct uk_venus_encoder *enc,
				   uint64_t queue,
				   uint32_t n_cmd_bufs,
				   const uint64_t *cmd_bufs,
				   uint64_t fence)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkQueueSubmit,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, queue);
	uk_venus_encode_uint32(enc, 1u); /* submitCount */
	/* pSubmits: array size + one VkSubmitInfo */
	uk_venus_encode_array_size(enc, 1u);
	uk_venus_encode_uint32(enc, VK_STYPE_SUBMIT_INFO);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	/* waitSemaphoreCount = 0 */
	uk_venus_encode_uint32(enc, 0u);
	uk_venus_encode_array_size(enc, 0); /* pWaitSemaphores */
	uk_venus_encode_array_size(enc, 0); /* pWaitDstStageMask */
	uk_venus_encode_uint32(enc, n_cmd_bufs);
	if (n_cmd_bufs && cmd_bufs) {
		uk_venus_encode_array_size(enc, n_cmd_bufs);
		for (uint32_t i = 0; i < n_cmd_bufs; i++)
			uk_venus_encode_uint64(enc, cmd_bufs[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
	/* signalSemaphoreCount = 0 */
	uk_venus_encode_uint32(enc, 0u);
	uk_venus_encode_array_size(enc, 0); /* pSignalSemaphores */
	uk_venus_encode_uint64(enc, fence);
}

void uk_venus_encode_vkQueueWaitIdle(struct uk_venus_encoder *enc,
				     uint64_t queue)
{
	uk_venus_encode_command_header(enc, VN_CMD_vkQueueWaitIdle,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, queue);
}
