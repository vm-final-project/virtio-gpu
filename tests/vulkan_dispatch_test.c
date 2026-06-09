/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uk/vulkan.h>

#include "test_harness.h"

typedef uint64_t VkInstance;
typedef uint64_t VkDevice;
typedef void *PFN_vkVoidFunction;

#define VK_NULL_HANDLE 0ULL

extern PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance,
						const char *pName);
extern PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device,
					      const char *pName);

static PFN_vkVoidFunction get_proc(const char *name)
{
	return vkGetInstanceProcAddr((VkInstance)VK_NULL_HANDLE, name);
}

int main(void)
{
	struct test_state t = { .suite = "vulkan_dispatch_test" };
	struct uk_vulkan_info info = { 0 };
	const char *required[] = {
		"vkCreateInstance",
		"vkEnumeratePhysicalDevices",
		"vkCreateDevice",
		"vkGetDeviceQueue",
		"vkCreateBuffer",
		"vkCreateShaderModule",
		"vkCreateComputePipelines",
		"vkCmdDispatch",
		"vkQueueSubmit",
		"vkWaitForFences",
	};
	size_t i;

	for (i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
		char label[96];

		snprintf(label, sizeof(label), "proc:%s", required[i]);
		TEST_CHECK(&t, label, get_proc(required[i]) != NULL);
	}

	TEST_CHECK(&t, "proc unknown returns null", get_proc("vkDoesNotExist") == NULL);
	TEST_CHECK(&t, "device proc known", vkGetDeviceProcAddr((VkDevice)VK_NULL_HANDLE, "vkCmdDispatch") != NULL);
	TEST_CHECK(&t, "device proc unknown null", vkGetDeviceProcAddr((VkDevice)VK_NULL_HANDLE, "vkNoSuchFn") == NULL);
	TEST_CHECK(&t, "init returns zero", uk_vulkan_init() == 0);
	TEST_CHECK(&t, "init idempotent", uk_vulkan_init() == 0);
	TEST_CHECK(&t, "proc addr function non-null", uk_vulkan_get_instance_proc_addr_fn() != NULL);

	uk_vulkan_get_info(&info);
	TEST_CHECK(&t, "info initialized", info.initialized != 0);
	TEST_CHECK(&t, "driver venus", info.driver_name && strcmp(info.driver_name, "venus") == 0);
	TEST_CHECK(&t, "claim boundary present", info.claim_boundary && info.claim_boundary[0] != '\0');
	TEST_CHECK(&t, "supported commands non-zero", info.supported_command_count > 0u);
	TEST_CHECK(&t, "batch disabled by default", info.batch_enabled == 0);
	TEST_CHECK(&t, "hostmem fixed disabled by default", info.hostmem_fixed == 0);

	return test_finish(&t);
}
