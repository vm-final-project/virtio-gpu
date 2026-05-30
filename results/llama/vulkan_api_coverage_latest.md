# ggml-vulkan Vulkan API coverage

Status: **pass**

| API | Status | Required? |
|---|---:|---:|
| `vkAllocateCommandBuffers` | supported | true |
| `vkAllocateDescriptorSets` | supported | true |
| `vkAllocateMemory` | supported | true |
| `vkBindBufferMemory` | supported | true |
| `vkCmdBeginDebugUtilsLabelEXT` | optional-missing | false |
| `vkCmdBindDescriptorSets` | supported | true |
| `vkCmdBindPipeline` | supported | true |
| `vkCmdCopyBuffer` | supported | true |
| `vkCmdDispatch` | supported | true |
| `vkCmdEndDebugUtilsLabelEXT` | optional-missing | false |
| `vkCmdFillBuffer` | supported | true |
| `vkCmdInsertDebugUtilsLabelEXT` | optional-missing | false |
| `vkCmdPipelineBarrier` | supported | true |
| `vkCmdResetQueryPool` | supported | true |
| `vkCreateBuffer` | supported | true |
| `vkCreateCommandPool` | supported | true |
| `vkCreateComputePipelines` | supported | true |
| `vkCreateDescriptorPool` | supported | true |
| `vkCreateDescriptorSetLayout` | supported | true |
| `vkCreateDevice` | supported | true |
| `vkCreateEvent` | supported | true |
| `vkCreateFence` | supported | true |
| `vkCreatePipelineLayout` | supported | true |
| `vkCreateQueryPool` | supported | true |
| `vkCreateSemaphore` | supported | true |
| `vkCreateShaderModule` | supported | true |
| `vkDestroyBuffer` | supported | true |
| `vkDestroyCommandPool` | supported | true |
| `vkDestroyDescriptorPool` | supported | true |
| `vkDestroyDescriptorSetLayout` | supported | true |
| `vkDestroyDevice` | supported | true |
| `vkDestroyEvent` | supported | true |
| `vkDestroyFence` | supported | true |
| `vkDestroyPipeline` | supported | true |
| `vkDestroyPipelineLayout` | supported | true |
| `vkDestroyQueryPool` | supported | true |
| `vkDestroySemaphore` | supported | true |
| `vkDestroyShaderModule` | supported | true |
| `vkEnumerateDeviceExtensionProperties` | supported | true |
| `vkEnumeratePhysicalDevices` | supported | true |
| `vkFreeMemory` | supported | true |
| `vkGetBufferDeviceAddress` | supported | true |
| `vkGetBufferMemoryRequirements` | supported | true |
| `vkGetDeviceQueue` | supported | true |
| `vkGetFenceStatus` | supported | true |
| `vkGetInstanceProcAddr` | supported | true |
| `vkGetMemoryHostPointerPropertiesEXT` | supported | true |
| `vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV` | optional-missing | false |
| `vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR` | optional-missing | false |
| `vkGetPhysicalDeviceFeatures2` | supported | true |
| `vkGetPhysicalDeviceMemoryProperties` | supported | true |
| `vkGetPhysicalDeviceMemoryProperties2` | supported | true |
| `vkGetPhysicalDeviceProperties` | supported | true |
| `vkGetPhysicalDeviceProperties2` | supported | true |
| `vkGetPhysicalDeviceQueueFamilyProperties` | supported | true |
| `vkGetQueryPoolResults` | supported | true |
| `vkMapMemory` | supported | true |
| `vkQueueBeginDebugUtilsLabelEXT` | optional-missing | false |
| `vkQueueEndDebugUtilsLabelEXT` | optional-missing | false |
| `vkQueueSubmit` | supported | true |
| `vkResetCommandPool` | supported | true |
| `vkResetEvent` | supported | true |
| `vkResetFences` | supported | true |
| `vkSetDebugUtilsObjectNameEXT` | optional-missing | false |
| `vkSetEvent` | supported | true |
| `vkUpdateDescriptorSets` | supported | true |
| `vkWaitForFences` | supported | true |

Claim boundary: Covers the pinned ggml-vulkan.cpp API surface, not general Vulkan conformance.
