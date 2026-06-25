#include "vk_internal.h"

VkResult stub_vkAllocateMemory(VkDevice dev, const void *ci,
                                      const void *alloc, VkDeviceMemory *pMem)
{
    (void)dev; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    /* VkMemoryAllocateInfo (Vulkan 1.3):
     *   sType            (uint32) offset 0
     *   [pad 4]
     *   pNext            (uint64) offset 8
     *   allocationSize   (uint64) offset 16
     *   memoryTypeIndex  (uint32) offset 24
     */
    uint64_t sz       = rd_u64(ci, OFF_MEM_ALLOC_SIZE);
    uint32_t mem_type = rd_u32(ci, OFF_MEM_ALLOC_TYPE);
    printf("VOGUE-DBG allocMem type=%u size=%lluMB hostvis=%d\n",
           mem_type, (unsigned long long)(sz >> 20),
           uk_mem_type_host_visible(mem_type));

    /* Host-visible types: expose the host VkDeviceMemory to the guest via a
     * HOST3D blob whose blob_id is the memory's Venus object id. The host
     * (virglrenderer vkr_context_get_blob) resolves blob_id -> VkDeviceMemory
     * and exports its mapping, so the guest's blob mapping aliases the host
     * memory the GPU uses. Order matters: allocate the memory (fence-synced)
     * BEFORE creating the blob that references it. Device-local memory keeps the
     * plain fire-and-forget alloc (host VRAM, never mapped by the guest). */
    if (uk_mem_type_host_visible(mem_type) && g_vk.gpu && g_vk.ctx
        && !uk_dispatch_batch_active() && sz) {
        struct uk_hv_mem *slot = NULL;
        for (int i = 0; i < UK_HV_MEM_MAX; i++)
            if (!g_hv_mem[i].used) { slot = &g_hv_mem[i]; break; }
        if (slot) {
            struct uk_virtio_gpu_blob *b = &slot->blob;
            uint64_t bsz = (sz + 0xFFFull) & ~0xFFFull; /* page-round */
            uint8_t  abuf[128];
            struct uk_venus_encoder aenc;
            uk_gpu_fence_id fence = 0;
            memset(b, 0, sizeof(*b));
            /* 1. allocate the host-visible VkDeviceMemory (object id = h) and
             *    fence-wait so the host registers it before the blob create. */
            uk_venus_encoder_init(&aenc, abuf, sizeof(abuf));
            uk_venus_encode_vkAllocateMemory(&aenc, UK_H_DEVICE, h, sz, mem_type);
            if (uk_virtio_gpu_gl_context_submit(g_vk.gpu, g_vk.ctx, aenc.buf, aenc.pos,
                                                &fence) == 0) {
                (void)uk_virtio_gpu_fence_wait(g_vk.gpu, fence, 2000000000ull);
                /* 2. export it: HOST3D blob with blob_id = memory id (h). */
                int rc_c = uk_virtio_gpu_gl_blob_create_with_ctx(g_vk.gpu, g_vk.ctx->id, bsz,
                        UK_VIRTIO_GPU_BLOB_MEM_HOST3D,
                        UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE, h, b);
                int rc_m = rc_c ? -1 : uk_virtio_gpu_gl_blob_map(g_vk.gpu, b);
                int rc_a = (rc_m || !b->mapped_addr) ? -1 :
                           uk_virtio_gpu_gl_context_attach_resource(g_vk.gpu, g_vk.ctx, b->resource_id);
                if (rc_c == 0 && rc_m == 0 && b->mapped_addr && rc_a == 0) {
                    slot->handle = h;
                    slot->used = 1;
                    *pMem = (VkDeviceMemory)h;
                    return VK_SUCCESS;
                }
            }
            /* Export failed: the memory id h is already allocated on the host;
             * tear the partial blob down and fall back (guest-local staging). */
            if (b->mapped)  uk_virtio_gpu_gl_blob_unmap(g_vk.gpu, b);
            if (b->created) uk_virtio_gpu_gl_blob_destroy(g_vk.gpu, b);
            memset(b, 0, sizeof(*b));
            *pMem = (VkDeviceMemory)h;
            return VK_SUCCESS;
        }
    }

    UK_ENC_BEGIN();
    uk_venus_encode_vkAllocateMemory(&_enc, UK_H_DEVICE, h, sz, mem_type);
    UK_ENC_SUBMIT();
    *pMem = (VkDeviceMemory)h;
    return VK_SUCCESS;
}

void stub_vkFreeMemory(VkDevice d, VkDeviceMemory m, const void *a)
{
    (void)d; (void)a;
    struct uk_hv_mem *hv = uk_hv_mem_find((uint64_t)m);
    if (hv) {
        if (g_vk.gpu && hv->blob.created)
            (void)uk_virtio_gpu_gl_context_detach_resource(g_vk.gpu, g_vk.ctx,
                                                           hv->blob.resource_id);
        if (g_vk.gpu && hv->blob.mapped)
            (void)uk_virtio_gpu_gl_blob_unmap(g_vk.gpu, &hv->blob);
        if (g_vk.gpu && hv->blob.created)
            (void)uk_virtio_gpu_gl_blob_destroy(g_vk.gpu, &hv->blob);
        hv->used = 0;
    }
}

/* Host-visible memory map: fall back to a local staging buffer for memory that
 * is not backed by a host-visible blob (see uk_hv_mem above). */
#define UK_STAGING_BUF_SIZE (256u * 1024u * 1024u) /* fallback staging */
static uint8_t g_staging_mem[UK_STAGING_BUF_SIZE];

VkResult stub_vkMapMemory(VkDevice dev, VkDeviceMemory mem,
                                 VkDeviceSize offset, VkDeviceSize size,
                                 uint32_t flags, void **ppData)
{
    (void)dev; (void)size; (void)flags;
    struct uk_hv_mem *hv = uk_hv_mem_find((uint64_t)mem);
    if (hv && hv->blob.mapped_addr) {
        *ppData = (void *)((uint8_t *)hv->blob.mapped_addr + offset);
        return VK_SUCCESS;
    }
    *ppData = (void *)(g_staging_mem + (offset & (UK_STAGING_BUF_SIZE - 1)));
    return VK_SUCCESS;
}

void stub_vkUnmapMemory(VkDevice d, VkDeviceMemory m)
{
    (void)d; (void)m;
}

VkResult stub_vkGetMemoryHostPointerPropertiesEXT(VkDevice d, uint32_t handleType,
                                                          const void *pHostPointer,
                                                          void *pProps)
{
    (void)d; (void)handleType; (void)pHostPointer;
    if (pProps) {
        /* VkMemoryHostPointerPropertiesEXT: sType(u32), pad(4), pNext(u64), memoryTypeBits(u32) */
        uint32_t *bits = (uint32_t *)((uint8_t *)pProps + 16);
        *bits = 0x7u; /* types 0,1,2 all compatible */
    }
    return VK_SUCCESS;
}

VkResult stub_vkFlushMappedMemoryRanges(VkDevice d, uint32_t c, const void *r)
{ (void)d; (void)c; (void)r; return VK_SUCCESS; }
VkResult stub_vkInvalidateMappedMemoryRanges(VkDevice d, uint32_t c, const void *r)
{ (void)d; (void)c; (void)r; return VK_SUCCESS; }

