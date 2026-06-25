#include "vk_internal.h"

VkResult stub_vkCreateFence(VkDevice dev, const void *ci, const void *alloc,
                                    VkFence *pFence)
{
    (void)dev; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    int signaled = 0;
    if (ci) {
        uint32_t flags = rd_u32(ci, 16u);
        signaled = (flags & 1) ? 1 : 0;
    }
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateFence(&_enc, UK_H_DEVICE, h, signaled);
    UK_ENC_SUBMIT();
    *pFence = (VkFence)h;
    return VK_SUCCESS;
}

void stub_vkDestroyFence(VkDevice d, VkFence f, const void *a)
{
    (void)d; (void)f; (void)a;
}

VkResult stub_vkResetFences(VkDevice dev, uint32_t count, const VkFence *pFences)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkResetFences(&_enc, UK_H_DEVICE, count,
                                   (const uint64_t *)pFences);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

VkResult stub_vkWaitForFences(VkDevice dev, uint32_t count,
                                      const VkFence *pFences, uint32_t waitAll,
                                      uint64_t timeout)
{
    (void)dev; (void)count; (void)pFences; (void)waitAll;
    /* Opt: poll completed_fence instead of sending a Venus round-trip.
     * cmd_submit_locked() already updates completed_fence synchronously when
     * the host returns the response; so by the time QueueSubmit returns the
     * fence is already marked done — no Venus vkWaitForFences command needed. */
    if (g_vk.gpu) {
        int rc = uk_virtio_gpu_fence_wait(g_vk.gpu, g_last_fence,
                                           timeout == UINT64_MAX ? 5000000000ull
                                                                  : timeout);
        (void)rc;
    }
    return VK_SUCCESS;
}

VkResult stub_vkGetFenceStatus(VkDevice d, VkFence f)
{
    (void)d; (void)f; return VK_SUCCESS;
}

VkResult stub_vkCreateEvent(VkDevice dev, const void *ci, const void *a,
                                    VkEvent *pEvent)
{
    (void)dev; (void)ci; (void)a;
    *pEvent = (VkEvent)uk_vk_alloc_handle();
    return VK_SUCCESS;
}
void     stub_vkDestroyEvent(VkDevice d, VkEvent e, const void *a) { (void)d;(void)e;(void)a; }
VkResult stub_vkGetEventStatus(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_SUCCESS; }
VkResult stub_vkSetEvent(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_SUCCESS; }
VkResult stub_vkResetEvent(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_SUCCESS; }

VkResult stub_vkCreateSemaphore(VkDevice dev, const void *ci, const void *a,
                                        VkSemaphore *pSem)
{
    (void)dev; (void)ci; (void)a;
    *pSem = (VkSemaphore)uk_vk_alloc_handle();
    return VK_SUCCESS;
}
void stub_vkDestroySemaphore(VkDevice d, VkSemaphore s, const void *a)
{
    (void)d; (void)s; (void)a;
}

/* Timeline semaphore ops. ggml's async-upload event path
 * (ggml_backend_vk_event_*) records vkCmdCopyBuffer into a transfer command
 * buffer and submits it, then waits on a timeline semaphore. Our SUBMIT_3D
 * queue submit is synchronous (fence-waited), so by the time these are called
 * the GPU work has completed — the timeline has already reached its target. */
VkResult stub_vkWaitSemaphores(VkDevice dev, const void *pWaitInfo,
                                      uint64_t timeout)
{
    (void)dev; (void)pWaitInfo; (void)timeout;
    return VK_SUCCESS;
}
VkResult stub_vkSignalSemaphore(VkDevice dev, const void *pSignalInfo)
{
    (void)dev; (void)pSignalInfo;
    return VK_SUCCESS;
}
VkResult stub_vkGetSemaphoreCounterValue(VkDevice dev, VkSemaphore sem,
                                                uint64_t *pValue)
{
    (void)dev; (void)sem;
    /* Report a large monotonically-satisfied value so any timeline wait the
     * guest polls is already satisfied. */
    if (pValue)
        *pValue = ~0ull >> 1;
    return VK_SUCCESS;
}

/* Command-buffer event/barrier ops used by ggml's async-upload path
 * (resetEvent + copyBuffer + setEvent). Our queue submit is synchronous and
 * the timeline wait is satisfied immediately, so GPU-side event signalling and
 * the synchronization2 barriers are no-ops; the actual data movement is the
 * vkCmdCopyBuffer that IS encoded. */
