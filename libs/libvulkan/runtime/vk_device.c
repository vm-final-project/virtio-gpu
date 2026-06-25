#include "vk_internal.h"

VkResult stub_vkCreateDevice(VkPhysicalDevice physdev, const void *ci,
                                    const void *alloc, VkDevice *pDevice)
{
    (void)physdev; (void)ci; (void)alloc;
    /* The Venus device object (UK_H_DEVICE) is a singleton: it must be created
     * on the host EXACTLY ONCE. A duplicate id is fatal to the host context
     * (vkr_context_validate_object_id), so guard against repeated creation. */
    *pDevice = (VkDevice)UK_H_DEVICE;
    if (g_vk.device_created)
        return VK_SUCCESS;
    g_vk.device_created = 1;

    if (g_vk.gpu && g_vk.ctx) {
        /* Create the device and confirm the host accepted it (reply round-trip):
         * the host only registers the device object on VK_SUCCESS. */
        int32_t vkres = -1;
        uk_venus_create_device_checked(g_vk.gpu, g_vk.ctx, UK_H_PHYSDEV,
                                       UK_H_DEVICE, 0u, &vkres);
        /* The Venus host requires vkGetDeviceQueue2 with a
         * VkDeviceQueueTimelineInfoMESA (ringIdx) — the legacy vkGetDeviceQueue
         * fatally tears down the context. */
        UK_ENC_BEGIN();
        uk_venus_encode_vkGetDeviceQueue2(&_enc, UK_H_DEVICE, 0u, 0u, 1u, UK_H_QUEUE);
        UK_ENC_SUBMIT();
        return VK_SUCCESS;
    }
    /* Fallback: fire-and-forget create + queue when no query path is ready. */
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateDevice(&_enc, UK_H_PHYSDEV, UK_H_DEVICE, 0u, 1.0f, 0, (const char **)0);
    uk_venus_encode_vkGetDeviceQueue(&_enc, UK_H_DEVICE, 0u, 0u, UK_H_QUEUE);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

void stub_vkDestroyDevice(VkDevice d, const void *a) { (void)d; (void)a; }

void stub_vkGetDeviceQueue(VkDevice d, uint32_t fi, uint32_t qi, VkQueue *q)
{
    (void)d; (void)fi; (void)qi;
    *q = (VkQueue)UK_H_QUEUE;
}

/* Host-visible memory backing (Venus import-resource path).
 *
 * ggml stages model weights / IO through HOST_VISIBLE memory then copies to
 * device-local buffers. For real Venus those bytes must reach the host GPU, so a
 * host-visible VkDeviceMemory is backed by a host-visible virtio-gpu blob
 * (VkImportMemoryResourceInfoMESA): the guest maps the blob and writes into it,
 * and the host's VkDeviceMemory aliases the same shmem. Device-local memory
 * keeps the plain fire-and-forget alloc (host VRAM, never mapped by the guest). */
#define UK_HV_MEM_MAX 128
static struct uk_hv_mem {
    uint64_t handle;
    struct uk_virtio_gpu_blob blob;
    uint8_t  used;
} g_hv_mem[UK_HV_MEM_MAX];

/* A memory type is host-visible if its propertyFlags carries
 * VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT (0x2). With real properties we read the
 * real type's flags; otherwise the fabricated layout makes types 1 and 2
 * host-visible. */
static inline int uk_mem_type_host_visible(uint32_t idx)
{
    if (g_memprops_valid && idx < 32u) {
        uint32_t flags = *(uint32_t *)(g_memprops + 4 + idx * 8);
        return (flags & 0x2u) != 0u;
    }
    return idx == 1u || idx == 2u;
}

static struct uk_hv_mem *uk_hv_mem_find(uint64_t handle)
{
    for (int i = 0; i < UK_HV_MEM_MAX; i++)
        if (g_hv_mem[i].used && g_hv_mem[i].handle == handle)
            return &g_hv_mem[i];
    return NULL;
}

/* VkBuffer handle -> requested size, so vkGetBufferMemoryRequirements can report
 * the real size without a Venus round-trip. */
#define UK_BUF_SIZE_MAX 1024
static struct { uint64_t handle; uint64_t size; } g_buf_size[UK_BUF_SIZE_MAX];
static int g_buf_size_next;
static void uk_buf_size_put(uint64_t handle, uint64_t size)
{
    int slot = g_buf_size_next++ % UK_BUF_SIZE_MAX;
    g_buf_size[slot].handle = handle;
    g_buf_size[slot].size = size;
}
static uint64_t uk_buf_size_find(uint64_t handle)
{
    for (int i = 0; i < UK_BUF_SIZE_MAX; i++)
        if (g_buf_size[i].handle == handle)
            return g_buf_size[i].size;
    return 0;
}

VkResult stub_vkDeviceWaitIdle(VkDevice d) { (void)d; return VK_SUCCESS; }

