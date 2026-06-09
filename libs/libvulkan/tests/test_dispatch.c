/* SPDX-License-Identifier: BSD-3-Clause */
#include <string.h>

#include <uk/test.h>
#include <uk/vulkan.h>

typedef uint64_t VkInstance;
typedef uint64_t VkDevice;
typedef void *PFN_vkVoidFunction;

#define VK_NULL_HANDLE 0ULL

extern PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance,
						const char *pName);
extern PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device,
					      const char *pName);

static PFN_vkVoidFunction test_get_proc(const char *name)
{
	return vkGetInstanceProcAddr((VkInstance)VK_NULL_HANDLE, name);
}

UK_TESTCASE(libvulkan, static_proc_lookup)
{
	UK_TEST_EXPECT_NOT_NULL(test_get_proc("vkCreateInstance"));
	UK_TEST_EXPECT_NOT_NULL(test_get_proc("vkEnumeratePhysicalDevices"));
	UK_TEST_EXPECT_NOT_NULL(test_get_proc("vkCreateDevice"));
	UK_TEST_EXPECT_NOT_NULL(
		vkGetDeviceProcAddr((VkDevice)VK_NULL_HANDLE, "vkCmdDispatch"));
	UK_TEST_EXPECT_NULL(test_get_proc("vkDoesNotExist"));
	UK_TEST_EXPECT_NULL(
		vkGetDeviceProcAddr((VkDevice)VK_NULL_HANDLE, "vkNoSuchFn"));
	UK_TEST_EXPECT_NOT_NULL(uk_vulkan_get_instance_proc_addr_fn());
}

UK_TESTCASE(libvulkan, info_surface_defaults)
{
	struct uk_vulkan_info info = { 0 };

	uk_vulkan_get_info(&info);
	UK_TEST_EXPECT_ZERO(info.initialized);
	UK_TEST_EXPECT_ZERO(strcmp(info.driver_name, "venus"));
	UK_TEST_EXPECT_ZERO(strcmp(info.claim_boundary, "ggml compute subset"));
	UK_TEST_EXPECT_SNUM_GT((long)info.supported_command_count, 0L);
	UK_TEST_EXPECT_ZERO(info.batch_enabled);
	UK_TEST_EXPECT_ZERO(info.hostmem_fixed);
}

uk_testsuite_register(libvulkan, NULL);
