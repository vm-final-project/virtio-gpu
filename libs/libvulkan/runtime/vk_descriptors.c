#include "vk_internal.h"

VkResult stub_vkCreateDescriptorSetLayout(VkDevice dev, const void *ci,
                                                  const void *alloc,
                                                  VkDescriptorSetLayout *pDSL)
{
    (void)dev; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    uint32_t n = rd_u32(ci, OFF_DSL_BINDING_COUNT);
    const uint8_t *bindings = (const uint8_t *)rd_ptr(ci, OFF_DSL_PBINDINGS);
    uint32_t binding_nums[32], desc_types[32], desc_counts[32], stage_flags[32];
    if (n > 32u) n = 32u;
    for (uint32_t i = 0; i < n; i++) {
        const void *b = bindings ? (const void *)(bindings + i * SIZE_DSLB) : NULL;
        binding_nums[i] = rd_u32(b, OFF_DSLB_BINDING);
        desc_types[i]   = rd_u32(b, OFF_DSLB_TYPE);
        desc_counts[i]  = rd_u32(b, OFF_DSLB_COUNT);
        stage_flags[i]  = rd_u32(b, OFF_DSLB_STAGE);
    }
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateDescriptorSetLayout(&_enc, UK_H_DEVICE, h, n,
                                                binding_nums, desc_types,
                                                desc_counts, stage_flags);
    UK_ENC_SUBMIT();
    *pDSL = (VkDescriptorSetLayout)h;
    return VK_SUCCESS;
}

void stub_vkDestroyDescriptorSetLayout(VkDevice d, VkDescriptorSetLayout l,
                                               const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyDescriptorSetLayout, (uint64_t)l); }

VkResult stub_vkCreateDescriptorPool(VkDevice dev, const void *ci,
                                            const void *alloc, VkDescriptorPool *pDP)
{
    (void)dev; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    uint32_t max_sets = rd_u32(ci, OFF_DP_MAX_SETS);
    uint32_t n = rd_u32(ci, OFF_DP_POOL_COUNT);
    const uint8_t *sizes = (const uint8_t *)rd_ptr(ci, OFF_DP_POOL_SIZES);
    uint32_t desc_types[32], desc_counts[32];
    if (n > 32u) n = 32u;
    for (uint32_t i = 0; i < n; i++) {
        const void *ps = sizes ? (const void *)(sizes + i * SIZE_DP_POOL_SIZE) : NULL;
        desc_types[i]  = rd_u32(ps, 0);
        desc_counts[i] = rd_u32(ps, 4);
    }
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateDescriptorPool(&_enc, UK_H_DEVICE, h,
                                           max_sets ? max_sets : 1u,
                                           n, desc_types, desc_counts);
    UK_ENC_SUBMIT();
    *pDP = (VkDescriptorPool)h;
    return VK_SUCCESS;
}

void stub_vkDestroyDescriptorPool(VkDevice d, VkDescriptorPool p,
                                          const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyDescriptorPool, (uint64_t)p); }

VkResult stub_vkAllocateDescriptorSets(VkDevice dev, const void *ai,
                                               VkDescriptorSet *pDS)
{
    (void)dev;
    uint64_t pool = rd_u64(ai, OFF_DSA_POOL);
    uint32_t count = rd_u32(ai, OFF_DSA_SET_COUNT);
    const uint64_t *layouts = (const uint64_t *)rd_ptr(ai, OFF_DSA_LAYOUTS);
    if (!count) count = 1;
    uint64_t handles[32];
    uint32_t n = count > 32u ? 32u : count;
    for (uint32_t i = 0; i < n; i++) {
        handles[i] = uk_vk_alloc_handle();
        pDS[i] = (VkDescriptorSet)handles[i];
    }
    UK_ENC_BEGIN();
    uk_venus_encode_vkAllocateDescriptorSets(&_enc, UK_H_DEVICE, pool,
                                             n, layouts, handles);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

void stub_vkUpdateDescriptorSets(VkDevice dev, uint32_t writeCount,
                                        const void *pWrites,
                                        uint32_t copyCount, const void *pCopies)
{
    (void)dev; (void)copyCount; (void)pCopies;
    const uint8_t *w = (const uint8_t *)pWrites;
    for (uint32_t i = 0; i < writeCount; i++, w += SIZE_WRITE_DESC) {
        uint64_t set = rd_u64(w, OFF_WRITE_DST_SET);
        uint32_t binding = rd_u32(w, OFF_WRITE_BINDING);
        uint32_t count = rd_u32(w, OFF_WRITE_DESC_COUNT);
        uint32_t type = rd_u32(w, OFF_WRITE_DESC_TYPE);
        const uint8_t *bi = (const uint8_t *)rd_ptr(w, OFF_WRITE_BUFFER_INFO);
        for (uint32_t j = 0; j < count && bi; j++) {
            const void *b = (const void *)(bi + j * SIZE_DESC_BUF_INFO);
            uint64_t buffer = rd_u64(b, 0);
            uint64_t off = rd_u64(b, 8);
            uint64_t range = rd_u64(b, 16);
            if (type == 7u /* VK_DESCRIPTOR_TYPE_STORAGE_BUFFER */) {
                UK_ENC_BEGIN();
                uk_venus_encode_vkUpdateDescriptorSets_storage(&_enc, UK_H_DEVICE,
                    set, binding + j, buffer, off, range);
                UK_ENC_SUBMIT();
            }
        }
    }
}

