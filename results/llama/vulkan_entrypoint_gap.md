# Vulkan entry-point gap analysis

Generated: 2026-05-22T08:00:49Z
ggml-vulkan.cpp: `/home/jerrytsai/llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp`
dispatch table: `libs/libukggml_vulkan/uk_vulkan_dispatch.c`

**Total needed by ggml: 41**
**Covered: 41**
**Gap: 0**

| Function | Covered | Note |
|----------|---------|------|
| `vkAllocateCommandBuffers` | ✓ | |
| `vkAllocateDescriptorSets` | ✓ | |
| `vkAllocateMemory` | ✓ | |
| `vkBindBufferMemory` | ✓ | |
| `vkCmdCopyBuffer` | ✓ | |
| `vkCreateBuffer` | ✓ | |
| `vkCreateCommandPool` | ✓ | |
| `vkCreateDescriptorPool` | ✓ | |
| `vkCreateDescriptorSetLayout` | ✓ | |
| `vkCreateDevice` | ✓ | |
| `vkCreateEvent` | ✓ | |
| `vkCreateFence` | ✓ | |
| `vkCreatePipelineLayout` | ✓ | |
| `vkCreateQueryPool` | ✓ | |
| `vkCreateSemaphore` | ✓ | |
| `vkCreateShaderModule` | ✓ | |
| `vkDestroyBuffer` | ✓ | |
| `vkDestroyCommandPool` | ✓ | |
| `vkDestroyDescriptorPool` | ✓ | |
| `vkDestroyDescriptorSetLayout` | ✓ | |
| `vkDestroyEvent` | ✓ | |
| `vkDestroyFence` | ✓ | |
| `vkDestroyPipeline` | ✓ | |
| `vkDestroyPipelineLayout` | ✓ | |
| `vkDestroyQueryPool` | ✓ | |
| `vkDestroySemaphore` | ✓ | |
| `vkDestroyShaderModule` | ✓ | |
| `vkEnumerateDeviceExtensionProperties` | ✓ | |
| `vkEnumeratePhysicalDevices` | ✓ | |
| `vkFreeMemory` | ✓ | |
| `vkGetBufferMemoryRequirements` | ✓ | |
| `vkGetFenceStatus` | ✓ | |
| `vkGetMemoryHostPointerPropertiesEXT` | ✓ | |
| `vkGetPhysicalDeviceFeatures2` | ✓ | |
| `vkGetPhysicalDeviceQueueFamilyProperties` | ✓ | |
| `vkMapMemory` | ✓ | |
| `vkQueueSubmit` | ✓ | |
| `vkResetCommandPool` | ✓ | |
| `vkResetFences` | ✓ | |
| `vkUpdateDescriptorSets` | ✓ | |
| `vkWaitForFences` | ✓ | |
