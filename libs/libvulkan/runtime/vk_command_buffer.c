#include "vk_internal.h"

VkResult stub_vkCreateCommandPool(VkDevice dev, const void *ci,
                                         const void *alloc, VkCommandPool *pPool)
{
    (void)dev; (void)ci; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateCommandPool(&_enc, UK_H_DEVICE, h, 0u);
    UK_ENC_SUBMIT();
    *pPool = (VkCommandPool)h;
    return VK_SUCCESS;
}

void stub_vkDestroyCommandPool(VkDevice d, VkCommandPool p, uint32_t f)
{ (void)d; (void)f; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyCommandPool, (uint64_t)p); }

VkResult stub_vkAllocateCommandBuffers(VkDevice dev, const void *ai,
                                               VkCommandBuffer *pCBs)
{
    (void)dev;
    /* VkCommandBufferAllocateInfo layout (Vulkan 1.3):
     *   sType     (uint32)  offset 0
     *   [pad 4]
     *   pNext     (uint64)  offset 8
     *   commandPool (uint64) offset 16
     *   level     (uint32)  offset 24
     *   commandBufferCount (uint32) offset 28
     */
    const uint8_t *p8 = (const uint8_t *)ai;
    uint64_t pool_h = *((const uint64_t *)(p8 + 16)); /* commandPool */
    uint32_t count  = *((const uint32_t *)(p8 + 28)); /* commandBufferCount */
    if (!count) count = 1; /* guard against malformed AI */
    for (uint32_t i = 0; i < count; i++) {
        uint64_t h = uk_vk_alloc_handle();
        UK_ENC_BEGIN();
        uint64_t cb[1] = { h };
        uk_venus_encode_vkAllocateCommandBuffers(&_enc, UK_H_DEVICE, pool_h, 1u, cb);
        UK_ENC_SUBMIT();
        pCBs[i] = (VkCommandBuffer)h;
    }
    return VK_SUCCESS;
}

void stub_vkFreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n,
                                      const VkCommandBuffer *cbs)
{
    (void)d;
    UK_ENC_BEGIN();
    uk_venus_encode_vkFreeCommandBuffers(&_enc, UK_H_DEVICE, (uint64_t)p, n,
                                         (const uint64_t *)cbs);
    UK_ENC_SUBMIT();
}

VkResult stub_vkBeginCommandBuffer(VkCommandBuffer cb, const void *bi)
{
    (void)bi;
    /* Lazily bring up the Venus ring now that the device exists. */
    uk_dispatch_ring_lazy_init();
    if (g_ring_enabled && g_ring_ready) {
        /* Ring mode: open ring write window; Begin command goes into ring. */
        g_ring_recording = 1;
    } else if (g_batch_enabled) {
        uk_venus_encoder_init(&g_batch_enc, g_batch_buf, UK_DISPATCH_BUF_SIZE);
        g_batch_recording = 1;
    }
    UK_ENC_BEGIN();
    uk_venus_encode_vkBeginCommandBuffer(&_enc, (uint64_t)cb);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

VkResult stub_vkEndCommandBuffer(VkCommandBuffer cb)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkEndCommandBuffer(&_enc, (uint64_t)cb);
    UK_ENC_SUBMIT();
    if (g_ring_enabled && g_ring_ready && g_ring_recording) {
        /* Ring mode: flush tail to shared memory + notify host if idle.
         * QueueSubmit will append its command and do the final flush. */
        uk_venus_ring_cmd_flush(g_vk.gpu, &g_ring);
        g_ring_recording = 0;
    } else if (g_batch_enabled && g_batch_recording) {
        /* Batch (SUBMIT_3D) fallback: flush in one shot. */
        uk_venus_submit(g_vk.gpu, g_vk.ctx, &g_batch_enc);
        g_batch_recording = 0;
    }
    return VK_SUCCESS;
}

VkResult stub_vkResetCommandBuffer(VkCommandBuffer cb, uint32_t flags)
{
    (void)cb; (void)flags; return VK_SUCCESS;
}

VkResult stub_vkResetCommandPool(VkDevice d, VkCommandPool pool, uint32_t flags)
{
    (void)d; (void)pool; (void)flags; return VK_SUCCESS;
}

/* vkGetMemoryHostPointerPropertiesEXT is not in the Venus protocol subset;
 * return all host-visible memory types as compatible. */
void stub_vkCmdBindPipeline(VkCommandBuffer cb, uint32_t bindPoint,
                                    VkPipeline pipeline)
{
    (void)bindPoint;
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdBindPipeline(&_enc, (uint64_t)cb, (uint64_t)pipeline);
    UK_ENC_SUBMIT();
}

void stub_vkCmdBindDescriptorSets(VkCommandBuffer cb, uint32_t bindPoint,
                                          VkPipelineLayout layout, uint32_t firstSet,
                                          uint32_t dsCount, const VkDescriptorSet *pDS,
                                          uint32_t dynCount, const uint32_t *pDynOffsets)
{
    (void)bindPoint; (void)firstSet; (void)dynCount; (void)pDynOffsets;
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdBindDescriptorSets(&_enc, (uint64_t)cb, (uint64_t)layout,
                                             0u, dsCount,
                                             (const uint64_t *)pDS);
    UK_ENC_SUBMIT();
}

void stub_vkCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout,
                                     uint32_t stageFlags, uint32_t offset,
                                     uint32_t size, const void *pValues)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdPushConstants(&_enc, (uint64_t)cb, (uint64_t)layout,
                                        stageFlags, offset, size, pValues);
    UK_ENC_SUBMIT();
}

void stub_vkCmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdDispatch(&_enc, (uint64_t)cb, x, y, z);
    UK_ENC_SUBMIT();
}

void stub_vkCmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst,
                                  uint32_t regionCount, const void *pRegions)
{
    const uint8_t *r = (const uint8_t *)pRegions;
    for (uint32_t i = 0; i < regionCount; i++, r += SIZE_BCOPY) {
        uint64_t size = rd_u64(r, OFF_BCOPY_SIZE);
        UK_ENC_BEGIN();
        uk_venus_encode_vkCmdCopyBuffer(&_enc, (uint64_t)cb, (uint64_t)src,
                                        (uint64_t)dst, size);
        UK_ENC_SUBMIT();
    }
}

void stub_vkCmdFillBuffer(VkCommandBuffer cb, VkBuffer buf,
                                  VkDeviceSize offset, VkDeviceSize size, uint32_t data)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdFillBuffer(&_enc, (uint64_t)cb, (uint64_t)buf,
                                    offset, size, data);
    UK_ENC_SUBMIT();
}

void stub_vkCmdPipelineBarrier(VkCommandBuffer cb, uint32_t srcStage,
                                       uint32_t dstStage, uint32_t depFlags,
                                       uint32_t memBarCount, const void *pMemBars,
                                       uint32_t bufBarCount, const void *pBufBars,
                                       uint32_t imgBarCount, const void *pImgBars)
{
    (void)depFlags; (void)memBarCount; (void)pMemBars;
    (void)bufBarCount; (void)pBufBars; (void)imgBarCount; (void)pImgBars;
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdPipelineBarrier(&_enc, (uint64_t)cb, srcStage, dstStage);
    UK_ENC_SUBMIT();
}

void stub_vkCmdSetEvent(VkCommandBuffer cb, VkEvent ev, uint32_t stageMask)
{ (void)cb; (void)ev; (void)stageMask; }
void stub_vkCmdResetEvent(VkCommandBuffer cb, VkEvent ev, uint32_t stageMask)
{ (void)cb; (void)ev; (void)stageMask; }
void stub_vkCmdWaitEvents(VkCommandBuffer cb, uint32_t evCount, const void *pEvents,
                                 uint32_t srcStage, uint32_t dstStage,
                                 uint32_t mbc, const void *mb, uint32_t bbc,
                                 const void *bb, uint32_t ibc, const void *ib)
{ (void)cb; (void)evCount; (void)pEvents; (void)srcStage; (void)dstStage;
  (void)mbc; (void)mb; (void)bbc; (void)bb; (void)ibc; (void)ib; }
void stub_vkCmdPipelineBarrier2(VkCommandBuffer cb, const void *pDependencyInfo)
{ (void)cb; (void)pDependencyInfo; }
void stub_vkCmdCopyBuffer2(VkCommandBuffer cb, const void *pCopyBufferInfo)
{ (void)cb; (void)pCopyBufferInfo; }
