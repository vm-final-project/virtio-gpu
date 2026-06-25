#include "vk_internal.h"

VkResult stub_vkEnumeratePhysicalDevices(VkInstance instance,
                                                 uint32_t *pPhysicalDeviceCount,
                                                 VkPhysicalDevice *pPhysicalDevices)
{
    (void)instance;
    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }
    if (*pPhysicalDeviceCount >= 1) {
        UK_ENC_BEGIN();
        uint64_t physdev_arr[1] = { UK_H_PHYSDEV };
        uk_venus_encode_vkEnumeratePhysicalDevices(&_enc, UK_H_INSTANCE,
                                                   1u, physdev_arr);
        UK_ENC_SUBMIT();
        pPhysicalDevices[0] = (VkPhysicalDevice)UK_H_PHYSDEV;
        *pPhysicalDeviceCount = 1;
    }
    return VK_SUCCESS;
}

/* VkPhysicalDeviceProperties.
 * Prefer the real host properties through the Venus reply stream. The fallback
 * below is only diagnostic after emitting a blocked marker; llama-vk runners
 * must not treat that path as acceptance evidence.
 *
 * Byte offsets verified with offsetof() on x86-64:
 *   limits start at byte 296:
 *     maxStorageBufferRange    = byte 324  (uint32)
 *     maxMemoryAllocationCount = byte 332  (uint32)
 *     maxPushConstantsSize     = byte 328  (uint32)
 *     maxBoundDescriptorSets   = byte 360  (uint32)
 *     maxComputeSharedMemorySize          = byte 512 (uint32)
 *     maxComputeWorkGroupInvocations      = byte 528 (uint32)
 *     maxComputeWorkGroupSize[3]          = bytes 532-543 (uint32[3])
 *     minUniformBufferOffsetAlignment     = byte 616 (uint64) — MUST be nonzero
 *     minStorageBufferOffsetAlignment     = byte 624 (uint64) — MUST be nonzero
 *       If zero: GGML_PAD(size,0)=0 so ggml thinks tensors need no alloc → NULL buf
 */
void stub_vkGetPhysicalDeviceProperties(VkPhysicalDevice physdev,
                                               void *pProperties)
{
    (void)physdev;
    uint8_t *b = (uint8_t *)pProperties;
    if (!b) return;

    static uint8_t real_props[824];
    static int  real_props_state; /* 0=untried, 1=have real, -1=failed */
    if (real_props_state == 0 && g_vk.gpu && g_vk.ctx) {
        if (uk_venus_query_physical_device_properties(g_vk.gpu, g_vk.ctx,
                                                      UK_H_PHYSDEV,
                                                      real_props,
                                                      sizeof(real_props)) == 0) {
            real_props_state = 1;
            printf("uk-ggml-vk: venus physical_device=%s\n",
                   (const char *)(real_props + 20));
        } else {
            real_props_state = -1;
            uk_vk_report_blocked_once(&g_props_query_blocked_reported,
                                      "blocked:venus-query-physical-device-properties");
        }
    }
    if (real_props_state == 1) {
        memcpy(b, real_props, sizeof(real_props));
        return;
    }

    memset(b, 0, 824); /* sizeof(VkPhysicalDeviceProperties) = 824 */
    uint32_t *p = (uint32_t *)b;
    p[0] = 0x00402000u; /* apiVersion: VK 1.2 */
    p[1] = 0;           /* driverVersion */
    p[2] = 0x10de;      /* vendorID: NVIDIA */
    p[3] = 0x27b0;      /* deviceID: RTX 4000 Ada */
    p[4] = 2;           /* deviceType: DISCRETE_GPU */
    char *name = (char *)(p + 5); /* deviceName at byte 20 */
    const char *devname = "VOGUE-Venus/NVIDIA RTX 4000 Ada";
    __builtin_memcpy(name, devname, __builtin_strlen(devname) + 1);
    /* Key limits (byte offsets within VkPhysicalDeviceProperties): */
    *(uint32_t *)(b + 324) = 0xFFFFFFFFu; /* maxStorageBufferRange */
    *(uint32_t *)(b + 328) = 256u;         /* maxPushConstantsSize */
    *(uint32_t *)(b + 332) = 4096u;        /* maxMemoryAllocationCount */
    *(uint32_t *)(b + 360) = 32u;          /* maxBoundDescriptorSets */
    *(uint32_t *)(b + 512) = 49152u;       /* maxComputeSharedMemorySize: 48 KB */
    /* maxComputeWorkGroupCount[3] @ bytes 516/520/524 (real V100 values). ggml
     * GGML_ASSERTs the dispatch grid <= these; if 0, every dispatch aborts. */
    *(uint32_t *)(b + 516) = 2147483647u;  /* maxComputeWorkGroupCount[0] */
    *(uint32_t *)(b + 520) = 65535u;       /* maxComputeWorkGroupCount[1] */
    *(uint32_t *)(b + 524) = 65535u;       /* maxComputeWorkGroupCount[2] */
    *(uint32_t *)(b + 528) = 1024u;        /* maxComputeWorkGroupInvocations */
    *(uint32_t *)(b + 532) = 1024u;        /* maxComputeWorkGroupSize[0] */
    *(uint32_t *)(b + 536) = 1024u;        /* maxComputeWorkGroupSize[1] */
    *(uint32_t *)(b + 540) = 64u;          /* maxComputeWorkGroupSize[2] */
    /* minMemoryMapAlignment (VkPhysicalDeviceLimits, size_t @ byte 600). ggml's
     * Vulkan_Host buffer type alignment is this value; if 0, GGML_PAD(size,0)=0
     * makes pinned host buffers (e.g. token_embd.weight) compute a 0-size and
     * fail to allocate ("unable to allocate Vulkan_Host buffer"). */
    /* Must be nonzero (ggml-vulkan's pinned Vulkan_Host buffer type uses this as
     * its tensor alignment; 0 makes GGML_PAD(size,0)=0 -> 0-size host buffers).
     * Must NOT be larger than a tensor's natural nbytes alignment, though: the
     * host buffer is sized from raw ggml_nbytes but tensors are placed padded to
     * this value, so an oversized alignment (e.g. a 4096 page) overflows the
     * buffer (observed: token_embd.weight needed 320864256 vs available
     * 320863392). 64 matches real-hardware minMemoryMapAlignment and divides
     * ggml's row-aligned tensor sizes. The host-visible blob mapping itself is
     * still page-aligned by virtio-gpu independently of this value. */
    *(uint64_t *)(b + 600) = 64ULL;        /* minMemoryMapAlignment */
    *(uint64_t *)(b + 616) = 256ULL;       /* minUniformBufferOffsetAlignment */
    *(uint64_t *)(b + 624) = 16ULL;        /* minStorageBufferOffsetAlignment */
}

/* VkPhysicalDeviceProperties2 — fills base properties then walks pNext chain.
 * sType values from vulkan_core.h:
 *   VkPhysicalDeviceSubgroupProperties      = 1000094000
 *   VkPhysicalDeviceMaintenance3Properties  = 1000168000
 *   VkPhysicalDeviceDriverProperties        = 1000196000
 *   VkPhysicalDeviceVulkan11Properties      = 50
 *   VkPhysicalDeviceVulkan12Properties      = 52
 * Byte offsets from check_layout.c:
 *   SubgroupProperties:     subgroupSize@16, supportedStages@20, supportedOperations@24
 *   Maintenance3Properties: maxPerSetDescriptors@16, maxMemoryAllocationSize@24
 *   Vulkan11Properties:     subgroupSize@64, subgroupSupportedStages@68,
 *                           subgroupSupportedOperations@72, maxMemoryAllocationSize@104
 */
void stub_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physdev, void *p2)
{
    if (!p2) return;
    stub_vkGetPhysicalDeviceProperties(physdev, (uint8_t *)p2 + 16); /* skip sType+pNext */
    struct { uint32_t stype; uint32_t _pad; void *pnext; } *chain =
        (void *)*(void **)((uint8_t *)p2 + 8); /* p2->pNext */
    while (chain) {
        uint8_t *b = (uint8_t *)chain;
        switch (chain->stype) {
        case 1000094000u: /* VkPhysicalDeviceSubgroupProperties */
            *(uint32_t *)(b + 16) = 32u;    /* subgroupSize: NVIDIA warp = 32 */
            *(uint32_t *)(b + 20) = 0x20u;  /* supportedStages: COMPUTE */
            *(uint32_t *)(b + 24) = 0x7fu;  /* supportedOperations: all basic ops */
            *(uint32_t *)(b + 28) = 1u;     /* quadOperationsInAllStages */
            break;
        case 1000168000u: /* VkPhysicalDeviceMaintenance3Properties */
            *(uint32_t *)(b + 16) = 1024u;             /* maxPerSetDescriptors */
            *(uint64_t *)(b + 24) = 0xFFFFFFFFFFFFFFFFULL; /* maxMemoryAllocationSize */
            break;
        case 1000196000u: /* VkPhysicalDeviceDriverProperties */
            *(uint32_t *)(b + 16) = 1u;     /* driverID: VK_DRIVER_ID_NVIDIA_PROPRIETARY */
            break;
        case 50u: /* VkPhysicalDeviceVulkan11Properties */
            *(uint32_t *)(b + 64)  = 32u;    /* subgroupSize */
            *(uint32_t *)(b + 68)  = 0x20u;  /* subgroupSupportedStages: COMPUTE */
            *(uint32_t *)(b + 72)  = 0x7fu;  /* subgroupSupportedOperations */
            *(uint64_t *)(b + 104) = 0xFFFFFFFFFFFFFFFFULL; /* maxMemoryAllocationSize */
            break;
        default:
            break;
        }
        chain = (void *)chain->pnext;
    }
}

/* VkPhysicalDeviceFeatures — enable all compute-relevant features */
void stub_vkGetPhysicalDeviceFeatures(VkPhysicalDevice physdev, void *pFeatures)
{
    (void)physdev;
    if (!pFeatures) return;
    /* All VkPhysicalDeviceFeatures are VkBool32; set all to VK_TRUE */
    uint32_t *f = (uint32_t *)pFeatures;
    for (int i = 0; i < 55; i++) f[i] = 1u; /* VK_TRUE for all 55 features */
}

void stub_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physdev, void *p2)
{
    if (!p2) return;
    stub_vkGetPhysicalDeviceFeatures(physdev, (uint8_t *)p2 + 16);
    /* Walk pNext chain and set VkBool32 feature fields to VK_TRUE.
     * IMPORTANT: write exactly as many fields as the struct actually contains;
     * writing past the end corrupts the heap (e.g. VkPhysicalDeviceVulkan11Features
     * is only 64 bytes = 16-byte header + 12 bools; writing 64 words overflows). */
    struct { uint32_t stype; uint32_t _pad; void *pnext; } *chain =
        (void *)*(void **)((uint8_t *)p2 + 8); /* p2->pNext */
    while (chain) {
        int n;
        switch (chain->stype) {
        case 49: n = 12; break; /* VkPhysicalDeviceVulkan11Features */
        case 51: n = 47; break; /* VkPhysicalDeviceVulkan12Features */
        case 53: n = 15; break; /* VkPhysicalDeviceVulkan13Features */
        default:  n =  2; break; /* safe default for smaller extension structs */
        }
        uint32_t *fields = (uint32_t *)((uint8_t *)chain + 16);
        for (int i = 0; i < n; i++) fields[i] = 1u;
        chain = (void *)chain->pnext;
    }
}

/* VkPhysicalDeviceMemoryProperties — 2 heap / 3 type layout typical for discrete GPU */
void stub_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physdev,
                                                      void *pMemProps)
{
    (void)physdev;
    if (!pMemProps) return;

    /* Real Venus round-trip: read the host GPU's actual memory types/heaps so
     * ggml selects a memory type index that truly exists and is host-visible on
     * the host (required for vkAllocateMemory + host-visible blob export to
     * succeed). Cached after the first success; falls back to the fabricated
     * layout below on failure. */
    static int real_mp_state; /* 0=untried, 1=have real, -1=failed */
    if (real_mp_state == 0 && g_vk.gpu && g_vk.ctx) {
        if (uk_venus_query_memory_properties(g_vk.gpu, g_vk.ctx, UK_H_PHYSDEV, g_memprops) == 0
            && *(uint32_t *)g_memprops > 0u) {
            real_mp_state = 1;
            g_memprops_valid = 1;
            printf("uk-ggml-vk: venus memory types=%u heaps=%u\n",
                   *(uint32_t *)g_memprops, *(uint32_t *)(g_memprops + 260));
        } else {
            real_mp_state = -1;
            uk_vk_report_blocked_once(&g_memprops_query_blocked_reported,
                                      "blocked:venus-query-memory-properties");
        }
    }
    if (real_mp_state == 1) {
        memcpy(pMemProps, g_memprops, 520);
        return;
    }

    memset(pMemProps, 0, 520); /* sizeof(VkPhysicalDeviceMemoryProperties) */
    uint32_t *mp = (uint32_t *)pMemProps;
    /* memoryTypeCount = 3 */
    mp[0] = 3;
    /* memoryTypes[0]: device-local (propertyFlags=0x1, heapIndex=0) */
    mp[1] = 0x1; mp[2] = 0;
    /* memoryTypes[1]: host-visible + host-coherent (0x6, heap=1) */
    mp[3] = 0x6; mp[4] = 1;
    /* memoryTypes[2]: device-local + host-visible (0x7, heap=0) */
    mp[5] = 0x7; mp[6] = 0;
    /* VkPhysicalDeviceMemoryProperties layout (verified via offsetof):
     *   byte   0: memoryTypeCount (uint32)
     *   bytes  4-259: memoryTypes[32] (each VkMemoryType = 8 bytes)
     *   byte 260: memoryHeapCount (uint32)
     *   bytes 264+: memoryHeaps[16] (each VkMemoryHeap = 16 bytes: size(8)+flags(4)+pad(4))
     */
    *(uint32_t *)((uint8_t *)pMemProps + 260) = 2u; /* memoryHeapCount */
    /* VkMemoryHeap = { VkDeviceSize size; VkMemoryHeapFlags flags; } (16B w/ pad).
     * heap[0] starts at 264: size@264, flags@272. heap[1]: size@280, flags@288.
     * The device-local heap MUST carry VK_MEMORY_HEAP_DEVICE_LOCAL_BIT (0x1) or
     * ggml_backend_vk_get_device_memory() skips it and reports "0 MiB free". */
    *(uint64_t *)((uint8_t *)pMemProps + 264) = 21474836480ULL; /* heap[0].size = 20 GiB device */
    *(uint32_t *)((uint8_t *)pMemProps + 272) = 0x1u;           /* heap[0].flags = DEVICE_LOCAL */
    *(uint64_t *)((uint8_t *)pMemProps + 280) = 34359738368ULL; /* heap[1].size = 32 GiB system */
    *(uint32_t *)((uint8_t *)pMemProps + 288) = 0x0u;           /* heap[1].flags = host/system */
}

void stub_vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physdev,
                                                       void *p2)
{
    if (!p2) return;
    stub_vkGetPhysicalDeviceMemoryProperties(physdev, (uint8_t *)p2 + 16);
}

void stub_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physdev,
                                                          uint32_t *pCount,
                                                          void *pQueueFamilyProperties)
{
    (void)physdev;
    if (!pQueueFamilyProperties) { *pCount = 1; return; }
    if (*pCount >= 1) {
        uint32_t *qfp = (uint32_t *)pQueueFamilyProperties;
        qfp[0] = 0x7u;  /* GRAPHICS|COMPUTE|TRANSFER */
        qfp[1] = 16u;   /* queueCount */
        qfp[2] = 64u;   /* timestampValidBits */
        /* minImageTransferGranularity = {1,1,1} */
        qfp[3] = 1; qfp[4] = 1; qfp[5] = 1;
        *pCount = 1;
    }
}

VkResult stub_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physdev,
                                                          const char *layerName,
                                                          uint32_t *pCount,
                                                          void *pProperties)
{
    (void)physdev; (void)layerName; (void)pProperties;
    /* Report core compute extensions ggml-vulkan queries */
    *pCount = 0;
    return VK_SUCCESS;
}

void stub_vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice p, void *v)
{ stub_vkGetPhysicalDeviceProperties2(p, v); }
void stub_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice p, void *v)
{ stub_vkGetPhysicalDeviceFeatures2(p, v); }
void stub_vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice p, void *v)
{ stub_vkGetPhysicalDeviceMemoryProperties2(p, v); }
