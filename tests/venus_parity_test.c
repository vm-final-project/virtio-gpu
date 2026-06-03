/* tests/venus_parity_test.c
 *
 * Byte-for-byte parity between the hand-written uk_venus_encode_* encoders and
 * the deterministically generated vn_encode_vk* encoders, for the commands on
 * the ggml-vulkan compute path. The generated encoder is the source of truth:
 * a mismatch means the hand-written encoder diverges from the real Venus wire
 * format. Both implementations link side by side (different symbol names).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uk/venus.h>
#include <uk/vn_cs.h>
#include "vn_protocol_driver_buffer.h"
#include "vn_protocol_driver_command_buffer.h"
#include "vn_protocol_driver_device_memory.h"
#include "vn_protocol_driver_fence.h"
#include "vn_protocol_driver_shader_module.h"
#include "vn_protocol_driver_queue.h"

static int g_fail;

static void chk(const char *name, const uint8_t *l, size_t nl,
		const uint8_t *g, size_t ng)
{
	if (nl == ng && memcmp(l, g, nl) == 0) {
		printf("  PASS  %s (%zu bytes)\n", name, nl);
		return;
	}
	g_fail++;
	printf("  FAIL  %s: legacy=%zu gen=%zu\n", name, nl, ng);
	printf("    legacy="); for (size_t i = 0; i < nl; i++) printf("%02x", l[i]);
	printf("\n    gen   ="); for (size_t i = 0; i < ng; i++) printf("%02x", g[i]);
	printf("\n");
}

#define H(v) ((void *)(uintptr_t)(v))

int main(void)
{
	uint8_t lb[2048], gb[2048];
	struct uk_venus_encoder le, ge;
	struct vn_cs_encoder e;
#define INIT() do { uk_venus_encoder_init(&le, lb, sizeof lb); \
		     uk_venus_encoder_init(&ge, gb, sizeof gb); e.e = &ge; } while (0)
#define LN uk_venus_encoder_size(&le)
#define GN uk_venus_encoder_size(&ge)

	/* vkCmdDispatch */
	INIT();
	uk_venus_encode_vkCmdDispatch(&le, 0x1234, 4, 5, 6);
	vn_encode_vkCmdDispatch(&e, 0, (VkCommandBuffer)H(0x1234), 4, 5, 6);
	chk("vkCmdDispatch", lb, LN, gb, GN);

	/* vkCreateBuffer */
	INIT();
	uk_venus_encode_vkCreateBuffer(&le, 7, 9, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = 1024, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE };
	VkBuffer bo = (VkBuffer)H(9);
	vn_encode_vkCreateBuffer(&e, 0, (VkDevice)H(7), &bci, NULL, &bo);
	chk("vkCreateBuffer", lb, LN, gb, GN);

	/* vkDestroyBuffer */
	INIT();
	uk_venus_encode_vkDestroyBuffer(&le, 7, 9);
	vn_encode_vkDestroyBuffer(&e, 0, (VkDevice)H(7), (VkBuffer)H(9), NULL);
	chk("vkDestroyBuffer", lb, LN, gb, GN);

	/* vkAllocateMemory */
	INIT();
	uk_venus_encode_vkAllocateMemory(&le, 7, 0x50, 4096, 3);
	VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = 4096, .memoryTypeIndex = 3 };
	VkDeviceMemory mo = (VkDeviceMemory)H(0x50);
	vn_encode_vkAllocateMemory(&e, 0, (VkDevice)H(7), &mai, NULL, &mo);
	chk("vkAllocateMemory", lb, LN, gb, GN);

	/* vkFreeMemory */
	INIT();
	uk_venus_encode_vkFreeMemory(&le, 7, 0x50);
	vn_encode_vkFreeMemory(&e, 0, (VkDevice)H(7), (VkDeviceMemory)H(0x50), NULL);
	chk("vkFreeMemory", lb, LN, gb, GN);

	/* vkBindBufferMemory */
	INIT();
	uk_venus_encode_vkBindBufferMemory(&le, 7, 9, 0x50, 0);
	vn_encode_vkBindBufferMemory(&e, 0, (VkDevice)H(7), (VkBuffer)H(9),
				     (VkDeviceMemory)H(0x50), 0);
	chk("vkBindBufferMemory", lb, LN, gb, GN);

	/* vkCmdCopyBuffer */
	INIT();
	uk_venus_encode_vkCmdCopyBuffer(&le, 0x10, 0x20, 0x30, 256);
	VkBufferCopy rg = { .srcOffset = 0, .dstOffset = 0, .size = 256 };
	vn_encode_vkCmdCopyBuffer(&e, 0, (VkCommandBuffer)H(0x10), (VkBuffer)H(0x20),
				  (VkBuffer)H(0x30), 1, &rg);
	chk("vkCmdCopyBuffer", lb, LN, gb, GN);

	/* vkCmdFillBuffer */
	INIT();
	uk_venus_encode_vkCmdFillBuffer(&le, 0x10, 0x20, 0, 256, 0xab);
	vn_encode_vkCmdFillBuffer(&e, 0, (VkCommandBuffer)H(0x10), (VkBuffer)H(0x20),
				  0, 256, 0xab);
	chk("vkCmdFillBuffer", lb, LN, gb, GN);

	/* vkCmdBindPipeline */
	INIT();
	uk_venus_encode_vkCmdBindPipeline(&le, 0x10, 0x40);
	vn_encode_vkCmdBindPipeline(&e, 0, (VkCommandBuffer)H(0x10),
				    VK_PIPELINE_BIND_POINT_COMPUTE, (VkPipeline)H(0x40));
	chk("vkCmdBindPipeline", lb, LN, gb, GN);

	/* vkCmdPushConstants */
	INIT();
	uint32_t pc[4] = { 1, 2, 3, 4 };
	uk_venus_encode_vkCmdPushConstants(&le, 0x10, 0x13, VK_SHADER_STAGE_COMPUTE_BIT,
					   0, sizeof pc, pc);
	vn_encode_vkCmdPushConstants(&e, 0, (VkCommandBuffer)H(0x10),
				     (VkPipelineLayout)H(0x13),
				     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
	chk("vkCmdPushConstants", lb, LN, gb, GN);

	/* vkCreateShaderModule */
	INIT();
	static const uint32_t spirv[5] = { 0x07230203, 0x00010000, 0, 1, 0 };
	uk_venus_encode_vkCreateShaderModule(&le, 7, 0x10f, spirv, 5);
	VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof spirv, .pCode = spirv };
	VkShaderModule smo = (VkShaderModule)H(0x10f);
	vn_encode_vkCreateShaderModule(&e, 0, (VkDevice)H(7), &smci, NULL, &smo);
	chk("vkCreateShaderModule", lb, LN, gb, GN);

	/* vkCreateFence */
	INIT();
	uk_venus_encode_vkCreateFence(&le, 7, 0x60, 0);
	VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fo = (VkFence)H(0x60);
	vn_encode_vkCreateFence(&e, 0, (VkDevice)H(7), &fci, NULL, &fo);
	chk("vkCreateFence", lb, LN, gb, GN);

	/* vkResetFences */
	INIT();
	uint64_t fences[1] = { 0x60 };
	uk_venus_encode_vkResetFences(&le, 7, 1, fences);
	VkFence gf[1] = { (VkFence)H(0x60) };
	vn_encode_vkResetFences(&e, 0, (VkDevice)H(7), 1, gf);
	chk("vkResetFences", lb, LN, gb, GN);

	/* vkQueueWaitIdle */
	INIT();
	uk_venus_encode_vkQueueWaitIdle(&le, 0x77);
	vn_encode_vkQueueWaitIdle(&e, 0, (VkQueue)H(0x77));
	chk("vkQueueWaitIdle", lb, LN, gb, GN);

	if (g_fail) {
		printf("venus_parity_test: %d MISMATCH(es)\n", g_fail);
		return 1;
	}
	printf("venus_parity_test: PASS (all parity checks identical)\n");
	return 0;
}
