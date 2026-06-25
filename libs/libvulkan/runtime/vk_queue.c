#include "vk_internal.h"

VkResult stub_vkQueueSubmit(VkQueue queue, uint32_t submitCount,
                                    const void *pSubmits, VkFence fence)
{
    (void)submitCount; (void)pSubmits;
    const uint8_t *s = (const uint8_t *)pSubmits;
    for (uint32_t i = 0; i < submitCount; i++) {
        uint32_t cbCount = rd_u32(s, OFF_SUBMIT_CMD_COUNT);
        const uint64_t *cbs = (const uint64_t *)rd_ptr(s, OFF_SUBMIT_CMDS);
        UK_ENC_BEGIN();
        uk_venus_encode_vkQueueSubmit(&_enc, UK_H_QUEUE, cbCount, cbs, (uint64_t)fence);
        UK_ENC_SUBMIT();
        s += SIZE_SUBMIT_INFO;
    }
    /* Track the fence for WaitForFences polling. */
    if (fence)
        g_last_fence = (uk_gpu_fence_id)(uintptr_t)fence;
    if (g_ring_enabled && g_ring_ready) {
        /* Ring mode: one final flush after QueueSubmit command is written.
         * This is the single virtqueue kick for the entire Begin→End→Submit
         * sequence (3 kicks → 1). */
        uk_venus_ring_cmd_flush(g_vk.gpu, &g_ring);
    }
    return VK_SUCCESS;
}

VkResult stub_vkQueueWaitIdle(VkQueue q) { (void)q; return VK_SUCCESS; }
VkResult stub_vkQueueSubmit2(VkQueue q, uint32_t count, const void *pSubmits,
                                    VkFence fence)
{ (void)q; (void)count; (void)pSubmits; (void)fence; return VK_SUCCESS; }
/* Coherent host-visible memory: flush/invalidate are no-ops. */
