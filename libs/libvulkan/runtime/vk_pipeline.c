#include "vk_internal.h"

VkResult stub_vkCreateShaderModule(VkDevice dev, const void *ci,
                                          const void *alloc, VkShaderModule *pShader)
{
    (void)dev; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    /* VkShaderModuleCreateInfo (Vulkan 1.3):
     *   sType     (uint32) offset 0
     *   [pad 4]
     *   pNext     (uint64) offset 8
     *   flags     (uint32) offset 16   [VkShaderModuleCreateFlags]
     *   [pad 4]
     *   codeSize  (uint64) offset 24   [size_t on 64-bit = uint64]
     *   pCode     (uint32*) offset 32
     */
    uint64_t code_size      = rd_u64(ci, OFF_SHADER_CODE_SIZE);
    const uint32_t *code    = (const uint32_t *)rd_ptr(ci, OFF_SHADER_PCODE);
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateShaderModule(&_enc, UK_H_DEVICE, h, code,
                                         (uint32_t)(code_size / 4u));
    printf("VOGUE-DBG createShaderModule h=0x%llx codeSize=%llu encpos=%u ovf=%d bufsz=%u\n",
           (unsigned long long)h, (unsigned long long)code_size,
           _enc.pos, _enc.overflow, UK_DISPATCH_BUF_SIZE);
    UK_ENC_SUBMIT();
    *pShader = (VkShaderModule)h;
    return VK_SUCCESS;
}

void stub_vkDestroyShaderModule(VkDevice d, VkShaderModule s, const void *a)
{ (void)d; (void)a;
  printf("VOGUE-DBG destroyShaderModule h=0x%llx\n", (unsigned long long)(uint64_t)s);
  uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyShaderModule, (uint64_t)s); }

VkResult stub_vkCreatePipelineLayout(VkDevice dev, const void *ci,
                                            const void *alloc, VkPipelineLayout *pPL)
{
    (void)dev; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    uint32_t nsets = rd_u32(ci, OFF_PL_SET_COUNT);
    const uint64_t *sets = (const uint64_t *)rd_ptr(ci, OFF_PL_SET_LAYOUTS);
    uint32_t nranges = rd_u32(ci, OFF_PL_PUSH_COUNT);
    const void *range = rd_ptr(ci, OFF_PL_PUSH_RANGES);
    uint32_t stage = nranges ? rd_u32(range, OFF_PUSH_STAGE) : 0u;
    uint32_t off   = nranges ? rd_u32(range, OFF_PUSH_OFFSET) : 0u;
    uint32_t size  = nranges ? rd_u32(range, OFF_PUSH_SIZE) : 0u;
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreatePipelineLayout(&_enc, UK_H_DEVICE, h,
                                           nsets, sets,
                                           nranges ? 1u : 0u,
                                           stage, off, size);
    UK_ENC_SUBMIT();
    *pPL = (VkPipelineLayout)h;
    return VK_SUCCESS;
}

void stub_vkDestroyPipelineLayout(VkDevice d, VkPipelineLayout l,
                                          const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyPipelineLayout, (uint64_t)l); }

VkResult stub_vkCreateComputePipelines(VkDevice dev, uint64_t cache,
                                              uint32_t count, const void *ci,
                                              const void *alloc, VkPipeline *pPipes)
{
    (void)dev; (void)cache; (void)alloc;
    /* VkComputePipelineCreateInfo: stage at offset 16 (VkPipelineShaderStageCreateInfo)
     * stage.module at offset 16+24=40 */
    const uint8_t *p = (const uint8_t *)ci;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t h = uk_vk_alloc_handle();
        const void *stage = (const void *)(p + OFF_CP_STAGE);
        uint64_t layout_h = rd_u64(p, OFF_CP_LAYOUT);
        uint64_t shader_h = rd_u64(stage, OFF_STAGE_MODULE);
        const char *entry = (const char *)rd_ptr(stage, OFF_STAGE_PNAME);
        UK_ENC_BEGIN();
        uk_venus_encode_vkCreateComputePipelines(&_enc, UK_H_DEVICE, h,
                                                 layout_h, shader_h,
                                                 entry ? entry : "main");
        UK_ENC_SUBMIT();
        printf("VOGUE-DBG createComputePipeline h=0x%llx module=0x%llx layout=0x%llx\n",
               (unsigned long long)h, (unsigned long long)shader_h,
               (unsigned long long)layout_h);
        pPipes[i] = (VkPipeline)h;
        p += SIZE_CP_INFO;
    }
    return VK_SUCCESS;
}

void stub_vkDestroyPipeline(VkDevice d, VkPipeline p, const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyPipeline, (uint64_t)p); }

