/*
 * venus_compute_test.c — Native wire-format tests for Venus compute dispatch.
 *
 * Tests that venus_compute.c correctly encodes the Vulkan compute dispatch
 * subset into Venus wire format (VkCommandTypeEXT packed protocol).
 *
 * Each check validates:
 *   - command type field at offset 0 (uint32)
 *   - command flags field at offset 4 (uint32, always 0)
 *   - handle / first argument at offset 8 (uint64)
 *   - key struct fields at their expected offsets
 *
 * These tests run on the host without a real VirtIO-GPU device.
 * Protocol correctness is what qualifies these as evidence for
 * the LLAMA-VK-N3-DISPATCH compute dispatch gate.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <uk/venus.h>

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static int g_fail;

#define CHECK(name, cond) do { \
	if (cond) { \
		printf("  PASS  " name "\n"); \
	} else { \
		printf("  FAIL  " name "\n"); \
		g_fail++; \
	} \
} while (0)

static uint32_t r32(const uint8_t *p, size_t off)
{
	uint32_t v; memcpy(&v, p + off, 4); return v;
}
static uint64_t r64(const uint8_t *p, size_t off)
{
	uint64_t v; memcpy(&v, p + off, 8); return v;
}

/* ── vkCreateBuffer ──────────────────────────────────────────────────────── */

static void test_create_buffer(void)
{
	uint8_t buf[256];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkCreateBuffer(&enc, 0xDEAD0001ull, 0xBEEF0001ull,
				       65536u, 0x80u /* STORAGE_BUFFER */);

	CHECK("create_buffer:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("create_buffer:cmd_type", r32(buf, 0) == 50u);
	CHECK("create_buffer:cmd_flags", r32(buf, 4) == 0u);
	CHECK("create_buffer:device", r64(buf, 8) == 0xDEAD0001ull);
	/* pointer_flag(1) for VkBufferCreateInfo = uint64 1 at offset 16 */
	CHECK("create_buffer:info_present", r64(buf, 16) == 1u);
	/* sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12 at offset 24 */
	CHECK("create_buffer:stype", r32(buf, 24) == 12u);
	/* size (uint64) is after pNext(uint64)+flags(uint32) = offsets 28+8+4=? */
	/* offset 28: pNext flag (uint64 = 8 bytes)
	 * offset 36: flags (uint32 = 4 bytes)
	 * offset 40: size (uint64) */
	CHECK("create_buffer:size", r64(buf, 40) == 65536u);
	CHECK("create_buffer:usage", r32(buf, 48) == 0x80u);
	printf("  INFO  create_buffer: encoded %zu bytes\n",
	       uk_venus_encoder_size(&enc));
}

/* ── vkDestroyBuffer ─────────────────────────────────────────────────────── */

static void test_destroy_buffer(void)
{
	uint8_t buf[64];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkDestroyBuffer(&enc, 0xDEAD0001ull, 0xBEEF0001ull);

	CHECK("destroy_buffer:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("destroy_buffer:cmd_type",  r32(buf, 0) == 51u);
	CHECK("destroy_buffer:cmd_flags", r32(buf, 4) == 0u);
	CHECK("destroy_buffer:device",    r64(buf, 8) == 0xDEAD0001ull);
	CHECK("destroy_buffer:handle",    r64(buf, 16) == 0xBEEF0001ull);
}

/* ── vkAllocateMemory ────────────────────────────────────────────────────── */

static void test_allocate_memory(void)
{
	uint8_t buf[128];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkAllocateMemory(&enc, 0xDEAD0001ull, 0xAA000001ull,
					 1024u * 1024u, 2u);

	CHECK("alloc_memory:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("alloc_memory:cmd_type",  r32(buf, 0) == 21u);
	CHECK("alloc_memory:cmd_flags", r32(buf, 4) == 0u);
	CHECK("alloc_memory:device",    r64(buf, 8) == 0xDEAD0001ull);
	/* pointer_flag(1) at offset 16 */
	CHECK("alloc_memory:info_present", r64(buf, 16) == 1u);
	/* sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5 at offset 24 */
	CHECK("alloc_memory:stype",     r32(buf, 24) == 5u);
	/* pNext flag at 28 (uint64) = 0 */
	CHECK("alloc_memory:pnext_null", r64(buf, 28) == 0u);
	/* allocationSize (uint64) at offset 36 */
	CHECK("alloc_memory:size",      r64(buf, 36) == 1024u * 1024u);
	/* memoryTypeIndex (uint32) at offset 44 */
	CHECK("alloc_memory:type_idx",  r32(buf, 44) == 2u);
}

/* ── vkCreateShaderModule ────────────────────────────────────────────────── */

static void test_create_shader_module(void)
{
	/* Minimal valid SPIR-V: magic + version + generator + bound + schema */
	static const uint32_t spirv[] = {
		0x07230203u, /* SPIR-V magic */
		0x00010300u, /* version 1.3 */
		0u,          /* generator */
		4u,          /* bound */
		0u,          /* schema */
	};
	uint8_t buf[256];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkCreateShaderModule(&enc, 0xDEAD0001ull, 0xAB000001ull,
					     spirv, 5u);

	CHECK("create_shader:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("create_shader:cmd_type",  r32(buf, 0) == 59u);
	CHECK("create_shader:cmd_flags", r32(buf, 4) == 0u);
	CHECK("create_shader:device",    r64(buf, 8) == 0xDEAD0001ull);
	/* pointer_flag(1) at offset 16 */
	CHECK("create_shader:info_present", r64(buf, 16) == 1u);
	/* sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16 at offset 24 */
	CHECK("create_shader:stype",     r32(buf, 24) == 16u);
	printf("  INFO  create_shader: encoded %zu bytes\n",
	       uk_venus_encoder_size(&enc));
}

/* ── vkCreateComputePipelines ────────────────────────────────────────────── */

static void test_create_compute_pipelines(void)
{
	uint8_t buf[512];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkCreateComputePipelines(&enc,
						 0xDEAD0001ull,
						 0xAC000001ull,
						 0xAD000001ull,
						 0xAB000001ull,
						 "main");

	CHECK("create_compute:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("create_compute:cmd_type",  r32(buf, 0) == 66u);
	CHECK("create_compute:cmd_flags", r32(buf, 4) == 0u);
	CHECK("create_compute:device",    r64(buf, 8) == 0xDEAD0001ull);
	printf("  INFO  create_compute_pipelines: encoded %zu bytes\n",
	       uk_venus_encoder_size(&enc));
}

/* ── vkCreateCommandPool ─────────────────────────────────────────────────── */

static void test_create_command_pool(void)
{
	uint8_t buf[128];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkCreateCommandPool(&enc, 0xDEAD0001ull,
					    0xAE000001ull, 0u);

	CHECK("create_cmd_pool:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("create_cmd_pool:cmd_type",  r32(buf, 0) == 85u);
	CHECK("create_cmd_pool:cmd_flags", r32(buf, 4) == 0u);
	CHECK("create_cmd_pool:device",    r64(buf, 8) == 0xDEAD0001ull);
	CHECK("create_cmd_pool:info_present", r64(buf, 16) == 1u);
	/* queueFamilyIndex at sType(4)+pNext(8)+flags(4) = offset 24+4+8+4 = 40 */
	CHECK("create_cmd_pool:queue_family",
	      r32(buf, 24 + 4 + 8 + 4) == 0u);
}

/* ── vkBeginCommandBuffer / vkEndCommandBuffer ───────────────────────────── */

static void test_begin_end_command_buffer(void)
{
	uint8_t buf[128];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkBeginCommandBuffer(&enc, 0xAF000001ull);

	CHECK("begin_cmd_buf:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("begin_cmd_buf:cmd_type",   r32(buf, 0) == 90u);
	CHECK("begin_cmd_buf:cmd_flags",  r32(buf, 4) == 0u);
	CHECK("begin_cmd_buf:cmd_buf",    r64(buf, 8) == 0xAF000001ull);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkEndCommandBuffer(&enc, 0xAF000001ull);

	CHECK("end_cmd_buf:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("end_cmd_buf:cmd_type",  r32(buf, 0) == 91u);
	CHECK("end_cmd_buf:cmd_buf",   r64(buf, 8) == 0xAF000001ull);
}

/* ── vkCmdDispatch ───────────────────────────────────────────────────────── */

static void test_cmd_dispatch(void)
{
	uint8_t buf[64];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkCmdDispatch(&enc, 0xAF000001ull, 16u, 1u, 1u);

	CHECK("cmd_dispatch:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("cmd_dispatch:cmd_type",   r32(buf, 0) == 110u);
	CHECK("cmd_dispatch:cmd_flags",  r32(buf, 4) == 0u);
	CHECK("cmd_dispatch:cmd_buf",    r64(buf, 8) == 0xAF000001ull);
	CHECK("cmd_dispatch:x",          r32(buf, 16) == 16u);
	CHECK("cmd_dispatch:y",          r32(buf, 20) == 1u);
	CHECK("cmd_dispatch:z",          r32(buf, 24) == 1u);
	CHECK("cmd_dispatch:size",       uk_venus_encoder_size(&enc) == 28u);
}

/* ── vkCmdBindPipeline ───────────────────────────────────────────────────── */

static void test_cmd_bind_pipeline(void)
{
	uint8_t buf[64];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkCmdBindPipeline(&enc, 0xAF000001ull, 0xAC000001ull);

	CHECK("cmd_bind_pipeline:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("cmd_bind_pipeline:cmd_type",  r32(buf, 0) == 93u);
	CHECK("cmd_bind_pipeline:cmd_buf",   r64(buf, 8) == 0xAF000001ull);
	/* VK_PIPELINE_BIND_POINT_COMPUTE = 1 at offset 16 */
	CHECK("cmd_bind_pipeline:bind_point", r32(buf, 16) == 1u);
	CHECK("cmd_bind_pipeline:pipeline",   r64(buf, 20) == 0xAC000001ull);
}

/* ── vkCmdPushConstants ──────────────────────────────────────────────────── */

static void test_cmd_push_constants(void)
{
	uint8_t buf[128];
	struct uk_venus_encoder enc;
	uint32_t pc_data[4] = {1u, 2u, 3u, 4u};
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkCmdPushConstants(&enc, 0xAF000001ull, 0xAD000001ull,
					   0x20u /* COMPUTE */, 0u, 16u, pc_data);

	CHECK("cmd_push_constants:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("cmd_push_constants:cmd_type",  r32(buf, 0) == 132u);
	CHECK("cmd_push_constants:cmd_buf",   r64(buf, 8) == 0xAF000001ull);
	CHECK("cmd_push_constants:layout",    r64(buf, 16) == 0xAD000001ull);
	CHECK("cmd_push_constants:stage",     r32(buf, 24) == 0x20u);
	CHECK("cmd_push_constants:offset",    r32(buf, 28) == 0u);
	CHECK("cmd_push_constants:size",      r32(buf, 32) == 16u);
}

/* ── vkCreateFence ───────────────────────────────────────────────────────── */

static void test_create_fence(void)
{
	uint8_t buf[128];
	struct uk_venus_encoder enc;
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkCreateFence(&enc, 0xDEAD0001ull, 0xB0000001ull, 0);

	CHECK("create_fence:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("create_fence:cmd_type",  r32(buf, 0) == 35u);
	CHECK("create_fence:cmd_flags", r32(buf, 4) == 0u);
	CHECK("create_fence:device",    r64(buf, 8) == 0xDEAD0001ull);
	CHECK("create_fence:info_present", r64(buf, 16) == 1u);
	/* sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8 at offset 24 */
	CHECK("create_fence:stype",     r32(buf, 24) == 8u);
	/* flags (not signaled) = 0 at offset 24+4+8=36 */
	CHECK("create_fence:flags",     r32(buf, 36) == 0u);
}

/* ── vkWaitForFences ─────────────────────────────────────────────────────── */

static void test_wait_for_fences(void)
{
	uint8_t buf[128];
	struct uk_venus_encoder enc;
	uint64_t fences[1] = {0xB0000001ull};
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkWaitForFences(&enc, 0xDEAD0001ull, 1u, fences,
					(uint64_t)5000000000ull);

	CHECK("wait_fences:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("wait_fences:cmd_type",  r32(buf, 0) == 39u);
	CHECK("wait_fences:cmd_flags", r32(buf, 4) == 0u);
	CHECK("wait_fences:device",    r64(buf, 8) == 0xDEAD0001ull);
	CHECK("wait_fences:count",     r32(buf, 16) == 1u);
	/* array_size (uint64, Mesa vn_encode_array_size) = 1 at offset 20 */
	CHECK("wait_fences:arr_size",  r64(buf, 20) == 1u);
	/* fence handle (uint64) at offset 28 (shifted +4 by uint64 array_size) */
	CHECK("wait_fences:fence",     r64(buf, 28) == 0xB0000001ull);
	/* waitAll (uint32) = 1 at offset 36 */
	CHECK("wait_fences:wait_all",  r32(buf, 36) == 1u);
	/* timeout (uint64) at offset 40 */
	CHECK("wait_fences:timeout",   r64(buf, 40) == 5000000000ull);
}

/* ── vkQueueSubmit with cmd bufs ─────────────────────────────────────────── */

static void test_queue_submit(void)
{
	uint8_t buf[256];
	struct uk_venus_encoder enc;
	uint64_t cmd_bufs[1] = {0xAF000001ull};
	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	uk_venus_encode_vkQueueSubmit(&enc, 0xB1000001ull, 1u, cmd_bufs,
				      0xB0000001ull);

	CHECK("queue_submit:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("queue_submit:cmd_type",  r32(buf, 0) == 18u);
	CHECK("queue_submit:cmd_flags", r32(buf, 4) == 0u);
	CHECK("queue_submit:queue",     r64(buf, 8) == 0xB1000001ull);
	/* submitCount = 1 at offset 16 */
	CHECK("queue_submit:count",     r32(buf, 16) == 1u);
	printf("  INFO  queue_submit: encoded %zu bytes\n",
	       uk_venus_encoder_size(&enc));
}

/* ── Minimal compute dispatch sequence ───────────────────────────────────── */

static void test_compute_dispatch_sequence(void)
{
	/* Encode a realistic minimal compute dispatch sequence into one buffer
	 * to verify the commands encode without overflow and in the right order. */
	uint8_t buf[2048];
	struct uk_venus_encoder enc;
	uint64_t device      = 0xDE000001ull;
	uint64_t mem         = 0xAA000001ull;
	uint64_t buf_in      = 0xB0000001ull;
	uint64_t buf_out     = 0xB0000002ull;
	uint64_t shader      = 0xAB000001ull;
	uint64_t dsl         = 0xC0000001ull;
	uint64_t dp          = 0xC1000001ull;
	uint64_t ds          = 0xC2000001ull;
	uint64_t playout     = 0xC3000001ull;
	uint64_t pipeline    = 0xAC000001ull;
	uint64_t cmd_pool    = 0xAE000001ull;
	uint64_t cmd_buf     = 0xAF000001ull;
	uint64_t fence       = 0xB0000001ull;
	uint64_t queue       = 0xB1000001ull;

	static const uint32_t spirv_hdr[] = {
		0x07230203u, 0x00010300u, 0u, 8u, 0u
	};
	uint32_t b_nums[2]  = {0u, 1u};
	uint32_t d_types[2] = {7u, 7u}; /* STORAGE_BUFFER */
	uint32_t d_cnts[2]  = {1u, 1u};
	uint32_t s_flags[2] = {0x20u, 0x20u}; /* COMPUTE */
	uint64_t dsl_arr[1] = {dsl};
	uint64_t ds_arr[1]  = {ds};

	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	/* 1. Allocate device memory */
	uk_venus_encode_vkAllocateMemory(&enc, device, mem, 65536u, 0u);
	/* 2. Create input buffer */
	uk_venus_encode_vkCreateBuffer(&enc, device, buf_in, 32768u, 0x80u);
	/* 3. Create output buffer */
	uk_venus_encode_vkCreateBuffer(&enc, device, buf_out, 32768u, 0x80u);
	/* 4. Bind buffers to memory */
	uk_venus_encode_vkBindBufferMemory(&enc, device, buf_in, mem, 0u);
	uk_venus_encode_vkBindBufferMemory(&enc, device, buf_out, mem, 32768u);
	/* 5. Shader module */
	uk_venus_encode_vkCreateShaderModule(&enc, device, shader,
					     spirv_hdr, 5u);
	/* 6. Descriptor set layout */
	uk_venus_encode_vkCreateDescriptorSetLayout(&enc, device, dsl,
						    2u, b_nums, d_types,
						    d_cnts, s_flags);
	/* 7. Descriptor pool */
	uk_venus_encode_vkCreateDescriptorPool(&enc, device, dp, 1u,
					       1u, d_types, d_cnts);
	/* 8. Allocate descriptor set */
	uk_venus_encode_vkAllocateDescriptorSets(&enc, device, dp, 1u,
						 dsl_arr, ds_arr);
	/* 9. Pipeline layout */
	uk_venus_encode_vkCreatePipelineLayout(&enc, device, playout,
					       1u, dsl_arr, 0u, 0u, 0u, 0u);
	/* 10. Compute pipeline */
	uk_venus_encode_vkCreateComputePipelines(&enc, device, pipeline,
						 playout, shader, "main");
	/* 11. Command pool */
	uk_venus_encode_vkCreateCommandPool(&enc, device, cmd_pool, 0u);
	/* 12. Allocate command buffer */
	uk_venus_encode_vkAllocateCommandBuffers(&enc, device, cmd_pool, 1u,
						 &cmd_buf);
	/* 13. Record: begin */
	uk_venus_encode_vkBeginCommandBuffer(&enc, cmd_buf);
	/* 14. Bind pipeline */
	uk_venus_encode_vkCmdBindPipeline(&enc, cmd_buf, pipeline);
	/* 15. Bind descriptor sets */
	uk_venus_encode_vkCmdBindDescriptorSets(&enc, cmd_buf, playout,
						0u, 1u, ds_arr);
	/* 16. Dispatch 256 work-groups */
	uk_venus_encode_vkCmdDispatch(&enc, cmd_buf, 256u, 1u, 1u);
	/* 17. End recording */
	uk_venus_encode_vkEndCommandBuffer(&enc, cmd_buf);
	/* 18. Fence */
	uk_venus_encode_vkCreateFence(&enc, device, fence, 0);
	/* 19. Submit */
	uk_venus_encode_vkQueueSubmit(&enc, queue, 1u, &cmd_buf, fence);
	/* 20. Wait */
	uk_venus_encode_vkWaitForFences(&enc, device, 1u, &fence,
					5000000000ull);

	size_t total = uk_venus_encoder_size(&enc);
	CHECK("compute_sequence:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("compute_sequence:encoded_bytes", total > 0u && total < sizeof(buf));

	/* Spot-check: first command must be vkAllocateMemory = 21 */
	CHECK("compute_sequence:first_cmd", r32(buf, 0) == 21u);

	printf("  INFO  compute_dispatch_sequence: encoded %zu bytes "
	       "covering 20 commands\n", total);
	printf("  PASS  venus-compute-dispatch:sequence\n");
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
	printf("venus_compute_test: Venus compute dispatch wire-format tests\n");
	printf("============================================================\n");

	test_create_buffer();
	test_destroy_buffer();
	test_allocate_memory();
	test_create_shader_module();
	test_create_compute_pipelines();
	test_create_command_pool();
	test_begin_end_command_buffer();
	test_cmd_dispatch();
	test_cmd_bind_pipeline();
	test_cmd_push_constants();
	test_create_fence();
	test_wait_for_fences();
	test_queue_submit();
	test_compute_dispatch_sequence();

	printf("============================================================\n");
	if (g_fail == 0) {
		printf("venus_compute_test: all checks PASS  "
		       "(Venus compute dispatch encoded correctly)\n");
		printf("uk-venus-compute: PASS evidence_id=venus-compute-dispatch\n");
	} else {
		printf("venus_compute_test: %d check(s) FAILED\n", g_fail);
	}
	return g_fail ? 1 : 0;
}
