#include "vk_internal.h"

VkResult stub_vkCreateBuffer(VkDevice dev, const void *ci,
                                    const void *alloc, VkBuffer *pBuf)
{
    (void)dev; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    /* VkBufferCreateInfo (Vulkan 1.3):
     *   sType       (uint32) offset 0
     *   [pad 4]
     *   pNext       (uint64) offset 8
     *   flags       (uint32) offset 16  [VkBufferCreateFlags]
     *   [pad 4]
     *   size        (uint64) offset 24  [VkDeviceSize]
     *   usage       (uint32) offset 32  [VkBufferUsageFlags]
     *   sharingMode (uint32) offset 36
     */
    uint64_t sz    = rd_u64(ci, OFF_BUF_SIZE);
    uint32_t usage = rd_u32(ci, OFF_BUF_USAGE);
    uk_buf_size_put(h, (sz + 0xFFFull) & ~0xFFFull);
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateBuffer(&_enc, UK_H_DEVICE, h, sz, usage);
    UK_ENC_SUBMIT();
    *pBuf = (VkBuffer)h;
    return VK_SUCCESS;
}

void stub_vkDestroyBuffer(VkDevice d, VkBuffer b, const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyBuffer, (uint64_t)b); }

/* VkMemoryRequirements for a buffer. Real Venus round-trip when possible (the
 * fixed fabricated size is too small for large model buffers and makes the host
 * reject bind); falls back to a conservative fabricated value. */
/* Memory-type mask a generic storage/transfer buffer may use: every real memory
 * type (ggml's find_memory_properties() then filters by required propertyFlags,
 * so listing all real types is safe — device-local reqs pick the device-local
 * types, host-visible reqs pick the host-visible ones). */
static uint32_t uk_buffer_memory_type_bits(void)
{
    if (g_memprops_valid) {
        uint32_t tc = *(uint32_t *)g_memprops;
        if (tc && tc <= 32u)
            return (tc >= 32u) ? 0xFFFFFFFFu : ((1u << tc) - 1u);
    }
    return 0x7u;
}

void stub_vkGetBufferMemoryRequirements(VkDevice dev, VkBuffer buf, void *pReq)
{
    (void)dev;
    if (!pReq) return;
    uint64_t *r = (uint64_t *)pReq;
    uint64_t host_size = 0, host_align = 0;
    uint32_t host_type_bits = 0;

    /* Prefer the host's real requirements through the Venus reply stream. If
     * this cannot complete, emit a blocker before falling back to tracked local
     * requirements so the runner cannot count the GPU workload as pass. */
    if (g_vk.gpu && g_vk.ctx && g_vk.device_created &&
        !uk_dispatch_batch_active() && !uk_dispatch_ring_active()) {
        if (uk_venus_query_buffer_requirements(g_vk.gpu, g_vk.ctx, UK_H_DEVICE,
                                               (uint64_t)buf, &host_size,
                                               &host_align,
                                               &host_type_bits) == 0 &&
            host_size && host_align && host_type_bits) {
            r[0] = host_size;
            r[1] = host_align;
            *((uint32_t *)(r + 2)) = host_type_bits;
            return;
        }
        uk_vk_report_blocked_once(&g_buffer_req_query_blocked_reported,
                                  "blocked:venus-query-buffer-requirements");
    }

    /* Diagnostic fallback: keep the process able to unwind using the tracked
     * buffer size and the best known memory-type mask. A preceding blocked
     * marker makes this non-acceptance evidence for llama-vk. */
    uk_vk_report_diag_once(&g_buffer_req_tracked_reported,
                           "buffer-memory-requirements=tracked-local");
    uint64_t sz = uk_buf_size_find((uint64_t)buf);
    r[0] = sz ? sz : (64ull * 1024ull * 1024ull);
    r[1] = 256u;
    *((uint32_t *)(r + 2)) = uk_buffer_memory_type_bits();
}

void stub_vkGetBufferMemoryRequirements2(VkDevice dev, const void *info,
                                                void *pReqs)
{
    (void)info;
    stub_vkGetBufferMemoryRequirements(dev, 0, (uint8_t *)pReqs + 16);
}

VkResult stub_vkBindBufferMemory(VkDevice dev, VkBuffer buf,
                                        VkDeviceMemory mem, VkDeviceSize offset)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkBindBufferMemory(&_enc, UK_H_DEVICE, (uint64_t)buf,
                                       (uint64_t)mem, (uint64_t)offset);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

VkDeviceAddress stub_vkGetBufferDeviceAddress(VkDevice dev, const void *info)
{
    (void)dev;
    /* Return the buffer handle as device address; Venus/host will resolve */
    const uint64_t *p = (const uint64_t *)info;
    return p[2]; /* VkBufferDeviceAddressInfo::buffer at offset 16 bytes */
}

void stub_vkGetBufferMemoryRequirements2KHR(VkDevice d, const void *i, void *r)
{ stub_vkGetBufferMemoryRequirements2(d, i, r); }
VkDeviceAddress stub_vkGetBufferDeviceAddressKHR(VkDevice d, const void *i)
{ return stub_vkGetBufferDeviceAddress(d, i); }

