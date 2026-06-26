
### run `find_used_vulkan_apis.py`

### command

```base
python3 scripts/find_used_vulkan_apis.py > scripts/vulkan/command.md
```

#### result
```base

Registry : /Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../mesa/src/vulkan/runtime/registry/vk.xml
Registry : 857 unique Vulkan commands
Source   : /Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp

Found 81 Vulkan commands used in source:

Vulkan C Command                         | Occurrences
-------------------------------------------------------
vkGetPhysicalDeviceProperties2           | 11
vkResetFences                            | 10
vkCmdCopyBuffer                          | 9
vkWaitForFences                          | 9
vkGetInstanceProcAddr                    | 8
vkEnumerateDeviceExtensionProperties     | 7
vkEnumeratePhysicalDevices               | 7
vkResetCommandPool                       | 7
vkDestroyBuffer                          | 6
vkDestroyDevice                          | 5
vkDestroyInstance                        | 5
vkCreateSemaphore                        | 4
vkDestroyEvent                           | 4
vkDestroySemaphore                       | 4
vkGetPhysicalDeviceProperties            | 4
vkQueueSubmit                            | 4
vkCmdResetEvent                          | 3
vkCmdWriteTimestamp                      | 3
vkCreateFence                            | 3
vkDestroyFence                           | 3
vkGetPhysicalDeviceFeatures2             | 3
vkResetEvent                             | 3
vkAllocateMemory                         | 2
vkCmdFillBuffer                          | 2
vkCreateEvent                            | 2
vkEnumerateInstanceExtensionProperties   | 2
vkFreeMemory                             | 2
vkGetPhysicalDeviceMemoryProperties      | 2
vkBeginCommandBuffer                     | 2
vkEndCommandBuffer                       | 2
vkResetCommandBuffer                     | 2
vkAllocateCommandBuffers                 | 1
vkAllocateDescriptorSets                 | 1
vkBindBufferMemory                       | 1
vkCmdBeginDebugUtilsLabelEXT             | 1
vkCmdBindDescriptorSets                  | 1
vkCmdBindPipeline                        | 1
vkCmdDispatch                            | 1
vkCmdEndDebugUtilsLabelEXT               | 1
vkCmdInsertDebugUtilsLabelEXT            | 1
vkCmdPipelineBarrier                     | 1
vkCmdPushConstants                       | 1
vkCmdResetQueryPool                      | 1
vkCmdSetEvent                            | 1
vkCmdWaitEvents                          | 1
vkCreateBuffer                           | 1
vkCreateCommandPool                      | 1
vkCreateDescriptorPool                   | 1
vkCreateDescriptorSetLayout              | 1
vkCreateDevice                           | 1
vkCreateInstance                         | 1
vkCreatePipelineLayout                   | 1
vkCreateQueryPool                        | 1
vkCreateShaderModule                     | 1
vkDestroyCommandPool                     | 1
vkDestroyDescriptorPool                  | 1
vkDestroyDescriptorSetLayout             | 1
vkDestroyPipeline                        | 1
vkDestroyPipelineLayout                  | 1
vkDestroyQueryPool                       | 1
vkDestroyShaderModule                    | 1
vkEnumerateInstanceLayerProperties       | 1
vkEnumerateInstanceVersion               | 1
vkGetBufferMemoryRequirements            | 1
vkGetFenceStatus                         | 1
vkGetMemoryHostPointerPropertiesEXT      | 1
vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV | 1
vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR | 1
vkGetPhysicalDeviceFeatures              | 1
vkGetPhysicalDeviceMemoryProperties2     | 1
vkGetPhysicalDeviceQueueFamilyProperties | 1
vkGetPipelineExecutableStatisticsKHR     | 1
vkGetQueryPoolResults                    | 1
vkMapMemory                              | 1
vkQueueBeginDebugUtilsLabelEXT           | 1
vkQueueEndDebugUtilsLabelEXT             | 1
vkResetQueryPool                         | 1
vkSetDebugUtilsObjectNameEXT             | 1
vkSetEvent                               | 1
vkUpdateDescriptorSets                   | 1
vkWaitSemaphores                         | 1
```

### run finding command bash


#### 找所有 #include <vulkan/...>

```bash
> grep -rn "vulkan/" $LLAMA_ROOT/ggml/src/
/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-virtgpu/virtgpu.h:44:/* from src/virtio/vulkan/vn_renderer_virtgpu.c */
/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp:2:#include <vulkan/vulkan_core.h>
/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp:22:#include <vulkan/vulkan.hpp>
/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp:6154:                    // Check https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDriverId.html for the list of driver id
```

#### 找用了 Vulkan-Hpp 的 .cpp

1. `vulkan.hpp`
```bash
> grep -rl "vulkan.hpp" $LLAMA_ROOT/ggml/src/
/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp
```

2. `vk::`
```bash
> grep -rl "vk::" $LLAMA_ROOT/ggml/src/
/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp
```

#### 找 GGML_USE_VULKAN 的 guard

```bash
> grep -rn "GGML_USE_VULKAN" $LLAMA_ROOT/ggml/src/
/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-backend-reg.cpp:45:#ifdef GGML_USE_VULKAN
/Users/caichaowei/NTU/114_2/virtual-machine/final-project/virtio-gpu/../llama.cpp/ggml/src/ggml-backend-reg.cpp:125:#ifdef GGML_USE_VULKAN
```

