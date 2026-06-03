/* tests/venus_generated_compile_test.c
 *
 * Includes the generated Venus encoder headers through the vn_cs/vn_ring shim
 * and exercises a few encoders end-to-end, so the whole include graph must
 * compile + link and produce a non-empty wire stream. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <uk/venus.h>
#include <uk/vn_cs.h>
#include "vn_protocol_driver_buffer.h"
#include "vn_protocol_driver_command_buffer.h"
#include "vn_protocol_driver_device_memory.h"

int main(void)
{
	uint8_t buf[512];
	struct uk_venus_encoder e;
	struct vn_cs_encoder enc = { .e = &e };

	/* vkCmdDispatch — pure command-buffer recording */
	uk_venus_encoder_init(&e, buf, sizeof(buf));
	VkCommandBuffer cb = (VkCommandBuffer)(uintptr_t)0x1234;
	vn_encode_vkCmdDispatch(&enc, 0, cb, 4, 5, 6);
	assert(!uk_venus_encoder_overflow(&e));
	assert(uk_venus_encoder_size(&e) > 0);

	/* vkCreateBuffer — struct-bearing create */
	uk_venus_encoder_init(&e, buf, sizeof(buf));
	VkDevice dev = (VkDevice)(uintptr_t)7;
	VkBuffer out = (VkBuffer)(uintptr_t)9;
	VkBufferCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = 1024,
		.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	vn_encode_vkCreateBuffer(&enc, 0, dev, &ci, NULL, &out);
	assert(!uk_venus_encoder_overflow(&e));
	assert(uk_venus_encoder_size(&e) > 0);

	printf("venus_generated_compile_test: PASS\n");
	return 0;
}
