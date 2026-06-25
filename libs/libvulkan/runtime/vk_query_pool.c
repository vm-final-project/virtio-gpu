#include "vk_internal.h"

VkResult stub_vkCreateQueryPool(VkDevice dev, const void *ci, const void *a,
                                        VkQueryPool *pPool)
{
    (void)dev; (void)ci; (void)a;
    *pPool = (VkQueryPool)uk_vk_alloc_handle();
    return VK_SUCCESS;
}
void stub_vkDestroyQueryPool(VkDevice d, VkQueryPool p, const void *a)
{
    (void)d; (void)p; (void)a;
}
VkResult stub_vkGetQueryPoolResults(VkDevice d, VkQueryPool p, uint32_t f,
                                            uint32_t cnt, size_t sz, void *data,
                                            VkDeviceSize stride, uint32_t flags)
{
    (void)d;(void)p;(void)f;(void)cnt;(void)stride;(void)flags;
    if (data && sz > 0) memset(data, 0, sz);
    return VK_NOT_READY;
}
void stub_vkCmdResetQueryPool(VkCommandBuffer cb, VkQueryPool p,
                                      uint32_t f, uint32_t c) { (void)cb;(void)p;(void)f;(void)c; }
void stub_vkCmdWriteTimestamp(VkCommandBuffer cb, uint32_t stage,
                                      VkQueryPool p, uint32_t q) { (void)cb;(void)stage;(void)p;(void)q; }
void stub_vkCmdBeginQuery(VkCommandBuffer cb, VkQueryPool p, uint32_t q,
                                   uint32_t f) { (void)cb;(void)p;(void)q;(void)f; }
void stub_vkCmdEndQuery(VkCommandBuffer cb, VkQueryPool p, uint32_t q)
{
    (void)cb;(void)p;(void)q;
}

/* KHR aliases — same signature as the non-KHR version (all return void) */
