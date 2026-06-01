# Vulkan registry check

```json
{
  "status": "pass",
  "written_at": "2026-05-31T17:35:32Z",
  "vulkan_docs_xml": "/mydata/JerryT/Vulkan-Docs/xml/vk.xml",
  "mesa_venus_defines": "/mydata/JerryT/mesa/src/virtio/venus-protocol/vn_protocol_driver_defines.h",
  "venus_protocol_xml": "/mydata/JerryT/venus-protocol/xmls/VK_EXT_command_serialization.xml",
  "header": "libs/libukvenus/include/uk/venus.h",
  "commands": {
    "vkCreateInstance": "VN_CMD_vkCreateInstance",
    "vkDestroyInstance": "VN_CMD_vkDestroyInstance",
    "vkEnumeratePhysicalDevices": "VN_CMD_vkEnumeratePhysicalDevices",
    "vkGetPhysicalDeviceProperties": "VN_CMD_vkGetPhysicalDeviceProperties",
    "vkCreateDevice": "VN_CMD_vkCreateDevice",
    "vkDestroyDevice": "VN_CMD_vkDestroyDevice",
    "vkGetDeviceQueue": "VN_CMD_vkGetDeviceQueue",
    "vkQueueSubmit": "VN_CMD_vkQueueSubmit",
    "vkAllocateMemory": "VN_CMD_vkAllocateMemory"
  },
  "errors": [],
  "claim_allowed": "VOGUE Venus command ids match Khronos Vulkan-Docs command existence and Mesa/venus-protocol VkCommandTypeEXT values.",
  "claim_forbidden": "This registry check does not prove Vulkan conformance or rendering."
}
```
