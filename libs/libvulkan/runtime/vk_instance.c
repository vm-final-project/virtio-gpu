#include "vk_internal.h"

VkResult stub_vkCreateInstance(const void *ci, const void *alloc,
                                      VkInstance *pInstance)
{
    (void)ci; (void)alloc;
    if (uk_vulkan_init() != 0)
        return VK_ERROR_DEVICE_LOST;
    /* Send Venus bootstrap: Instance + EnumeratePhysicalDevices + Device */
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateInstance(&_enc, UK_H_INSTANCE,
                                     "ggml-vulkan-uk", 0x00402000u,
                                     0, (const char **)0);
    UK_ENC_SUBMIT();
    *pInstance = (VkInstance)UK_H_INSTANCE;
    return VK_SUCCESS;
}

VkResult stub_vkDestroyInstance(VkInstance i, const void *a)
{
    (void)i; (void)a; return VK_SUCCESS;
}

VkResult stub_vkEnumerateInstanceLayerProperties(uint32_t *pCount, void *p)
{
    (void)p; *pCount = 0; return VK_SUCCESS;
}

/* vkEnumerateInstanceVersion — required by Vulkan-Hpp 1.1+ dispatcher init.
 * Returns VK 1.3.0 so ggml-vulkan's "Vulkan 1.2 required" check passes. */
VkResult stub_vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    if (pApiVersion)
        *pApiVersion = 0x00403000u; /* VK_MAKE_API_VERSION(0,1,3,0) */
    return VK_SUCCESS;
}

VkResult stub_vkEnumerateInstanceExtensionProperties(const char *l,
                                                             uint32_t *pCount,
                                                             void *p)
{
    (void)l; (void)p; *pCount = 0; return VK_SUCCESS;
}

