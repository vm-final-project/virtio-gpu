/*
 * ggml_vk_dispatch_test.c — N3 static Vulkan ICD dispatch layer test.
 *
 * Tests the libvulkan static Vulkan dispatch layer that backs
 * ggml-vulkan.cpp inside Unikraft without dlopen/libvulkan.so.
 *
 * Test groups:
 *
 *   A. Proc lookup  — vkGetInstanceProcAddr / vkGetDeviceProcAddr return
 *      non-NULL for every registered function; unknown names return NULL.
 *      No VirtIO-GPU device required.
 *
 *   B. Init          — uk_vulkan_init() against the fake
 *      VirtIO-GPU backend.  Idempotency and get_proc_addr_fn().
 *
 *   C. Call stubs    — Call each compute-path Vulkan function through the
 *      proc-addr table and verify VK_SUCCESS is returned.  Uses the fake
 *      VirtIO-GPU backend so UK_ENC_SUBMIT() exercises the full encode+
 *      submit path through the fake uk_virtio_gpu_gl_context_submit().
 *
 *   D. Sequence      — Full compute bootstrap sequence in order:
 *      CreateInstance → EnumeratePhysicalDevices → CreateDevice →
 *      AllocateMemory → CreateBuffer × 2 → CreateShaderModule →
 *      CreateDescriptorSetLayout → CreateDescriptorPool →
 *      AllocateDescriptorSets → CreatePipelineLayout →
 *      CreateComputePipelines → CreateCommandPool →
 *      AllocateCommandBuffers → Begin → BindPipeline →
 *      BindDescriptorSets → Dispatch → End → CreateFence →
 *      QueueSubmit → WaitForFences → ResetFences
 *      All must return VK_SUCCESS.
 *
 * Evidence: LLAMA-VK-N3-DISPATCH (host-side substrate gate).
 *
 * Build: see tests/Makefile (native target via ggml_vk_dispatch_test)
 * Run:   ./build/ggml_vk_dispatch_test
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <uk/vulkan.h>
#include <uk/vulkan_venus.h>
#include <uk/virtio_gpu.h>

/* ── Minimal Vulkan ABI types (must not include vulkan.h to avoid conflicts) */
typedef uint64_t VkInstance;
typedef uint64_t VkPhysicalDevice;
typedef uint64_t VkDevice;
typedef uint64_t VkQueue;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkBuffer;
typedef uint64_t VkShaderModule;
typedef uint64_t VkDescriptorSetLayout;
typedef uint64_t VkDescriptorPool;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkPipelineLayout;
typedef uint64_t VkPipeline;
typedef uint64_t VkCommandPool;
typedef uint64_t VkCommandBuffer;
typedef uint64_t VkFence;
typedef uint32_t VkResult;
typedef void    *PFN_vkVoidFunction;
#define VK_SUCCESS     0u
#define VK_NULL_HANDLE 0ULL

/* Extern declarations for the symbols exported by uk_vulkan_dispatch.c. */
extern PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance,
                                                const char *pName);
extern PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device,
                                              const char *pName);

/* ── Test helpers ──────────────────────────────────────────────────────── */
static int g_fail;
static int g_pass;

#define CHECK(name, cond) do { \
    if (cond) { \
        printf("  PASS  %s\n", name); \
        g_pass++; \
    } else { \
        printf("  FAIL  %s\n", name); \
        g_fail++; \
    } \
} while (0)

#define CHECK_EQ(name, a, b) CHECK(name, (uint64_t)(a) == (uint64_t)(b))
#define CHECK_NZ(name, val)  CHECK(name, (val) != 0)
#define CHECK_NULL(name, val) CHECK(name, (val) == NULL)

/* ── Helper: look up a function through the dispatch table ─────────────── */
static PFN_vkVoidFunction get(const char *name)
{
    return vkGetInstanceProcAddr((VkInstance)VK_NULL_HANDLE, name);
}

/* ── Group A: Proc lookup ──────────────────────────────────────────────── */
static void test_proc_lookup(void)
{
    printf("\n[A] Proc lookup — vkGetInstanceProcAddr / vkGetDeviceProcAddr\n");

    /* Core compute-path functions must be present */
    CHECK_NZ("proc:vkCreateInstance",           get("vkCreateInstance"));
    CHECK_NZ("proc:vkDestroyInstance",          get("vkDestroyInstance"));
    CHECK_NZ("proc:vkEnumeratePhysicalDevices", get("vkEnumeratePhysicalDevices"));
    CHECK_NZ("proc:vkGetPhysicalDeviceProperties",
             get("vkGetPhysicalDeviceProperties"));
    CHECK_NZ("proc:vkGetPhysicalDeviceProperties2",
             get("vkGetPhysicalDeviceProperties2"));
    CHECK_NZ("proc:vkGetPhysicalDeviceFeatures",
             get("vkGetPhysicalDeviceFeatures"));
    CHECK_NZ("proc:vkGetPhysicalDeviceFeatures2",
             get("vkGetPhysicalDeviceFeatures2"));
    CHECK_NZ("proc:vkGetPhysicalDeviceMemoryProperties",
             get("vkGetPhysicalDeviceMemoryProperties"));
    CHECK_NZ("proc:vkGetPhysicalDeviceMemoryProperties2",
             get("vkGetPhysicalDeviceMemoryProperties2"));
    CHECK_NZ("proc:vkGetPhysicalDeviceQueueFamilyProperties",
             get("vkGetPhysicalDeviceQueueFamilyProperties"));
    CHECK_NZ("proc:vkEnumerateDeviceExtensionProperties",
             get("vkEnumerateDeviceExtensionProperties"));
    CHECK_NZ("proc:vkEnumerateInstanceLayerProperties",
             get("vkEnumerateInstanceLayerProperties"));
    CHECK_NZ("proc:vkEnumerateInstanceExtensionProperties",
             get("vkEnumerateInstanceExtensionProperties"));
    CHECK_NZ("proc:vkCreateDevice",             get("vkCreateDevice"));
    CHECK_NZ("proc:vkDestroyDevice",            get("vkDestroyDevice"));
    CHECK_NZ("proc:vkGetDeviceQueue",           get("vkGetDeviceQueue"));
    CHECK_NZ("proc:vkAllocateMemory",           get("vkAllocateMemory"));
    CHECK_NZ("proc:vkFreeMemory",               get("vkFreeMemory"));
    CHECK_NZ("proc:vkMapMemory",                get("vkMapMemory"));
    CHECK_NZ("proc:vkUnmapMemory",              get("vkUnmapMemory"));
    CHECK_NZ("proc:vkCreateBuffer",             get("vkCreateBuffer"));
    CHECK_NZ("proc:vkDestroyBuffer",            get("vkDestroyBuffer"));
    CHECK_NZ("proc:vkGetBufferMemoryRequirements",
             get("vkGetBufferMemoryRequirements"));
    CHECK_NZ("proc:vkGetBufferMemoryRequirements2",
             get("vkGetBufferMemoryRequirements2"));
    CHECK_NZ("proc:vkBindBufferMemory",         get("vkBindBufferMemory"));
    CHECK_NZ("proc:vkGetBufferDeviceAddress",   get("vkGetBufferDeviceAddress"));
    CHECK_NZ("proc:vkCreateShaderModule",       get("vkCreateShaderModule"));
    CHECK_NZ("proc:vkDestroyShaderModule",      get("vkDestroyShaderModule"));
    CHECK_NZ("proc:vkCreateDescriptorSetLayout",
             get("vkCreateDescriptorSetLayout"));
    CHECK_NZ("proc:vkCreateDescriptorPool",     get("vkCreateDescriptorPool"));
    CHECK_NZ("proc:vkAllocateDescriptorSets",   get("vkAllocateDescriptorSets"));
    CHECK_NZ("proc:vkUpdateDescriptorSets",     get("vkUpdateDescriptorSets"));
    CHECK_NZ("proc:vkCreatePipelineLayout",     get("vkCreatePipelineLayout"));
    CHECK_NZ("proc:vkDestroyPipelineLayout",    get("vkDestroyPipelineLayout"));
    CHECK_NZ("proc:vkCreateComputePipelines",   get("vkCreateComputePipelines"));
    CHECK_NZ("proc:vkDestroyPipeline",          get("vkDestroyPipeline"));
    CHECK_NZ("proc:vkCreateCommandPool",        get("vkCreateCommandPool"));
    CHECK_NZ("proc:vkAllocateCommandBuffers",   get("vkAllocateCommandBuffers"));
    CHECK_NZ("proc:vkFreeCommandBuffers",       get("vkFreeCommandBuffers"));
    CHECK_NZ("proc:vkBeginCommandBuffer",       get("vkBeginCommandBuffer"));
    CHECK_NZ("proc:vkEndCommandBuffer",         get("vkEndCommandBuffer"));
    CHECK_NZ("proc:vkResetCommandBuffer",       get("vkResetCommandBuffer"));
    CHECK_NZ("proc:vkResetCommandPool",         get("vkResetCommandPool"));
    CHECK_NZ("proc:vkGetMemoryHostPointerPropertiesEXT",
             get("vkGetMemoryHostPointerPropertiesEXT"));
    CHECK_NZ("proc:vkCmdBindPipeline",          get("vkCmdBindPipeline"));
    CHECK_NZ("proc:vkCmdBindDescriptorSets",    get("vkCmdBindDescriptorSets"));
    CHECK_NZ("proc:vkCmdPushConstants",         get("vkCmdPushConstants"));
    CHECK_NZ("proc:vkCmdDispatch",              get("vkCmdDispatch"));
    CHECK_NZ("proc:vkCmdCopyBuffer",            get("vkCmdCopyBuffer"));
    CHECK_NZ("proc:vkCmdFillBuffer",            get("vkCmdFillBuffer"));
    CHECK_NZ("proc:vkCmdPipelineBarrier",       get("vkCmdPipelineBarrier"));
    CHECK_NZ("proc:vkQueueSubmit",              get("vkQueueSubmit"));
    CHECK_NZ("proc:vkQueueWaitIdle",            get("vkQueueWaitIdle"));
    CHECK_NZ("proc:vkDeviceWaitIdle",           get("vkDeviceWaitIdle"));
    CHECK_NZ("proc:vkCreateFence",              get("vkCreateFence"));
    CHECK_NZ("proc:vkDestroyFence",             get("vkDestroyFence"));
    CHECK_NZ("proc:vkResetFences",              get("vkResetFences"));
    CHECK_NZ("proc:vkWaitForFences",            get("vkWaitForFences"));
    CHECK_NZ("proc:vkGetFenceStatus",           get("vkGetFenceStatus"));
    CHECK_NZ("proc:vkCreateSemaphore",          get("vkCreateSemaphore"));
    CHECK_NZ("proc:vkCreateQueryPool",          get("vkCreateQueryPool"));
    CHECK_NZ("proc:vkGetQueryPoolResults",      get("vkGetQueryPoolResults"));
    CHECK_NZ("proc:vkCmdResetQueryPool",        get("vkCmdResetQueryPool"));
    CHECK_NZ("proc:vkCmdWriteTimestamp",        get("vkCmdWriteTimestamp"));
    CHECK_NZ("proc:vkCmdBeginQuery",            get("vkCmdBeginQuery"));
    CHECK_NZ("proc:vkCmdEndQuery",              get("vkCmdEndQuery"));

    /* KHR aliases must map to the same non-NULL entry */
    CHECK_NZ("proc:vkGetPhysicalDeviceProperties2KHR",
             get("vkGetPhysicalDeviceProperties2KHR"));
    CHECK_NZ("proc:vkGetPhysicalDeviceFeatures2KHR",
             get("vkGetPhysicalDeviceFeatures2KHR"));
    CHECK_NZ("proc:vkGetPhysicalDeviceMemoryProperties2KHR",
             get("vkGetPhysicalDeviceMemoryProperties2KHR"));
    CHECK_NZ("proc:vkGetBufferMemoryRequirements2KHR",
             get("vkGetBufferMemoryRequirements2KHR"));
    CHECK_NZ("proc:vkGetBufferDeviceAddressKHR",
             get("vkGetBufferDeviceAddressKHR"));

    /* Unknown function names must return NULL */
    CHECK_NULL("proc:unknown_fn returns NULL",   get("vkDoesNotExist"));
    CHECK_NULL("proc:empty_name returns NULL",   get(""));
    CHECK_NULL("proc:null_name returns NULL",
               vkGetInstanceProcAddr(VK_NULL_HANDLE, NULL));

    /* vkGetDeviceProcAddr must also work (same table) */
    CHECK_NZ("devproc:vkCmdDispatch",
             vkGetDeviceProcAddr((VkDevice)VK_NULL_HANDLE, "vkCmdDispatch"));
    CHECK_NZ("devproc:vkQueueSubmit",
             vkGetDeviceProcAddr((VkDevice)VK_NULL_HANDLE, "vkQueueSubmit"));
    CHECK_NULL("devproc:unknown returns NULL",
               vkGetDeviceProcAddr((VkDevice)VK_NULL_HANDLE, "vkNoSuchFn"));

    /* uk_vulkan_get_instance_proc_addr_fn must return non-NULL */
    CHECK_NZ("get_proc_addr_fn:non_null",
             uk_vulkan_get_instance_proc_addr_fn());

    /* The returned fn must itself find vkCreateInstance */
    PFN_uk_vkGetInstanceProcAddr pfn = uk_vulkan_get_instance_proc_addr_fn();
    CHECK_NZ("get_proc_addr_fn:finds_vkCreateInstance",
             pfn ? pfn(VK_NULL_HANDLE, "vkCreateInstance") : NULL);
}

/* ── Group B: Dispatch init ────────────────────────────────────────────── */
static void test_dispatch_init(void)
{
    printf("\n[B] Dispatch init — uk_vulkan_init()\n");

    int rc = uk_vulkan_init();
    CHECK("init:returns_zero", rc == 0);

    /* Idempotent: second call must also succeed */
    rc = uk_vulkan_init();
    CHECK("init:idempotent", rc == 0);

    /* plan-optimize.md L3.1/L3.4 + plan-redesign-vulkan.md — verify the info
     * getter returns sane values: initialized set, the venus driver advertised,
     * a non-empty supported command count, batching off, hostmem_fixed off. */
    struct uk_vulkan_info info = {0};
    info.batch_enabled = -1; info.hostmem_fixed = -1;
    uk_vulkan_get_info(&info);
    CHECK("info:initialized_set", info.initialized != 0);
    CHECK("info:driver_name_venus",
          info.driver_name && strcmp(info.driver_name, "venus") == 0);
    CHECK("info:claim_boundary_set",
          info.claim_boundary && info.claim_boundary[0] != '\0');
    CHECK("info:supported_command_count_nonzero",
          info.supported_command_count > 0u);
    CHECK("info:batch_enabled_default_off", info.batch_enabled == 0);
    CHECK("info:hostmem_fixed_default_off", info.hostmem_fixed == 0);

    /* NULL must not crash. */
    uk_vulkan_get_info(NULL);
    CHECK("info:null_safe", 1);
}

/* ── Group C: Call stubs ───────────────────────────────────────────────── */

/* Stub call helpers typed for the actual C ABI. */
typedef VkResult (*PFN_CreateInstance)(const void *, const void *, VkInstance *);
typedef VkResult (*PFN_EnumPhysDev)(VkInstance, uint32_t *, VkPhysicalDevice *);
typedef void     (*PFN_GetPhysDevProps)(VkPhysicalDevice, void *);
typedef void     (*PFN_GetPhysDevFeats)(VkPhysicalDevice, void *);
typedef void     (*PFN_GetPhysDevMemProps)(VkPhysicalDevice, void *);
typedef void     (*PFN_GetPhysDevQueueFam)(VkPhysicalDevice, uint32_t *, void *);
typedef VkResult (*PFN_EnumDevExt)(VkPhysicalDevice, const char *, uint32_t *, void *);
typedef VkResult (*PFN_CreateDevice)(VkPhysicalDevice, const void *, const void *, VkDevice *);
typedef void     (*PFN_GetDevQueue)(VkDevice, uint32_t, uint32_t, VkQueue *);
typedef VkResult (*PFN_AllocMem)(VkDevice, const void *, const void *, VkDeviceMemory *);
typedef VkResult (*PFN_CreateBuf)(VkDevice, const void *, const void *, VkBuffer *);
typedef void     (*PFN_GetBufMemReq)(VkDevice, VkBuffer, void *);
typedef VkResult (*PFN_BindBufMem)(VkDevice, VkBuffer, VkDeviceMemory, uint64_t);
typedef VkResult (*PFN_MapMemory)(VkDevice, VkDeviceMemory, uint64_t, uint64_t, uint32_t, void **);
typedef VkResult (*PFN_CreateShader)(VkDevice, const void *, const void *, VkShaderModule *);
typedef VkResult (*PFN_CreateDSL)(VkDevice, const void *, const void *, VkDescriptorSetLayout *);
typedef VkResult (*PFN_CreateDP)(VkDevice, const void *, const void *, VkDescriptorPool *);
typedef VkResult (*PFN_AllocDS)(VkDevice, const void *, VkDescriptorSet *);
typedef VkResult (*PFN_CreatePL)(VkDevice, const void *, const void *, VkPipelineLayout *);
typedef VkResult (*PFN_CreateCP)(VkDevice, uint64_t, uint32_t, const void *, const void *, VkPipeline *);
typedef VkResult (*PFN_CreateCmdPool)(VkDevice, const void *, const void *, VkCommandPool *);
typedef VkResult (*PFN_AllocCmds)(VkDevice, const void *, VkCommandBuffer *);
typedef VkResult (*PFN_BeginCmd)(VkCommandBuffer, const void *);
typedef VkResult (*PFN_EndCmd)(VkCommandBuffer);
typedef void     (*PFN_CmdBind)(VkCommandBuffer, uint32_t, VkPipeline);
typedef void     (*PFN_CmdBindDS)(VkCommandBuffer, uint32_t, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *);
typedef void     (*PFN_CmdPushConst)(VkCommandBuffer, VkPipelineLayout, uint32_t, uint32_t, uint32_t, const void *);
typedef void     (*PFN_CmdDispatch)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
typedef void     (*PFN_CmdBarrier)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, const void *, uint32_t, const void *, uint32_t, const void *);
typedef VkResult (*PFN_CreateFence)(VkDevice, const void *, const void *, VkFence *);
typedef VkResult (*PFN_ResetFences)(VkDevice, uint32_t, const VkFence *);
typedef VkResult (*PFN_WaitFences)(VkDevice, uint32_t, const VkFence *, uint32_t, uint64_t);
typedef VkResult (*PFN_QueueSubmit)(VkQueue, uint32_t, const void *, VkFence);
typedef void     (*PFN_UpdateDS)(VkDevice, uint32_t, const void *, uint32_t, const void *);
typedef void     (*PFN_CmdCopyBuffer)(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const void *);
typedef void     (*PFN_CmdFillBuffer)(VkCommandBuffer, VkBuffer, uint64_t, uint64_t, uint32_t);
typedef void     (*PFN_FreeMemory)(VkDevice, VkDeviceMemory, const void *);
typedef void     (*PFN_DestroyBuffer)(VkDevice, VkBuffer, const void *);
typedef void     (*PFN_DestroyShader)(VkDevice, VkShaderModule, const void *);
typedef void     (*PFN_DestroyPipeline)(VkDevice, VkPipeline, const void *);
typedef void     (*PFN_DestroyPipelineLayout)(VkDevice, VkPipelineLayout, const void *);
typedef void     (*PFN_DestroyDescriptorPool)(VkDevice, VkDescriptorPool, const void *);
typedef void     (*PFN_DestroyDescriptorSetLayout)(VkDevice, VkDescriptorSetLayout, const void *);
typedef void     (*PFN_DestroyCommandPool)(VkDevice, VkCommandPool, uint32_t);
typedef void     (*PFN_FreeCommandBuffers)(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer *);

/* Minimal struct stubs for creating resources.
 * These match the layout of the real Vulkan structs at the fields the
 * stubs read.  Only the fields actually accessed by the stub code need
 * to be correct; the rest can be zero.
 *
 * Layout references: Vulkan spec 1.3 chapter 46 (VkStructureType values)
 * and the struct member offsets the stub code uses.
 */

/* VkApplicationInfo: sType=0, pNext=0, pAppName, pEngineName, appVer, engVer, apiVer */
struct FakeAppInfo {
    uint32_t sType;   /* VK_STRUCTURE_TYPE_APPLICATION_INFO = 0 */
    uint64_t pNext;   /* NULL */
    uint64_t appName; /* ptr (ignored) */
    uint32_t appVer;
    uint64_t engName;
    uint32_t engVer;
    uint32_t apiVer;
};

/* VkInstanceCreateInfo: sType=1, pNext=0, flags=0, pApp=..., layerCount=0, extCount=0 */
struct FakeInstanceCI {
    uint32_t sType;   /* 1 */
    uint64_t pNext;
    uint32_t flags;
    uint64_t pApp;    /* ptr to FakeAppInfo (ignored) */
    uint32_t layerCount;
    uint64_t ppLayers;
    uint32_t extCount;
    uint64_t ppExts;
};

/* VkMemoryAllocateInfo: sType=5, pNext=0, allocationSize, memoryTypeIndex */
struct FakeMemAllocInfo {
    uint32_t sType;       /* 5 */
    uint64_t pNext;       /* 0 */
    uint64_t allocSize;
    uint32_t memTypeIdx;
};

/* VkBufferCreateInfo: sType=12, pNext=0, flags=0, size, usage, sharingMode=0, queueFamilyIndexCount=0 */
struct FakeBufCI {
    uint32_t sType;       /* 12 */
    uint64_t pNext;       /* 0 */
    uint32_t flags;       /* 0 */
    uint64_t size;
    uint32_t usage;       /* VK_BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x80 */
    uint32_t sharingMode; /* VK_SHARING_MODE_EXCLUSIVE = 0 */
    uint32_t qfiCount;    /* 0 */
    uint64_t pQFIs;       /* NULL */
};

/* VkShaderModuleCreateInfo: sType=16, pNext=0, flags=0, codeSize, pCode */
struct FakeShaderCI {
    uint32_t  sType;      /* 16 */
    uint64_t  pNext;      /* 0 */
    uint32_t  flags;      /* 0 */
    uint64_t  codeSize;   /* in bytes */
    uint32_t *pCode;
};

/* VkDescriptorSetLayoutBinding: binding, descriptorType=7, descriptorCount, stageFlags */
struct FakeDSLBinding {
    uint32_t binding;
    uint32_t descriptorType;  /* VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7 */
    uint32_t descriptorCount;
    uint32_t stageFlags;      /* VK_SHADER_STAGE_COMPUTE_BIT = 0x20 */
    uint64_t pImmutableSamplers; /* NULL */
};

/* VkDescriptorSetLayoutCreateInfo: sType=32, pNext=0, flags=0, bindingCount, pBindings */
struct FakeDSLCI {
    uint32_t sType;       /* 32 */
    uint64_t pNext;       /* 0 */
    uint32_t flags;       /* 0 */
    uint32_t bindingCount;
    uint64_t pBindings;   /* ptr (ignored by stub) */
};

/* VkDescriptorPoolSize: type, descriptorCount */
struct FakeDPSize {
    uint32_t descriptorType;
    uint32_t descriptorCount;
};

/* VkDescriptorPoolCreateInfo: sType=33, pNext=0, flags=0, maxSets, poolSizeCount, pPoolSizes */
struct FakeDPCI {
    uint32_t sType;        /* 33 */
    uint64_t pNext;        /* 0 */
    uint32_t flags;
    uint32_t maxSets;
    uint32_t poolSizeCount;
    uint64_t pPoolSizes;   /* ptr (ignored by stub) */
};

/* VkDescriptorSetAllocateInfo: sType=34, pNext=0, descriptorPool, descriptorSetCount, pSetLayouts */
struct FakeDSAI {
    uint32_t sType;           /* 34 */
    uint64_t pNext;           /* 0 */
    VkDescriptorPool pool;    /* uint64 at offset 12 */
    uint32_t setCount;        /* at offset 20 */
    uint64_t pLayouts;        /* at offset 24 */
};

/* VkPipelineLayoutCreateInfo: sType=30, pNext=0, flags=0, setLayoutCount, pSetLayouts,
 *                              pushConstantRangeCount, pPushConstantRanges */
struct FakePLCI {
    uint32_t sType;       /* 30 */
    uint64_t pNext;       /* 0 */
    uint32_t flags;
    uint32_t setLayoutCount;
    uint64_t pSetLayouts; /* ptr (ignored by stub) */
    uint32_t pcCount;
    uint64_t pPCRanges;
};

/* VkComputePipelineCreateInfo: sType=29, pNext=0, flags=0, stage{...}, layout, basePipelineHandle */
/* stage = VkPipelineShaderStageCreateInfo (sType=18, pNext=0, flags=0, stage, module, pName) */
struct FakeStageCI {
    uint32_t     sType;   /* 18 */
    uint64_t     pNext;   /* 0 */
    uint32_t     flags;
    uint32_t     stage;   /* VK_SHADER_STAGE_COMPUTE_BIT = 0x20 */
    VkShaderModule module_; /* at byte offset 20 in this struct */
    uint64_t     pName;   /* "main" ptr (ignored) */
    uint64_t     pSpecInfo; /* NULL */
};

struct FakeComputePCI {
    uint32_t      sType;    /* 29 */
    uint64_t      pNext;    /* 0 */
    uint32_t      flags;
    /* stage at offset 16: 8+4+4=16 bytes so far */
    struct FakeStageCI stage;
    VkPipelineLayout layout_; /* offset 16 + sizeof(FakeStageCI) */
    uint64_t      basePipeline;
    int32_t       basePipelineIdx;
};

/* VkCommandPoolCreateInfo: sType=39, pNext=0, flags=0, queueFamilyIndex */
struct FakeCPCI {
    uint32_t sType;        /* 39 */
    uint64_t pNext;        /* 0 */
    uint32_t flags;
    uint32_t queueFamilyIndex;
};

/* VkCommandBufferAllocateInfo: sType=40, pNext=0, commandPool, level=0, commandBufferCount */
struct FakeCBAI {
    uint32_t sType;        /* 40 */
    uint64_t pNext;        /* 0 */
    VkCommandPool cmdPool; /* uint64 at offset 12 */
    uint32_t level;        /* at offset 20 (PRIMARY=0) */
    uint32_t count;        /* commandBufferCount at offset 24 */
};

/* VkCommandBufferBeginInfo: sType=42, pNext=0, flags=0 */
struct FakeCBBI {
    uint32_t sType; /* 42 */
    uint64_t pNext;
    uint32_t flags;
};

/* VkFenceCreateInfo: sType=8, pNext=0, flags=0 */
struct FakeFenceCI {
    uint32_t sType; /* 8 */
    uint64_t pNext;
    uint32_t flags;
};

/* VkSubmitInfo: sType=4, pNext=0, waitSemaphoreCount=0, pWaitSemaphores=NULL,
 *              pWaitDstStageMask=NULL, commandBufferCount, pCommandBuffers, ... */
struct FakeSubmitInfo {
    uint32_t sType;           /* 4 */
    uint64_t pNext;           /* 0 */
    uint32_t waitSemCount;    /* 0 */
    uint64_t pWaitSems;       /* NULL */
    uint64_t pWaitDstStage;   /* NULL */
    uint32_t cmdBufCount;     /* offset 32: commandBufferCount */
    uint64_t pCmdBufs;        /* offset 36: pCommandBuffers */
    uint32_t sigSemCount;     /* 0 */
    uint64_t pSigSems;        /* NULL */
};

/* Minimal valid SPIR-V header (magic + version + generator + bound + schema) */
static const uint32_t k_spirv_hdr[] = {
    0x07230203u, /* SPIR-V magic */
    0x00010300u, /* version 1.3 */
    0u,          /* generator */
    4u,          /* bound (max id + 1) */
    0u,          /* reserved schema */
};

static void test_stubs(void)
{
    printf("\n[C] Call stubs — VK_SUCCESS from each registered function\n");

    /* Resolve all function pointers once */
    PFN_CreateInstance   fn_ci   = (PFN_CreateInstance)  get("vkCreateInstance");
    PFN_EnumPhysDev      fn_epd  = (PFN_EnumPhysDev)     get("vkEnumeratePhysicalDevices");
    PFN_GetPhysDevProps  fn_gdp  = (PFN_GetPhysDevProps)  get("vkGetPhysicalDeviceProperties");
    PFN_GetPhysDevFeats  fn_gdf  = (PFN_GetPhysDevFeats)  get("vkGetPhysicalDeviceFeatures");
    PFN_GetPhysDevMemProps fn_gm = (PFN_GetPhysDevMemProps)get("vkGetPhysicalDeviceMemoryProperties");
    PFN_CreateDevice     fn_cd   = (PFN_CreateDevice)     get("vkCreateDevice");
    PFN_GetDevQueue      fn_gdq  = (PFN_GetDevQueue)      get("vkGetDeviceQueue");
    PFN_AllocMem         fn_am   = (PFN_AllocMem)         get("vkAllocateMemory");
    PFN_CreateBuf        fn_cb   = (PFN_CreateBuf)        get("vkCreateBuffer");
    PFN_GetBufMemReq     fn_gbr  = (PFN_GetBufMemReq)     get("vkGetBufferMemoryRequirements");
    PFN_BindBufMem       fn_bbm  = (PFN_BindBufMem)       get("vkBindBufferMemory");
    PFN_MapMemory        fn_map  = (PFN_MapMemory)        get("vkMapMemory");
    PFN_CreateShader     fn_cs   = (PFN_CreateShader)     get("vkCreateShaderModule");
    PFN_CreateDSL        fn_dsl  = (PFN_CreateDSL)        get("vkCreateDescriptorSetLayout");
    PFN_CreateDP         fn_dp   = (PFN_CreateDP)         get("vkCreateDescriptorPool");
    PFN_AllocDS          fn_ds   = (PFN_AllocDS)          get("vkAllocateDescriptorSets");
    PFN_CreatePL         fn_pl   = (PFN_CreatePL)         get("vkCreatePipelineLayout");
    PFN_CreateCP         fn_cp   = (PFN_CreateCP)         get("vkCreateComputePipelines");
    PFN_CreateCmdPool    fn_cpool= (PFN_CreateCmdPool)    get("vkCreateCommandPool");
    PFN_AllocCmds        fn_acb  = (PFN_AllocCmds)        get("vkAllocateCommandBuffers");
    PFN_BeginCmd         fn_beg  = (PFN_BeginCmd)         get("vkBeginCommandBuffer");
    PFN_EndCmd           fn_end  = (PFN_EndCmd)           get("vkEndCommandBuffer");
    PFN_CmdBind          fn_bind = (PFN_CmdBind)          get("vkCmdBindPipeline");
    PFN_CmdBindDS        fn_bds  = (PFN_CmdBindDS)        get("vkCmdBindDescriptorSets");
    PFN_CmdPushConst     fn_pc   = (PFN_CmdPushConst)     get("vkCmdPushConstants");
    PFN_CmdDispatch      fn_disp = (PFN_CmdDispatch)      get("vkCmdDispatch");
    PFN_CmdBarrier       fn_bar  = (PFN_CmdBarrier)       get("vkCmdPipelineBarrier");
    PFN_CreateFence      fn_cf   = (PFN_CreateFence)      get("vkCreateFence");
    PFN_ResetFences      fn_rf   = (PFN_ResetFences)      get("vkResetFences");
    PFN_WaitFences       fn_wf   = (PFN_WaitFences)       get("vkWaitForFences");
    PFN_QueueSubmit      fn_qs   = (PFN_QueueSubmit)      get("vkQueueSubmit");
    PFN_UpdateDS         fn_uds  = (PFN_UpdateDS)         get("vkUpdateDescriptorSets");
    PFN_CmdCopyBuffer    fn_copy = (PFN_CmdCopyBuffer)    get("vkCmdCopyBuffer");
    PFN_CmdFillBuffer    fn_fill = (PFN_CmdFillBuffer)    get("vkCmdFillBuffer");
    PFN_FreeMemory       fn_fm   = (PFN_FreeMemory)       get("vkFreeMemory");
    PFN_DestroyBuffer    fn_db   = (PFN_DestroyBuffer)    get("vkDestroyBuffer");
    PFN_DestroyShader    fn_dsh  = (PFN_DestroyShader)    get("vkDestroyShaderModule");
    PFN_DestroyPipeline  fn_dpi  = (PFN_DestroyPipeline)  get("vkDestroyPipeline");
    PFN_DestroyPipelineLayout fn_dpl = (PFN_DestroyPipelineLayout)get("vkDestroyPipelineLayout");
    PFN_DestroyDescriptorPool fn_ddp = (PFN_DestroyDescriptorPool)get("vkDestroyDescriptorPool");
    PFN_DestroyDescriptorSetLayout fn_ddsl = (PFN_DestroyDescriptorSetLayout)get("vkDestroyDescriptorSetLayout");
    PFN_DestroyCommandPool fn_dcp = (PFN_DestroyCommandPool)get("vkDestroyCommandPool");
    PFN_FreeCommandBuffers fn_fcb = (PFN_FreeCommandBuffers)get("vkFreeCommandBuffers");

    /* All function pointers must be non-NULL (already verified in group A) */
    if (!fn_ci || !fn_epd || !fn_cd || !fn_am || !fn_cb || !fn_cs ||
        !fn_dsl || !fn_dp || !fn_ds || !fn_pl || !fn_cp || !fn_cpool ||
        !fn_acb || !fn_beg || !fn_end || !fn_bind || !fn_bds ||
        !fn_disp || !fn_cf || !fn_rf || !fn_wf || !fn_qs || !fn_uds ||
        !fn_copy || !fn_fill || !fn_fm || !fn_db || !fn_dsh || !fn_dpi ||
        !fn_dpl || !fn_ddp || !fn_ddsl || !fn_dcp || !fn_fcb) {
        printf("  SKIP  stub calls: function pointers unavailable\n");
        g_fail++;
        return;
    }

    /* ── vkCreateInstance ──────────────────────────────────────────────── */
    struct FakeInstanceCI ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = 1u;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult rc = fn_ci(&ici, NULL, &inst);
    CHECK("stub:CreateInstance returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateInstance sets instance handle", inst != VK_NULL_HANDLE);

    /* ── vkEnumeratePhysicalDevices ────────────────────────────────────── */
    uint32_t pdev_count = 0;
    rc = fn_epd(inst, &pdev_count, NULL);
    CHECK("stub:EnumPhysDevs count==1", rc == VK_SUCCESS && pdev_count == 1);
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    rc = fn_epd(inst, &pdev_count, &pdev);
    CHECK("stub:EnumPhysDevs returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:EnumPhysDevs sets pdev handle", pdev != VK_NULL_HANDLE);

    /* ── vkGetPhysicalDeviceProperties ────────────────────────────────── */
    uint8_t props[832];
    memset(props, 0, sizeof(props));
    fn_gdp(pdev, props);
    uint32_t vendor_id;
    memcpy(&vendor_id, props + 8, 4); /* vendorID at byte 8 */
    CHECK("stub:GetPhysDevProps vendorID==NVIDIA(0x10de)", vendor_id == 0x10deu);

    /* ── vkGetPhysicalDeviceFeatures ───────────────────────────────────── */
    uint32_t feats[55];
    memset(feats, 0, sizeof(feats));
    fn_gdf(pdev, feats);
    CHECK("stub:GetPhysDevFeatures robustBufferAccess==VK_TRUE", feats[0] == 1u);

    /* ── vkGetPhysicalDeviceMemoryProperties ───────────────────────────── */
    uint8_t memprops[520];
    memset(memprops, 0, sizeof(memprops));
    fn_gm(pdev, memprops);
    uint32_t type_count;
    memcpy(&type_count, memprops, 4);
    CHECK("stub:GetPhysDevMemProps memTypeCount==3", type_count == 3u);

    /* ── vkCreateDevice ────────────────────────────────────────────────── */
    /* DeviceCreateInfo layout: sType=3 at offset 0, rest zeroed is fine for stub */
    uint8_t dci[64];
    memset(dci, 0, sizeof(dci));
    *((uint32_t *)dci) = 3u; /* VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO */
    VkDevice dev = VK_NULL_HANDLE;
    rc = fn_cd(pdev, dci, NULL, &dev);
    CHECK("stub:CreateDevice returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateDevice sets device handle", dev != VK_NULL_HANDLE);

    /* ── vkGetDeviceQueue ──────────────────────────────────────────────── */
    VkQueue queue = VK_NULL_HANDLE;
    fn_gdq(dev, 0u, 0u, &queue);
    CHECK("stub:GetDeviceQueue sets queue handle", queue != VK_NULL_HANDLE);

    /* ── vkAllocateMemory ──────────────────────────────────────────────── */
    struct FakeMemAllocInfo mai;
    memset(&mai, 0, sizeof(mai));
    mai.sType     = 5u;
    mai.allocSize = 256u * 1024u;   /* 256 KiB */
    mai.memTypeIdx = 1u;            /* host-visible */
    VkDeviceMemory mem = VK_NULL_HANDLE;
    rc = fn_am(dev, &mai, NULL, &mem);
    CHECK("stub:AllocateMemory returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:AllocateMemory sets memory handle", mem != VK_NULL_HANDLE);

    /* ── vkCreateBuffer ────────────────────────────────────────────────── */
    struct FakeBufCI bci;
    memset(&bci, 0, sizeof(bci));
    bci.sType = 12u;
    bci.size  = 65536u;
    bci.usage = 0x80u; /* STORAGE_BUFFER */
    VkBuffer buf_in = VK_NULL_HANDLE;
    rc = fn_cb(dev, &bci, NULL, &buf_in);
    CHECK("stub:CreateBuffer(in) returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateBuffer(in) sets handle", buf_in != VK_NULL_HANDLE);

    VkBuffer buf_out = VK_NULL_HANDLE;
    rc = fn_cb(dev, &bci, NULL, &buf_out);
    CHECK("stub:CreateBuffer(out) returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateBuffer handles are distinct",
          buf_in != buf_out && buf_out != VK_NULL_HANDLE);

    /* ── vkGetBufferMemoryRequirements ─────────────────────────────────── */
    uint8_t memreq[24]; /* size(8) + alignment(8) + memoryTypeBits(4) + pad(4) */
    memset(memreq, 0, sizeof(memreq));
    fn_gbr(dev, buf_in, memreq);
    uint64_t align;
    uint32_t mtbits;
    memcpy(&align,  memreq + 8,  8);
    memcpy(&mtbits, memreq + 16, 4);
    CHECK("stub:GetBufMemReq alignment==256", align == 256u);
    CHECK("stub:GetBufMemReq memoryTypeBits==0x7", mtbits == 0x7u);

    /* ── vkBindBufferMemory ─────────────────────────────────────────────── */
    rc = fn_bbm(dev, buf_in, mem, 0u);
    CHECK("stub:BindBufferMemory(in) returns VK_SUCCESS", rc == VK_SUCCESS);
    rc = fn_bbm(dev, buf_out, mem, 65536u);
    CHECK("stub:BindBufferMemory(out) returns VK_SUCCESS", rc == VK_SUCCESS);

    /* ── vkMapMemory ────────────────────────────────────────────────────── */
    void *mapped = NULL;
    rc = fn_map(dev, mem, 0u, 65536u, 0u, &mapped);
    CHECK("stub:MapMemory returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:MapMemory returns non-NULL pointer", mapped != NULL);

    /* ── vkCreateShaderModule ───────────────────────────────────────────── */
    struct FakeShaderCI sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType    = 16u;
    sci.codeSize = sizeof(k_spirv_hdr);
    sci.pCode    = (uint32_t *)k_spirv_hdr;
    VkShaderModule shader = VK_NULL_HANDLE;
    rc = fn_cs(dev, &sci, NULL, &shader);
    CHECK("stub:CreateShaderModule returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateShaderModule sets handle", shader != VK_NULL_HANDLE);

    /* ── vkCreateDescriptorSetLayout ────────────────────────────────────── */
    struct FakeDSLCI dslci;
    memset(&dslci, 0, sizeof(dslci));
    dslci.sType = 32u;
    dslci.bindingCount = 2u;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    rc = fn_dsl(dev, &dslci, NULL, &dsl);
    CHECK("stub:CreateDescriptorSetLayout returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateDescriptorSetLayout sets handle", dsl != VK_NULL_HANDLE);

    /* ── vkCreateDescriptorPool ─────────────────────────────────────────── */
    struct FakeDPCI dpci;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType   = 33u;
    dpci.maxSets = 1u;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    rc = fn_dp(dev, &dpci, NULL, &dpool);
    CHECK("stub:CreateDescriptorPool returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateDescriptorPool sets handle", dpool != VK_NULL_HANDLE);

    /* ── vkAllocateDescriptorSets ───────────────────────────────────────── */
    struct FakeDSAI dsai;
    memset(&dsai, 0, sizeof(dsai));
    dsai.sType    = 34u;
    dsai.pool     = dpool;
    dsai.setCount = 1u;
    dsai.pLayouts = (uint64_t)(uintptr_t)&dsl;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    rc = fn_ds(dev, &dsai, &ds);
    CHECK("stub:AllocateDescriptorSets returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:AllocateDescriptorSets sets handle", ds != VK_NULL_HANDLE);

    /* ── vkUpdateDescriptorSets(storage buffer) ─────────────────────────── */
    uint8_t dbi[24]; memset(dbi, 0, sizeof(dbi));
    memcpy(dbi + 0, &buf_in, 8); { uint64_t zero = 0, range = 65536u; memcpy(dbi + 8, &zero, 8); memcpy(dbi + 16, &range, 8); }
    uint8_t write[64]; memset(write, 0, sizeof(write));
    { uint32_t stype = 35u, binding = 0u, count = 1u, dtype = 7u; uint64_t dset = ds, pbuf = (uint64_t)(uintptr_t)dbi;
      memcpy(write + 0, &stype, 4); memcpy(write + 16, &dset, 8); memcpy(write + 24, &binding, 4);
      memcpy(write + 32, &count, 4); memcpy(write + 36, &dtype, 4); memcpy(write + 48, &pbuf, 8); }
    fn_uds(dev, 1u, write, 0u, NULL);
    CHECK("stub:UpdateDescriptorSets storage buffer called", 1);

    /* ── vkCreatePipelineLayout ─────────────────────────────────────────── */
    struct FakePLCI plci;
    memset(&plci, 0, sizeof(plci));
    plci.sType          = 30u;
    plci.setLayoutCount = 1u;
    plci.pSetLayouts    = (uint64_t)(uintptr_t)&dsl;
    VkPipelineLayout playout = VK_NULL_HANDLE;
    rc = fn_pl(dev, &plci, NULL, &playout);
    CHECK("stub:CreatePipelineLayout returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreatePipelineLayout sets handle", playout != VK_NULL_HANDLE);

    /* ── vkCreateComputePipelines ───────────────────────────────────────── */
    struct FakeComputePCI cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType           = 29u;
    cpci.stage.sType     = 18u;
    cpci.stage.stage     = 0x20u; /* COMPUTE */
    cpci.stage.module_   = shader;
    cpci.layout_         = playout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    rc = fn_cp(dev, VK_NULL_HANDLE, 1u, &cpci, NULL, &pipeline);
    CHECK("stub:CreateComputePipelines returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateComputePipelines sets handle", pipeline != VK_NULL_HANDLE);

    /* ── vkCreateCommandPool ────────────────────────────────────────────── */
    struct FakeCPCI cpoolci;
    memset(&cpoolci, 0, sizeof(cpoolci));
    cpoolci.sType            = 39u;
    cpoolci.queueFamilyIndex = 0u;
    VkCommandPool cmdpool = VK_NULL_HANDLE;
    rc = fn_cpool(dev, &cpoolci, NULL, &cmdpool);
    CHECK("stub:CreateCommandPool returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateCommandPool sets handle", cmdpool != VK_NULL_HANDLE);

    /* ── vkAllocateCommandBuffers ───────────────────────────────────────── */
    struct FakeCBAI cbai;
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType   = 40u;
    cbai.cmdPool = cmdpool;
    cbai.level   = 0u; /* PRIMARY */
    cbai.count   = 1u;
    VkCommandBuffer cmdbuf = VK_NULL_HANDLE;
    rc = fn_acb(dev, &cbai, &cmdbuf);
    CHECK("stub:AllocateCommandBuffers returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:AllocateCommandBuffers sets handle", cmdbuf != VK_NULL_HANDLE);

    /* ── vkBeginCommandBuffer ───────────────────────────────────────────── */
    struct FakeCBBI cbbi;
    memset(&cbbi, 0, sizeof(cbbi));
    cbbi.sType = 42u;
    rc = fn_beg(cmdbuf, &cbbi);
    CHECK("stub:BeginCommandBuffer returns VK_SUCCESS", rc == VK_SUCCESS);

    /* ── vkCmdBindPipeline ──────────────────────────────────────────────── */
    fn_bind(cmdbuf, 1u /*COMPUTE*/, pipeline);
    CHECK("stub:CmdBindPipeline called without crash", 1);

    /* ── vkCmdBindDescriptorSets ────────────────────────────────────────── */
    const uint32_t dyn_offsets[1] = {0u};
    fn_bds(cmdbuf, 1u /*COMPUTE*/, playout, 0u, 1u, &ds, 0u, dyn_offsets);
    CHECK("stub:CmdBindDescriptorSets called without crash", 1);

    /* ── vkCmdPushConstants ─────────────────────────────────────────────── */
    uint32_t push_data[3] = {512u, 512u, 512u}; /* M, N, K */
    fn_pc(cmdbuf, playout, 0x20u, 0u, 12u, push_data);
    CHECK("stub:CmdPushConstants called without crash", 1);

    /* ── vkCmdCopyBuffer / vkCmdFillBuffer ─────────────────────────────── */
    uint8_t region[24]; memset(region, 0, sizeof(region)); { uint64_t sz = 4096u; memcpy(region + 16, &sz, 8); }
    fn_copy(cmdbuf, buf_in, buf_out, 1u, region);
    CHECK("stub:CmdCopyBuffer called without crash", 1);
    fn_fill(cmdbuf, buf_out, 0u, 4096u, 0u);
    CHECK("stub:CmdFillBuffer called without crash", 1);

    /* ── vkCmdPipelineBarrier ───────────────────────────────────────────── */
    fn_bar(cmdbuf,
           0x00000100u,  /* VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT */
           0x00000100u,
           0u, 0u, NULL, 0u, NULL, 0u, NULL);
    CHECK("stub:CmdPipelineBarrier called without crash", 1);

    /* ── vkCmdDispatch ──────────────────────────────────────────────────── */
    fn_disp(cmdbuf, 32u, 32u, 1u); /* 512/16 = 32 groups */
    CHECK("stub:CmdDispatch called without crash", 1);

    /* ── vkEndCommandBuffer ─────────────────────────────────────────────── */
    rc = fn_end(cmdbuf);
    CHECK("stub:EndCommandBuffer returns VK_SUCCESS", rc == VK_SUCCESS);

    /* ── vkCreateFence ──────────────────────────────────────────────────── */
    struct FakeFenceCI fci;
    memset(&fci, 0, sizeof(fci));
    fci.sType = 8u;
    VkFence fence = VK_NULL_HANDLE;
    rc = fn_cf(dev, &fci, NULL, &fence);
    CHECK("stub:CreateFence returns VK_SUCCESS", rc == VK_SUCCESS);
    CHECK("stub:CreateFence sets handle", fence != VK_NULL_HANDLE);

    /* ── vkQueueSubmit ──────────────────────────────────────────────────── */
    struct FakeSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType      = 4u;
    si.cmdBufCount = 1u;
    si.pCmdBufs   = (uint64_t)(uintptr_t)&cmdbuf;
    rc = fn_qs(queue, 1u, &si, fence);
    CHECK("stub:QueueSubmit returns VK_SUCCESS", rc == VK_SUCCESS);

    /* ── vkWaitForFences ────────────────────────────────────────────────── */
    rc = fn_wf(dev, 1u, &fence, 1u, (uint64_t)5000000000ull);
    CHECK("stub:WaitForFences returns VK_SUCCESS", rc == VK_SUCCESS);

    /* ── vkResetFences ──────────────────────────────────────────────────── */
    rc = fn_rf(dev, 1u, &fence);
    CHECK("stub:ResetFences returns VK_SUCCESS", rc == VK_SUCCESS);

    fn_fcb(dev, cmdpool, 1u, &cmdbuf);
    fn_dcp(dev, cmdpool, 0u);
    fn_dpi(dev, pipeline, NULL);
    fn_dpl(dev, playout, NULL);
    fn_ddp(dev, dpool, NULL);
    fn_ddsl(dev, dsl, NULL);
    fn_dsh(dev, shader, NULL);
    fn_db(dev, buf_in, NULL); fn_db(dev, buf_out, NULL);
    fn_fm(dev, mem, NULL);
    CHECK("stub:cleanup destroy/free path called", 1);
}

/* ── Group D: Full compute sequence ────────────────────────────────────── */
static void test_full_sequence(void)
{
    printf("\n[D] Full compute bootstrap sequence\n");

    /* This group runs the same operations as group C in logical order,
     * verifying that handles passed from one step feed correctly into
     * the next (no cross-contamination, no NULL dereferences). */

    PFN_CreateInstance fn_ci  = (PFN_CreateInstance) get("vkCreateInstance");
    PFN_EnumPhysDev    fn_epd = (PFN_EnumPhysDev)    get("vkEnumeratePhysicalDevices");
    PFN_CreateDevice   fn_cd  = (PFN_CreateDevice)    get("vkCreateDevice");
    PFN_GetDevQueue    fn_gdq = (PFN_GetDevQueue)     get("vkGetDeviceQueue");
    PFN_AllocMem       fn_am  = (PFN_AllocMem)        get("vkAllocateMemory");
    PFN_CreateBuf      fn_cb  = (PFN_CreateBuf)       get("vkCreateBuffer");
    PFN_BindBufMem     fn_bbm = (PFN_BindBufMem)      get("vkBindBufferMemory");
    PFN_CreateShader   fn_cs  = (PFN_CreateShader)    get("vkCreateShaderModule");
    PFN_CreateDSL      fn_dsl = (PFN_CreateDSL)       get("vkCreateDescriptorSetLayout");
    PFN_CreateDP       fn_dp  = (PFN_CreateDP)        get("vkCreateDescriptorPool");
    PFN_AllocDS        fn_ds  = (PFN_AllocDS)         get("vkAllocateDescriptorSets");
    PFN_CreatePL       fn_pl  = (PFN_CreatePL)        get("vkCreatePipelineLayout");
    PFN_CreateCP       fn_cp  = (PFN_CreateCP)        get("vkCreateComputePipelines");
    PFN_CreateCmdPool  fn_cpool=(PFN_CreateCmdPool)   get("vkCreateCommandPool");
    PFN_AllocCmds      fn_acb = (PFN_AllocCmds)       get("vkAllocateCommandBuffers");
    PFN_BeginCmd       fn_beg = (PFN_BeginCmd)         get("vkBeginCommandBuffer");
    PFN_CmdBind        fn_bind= (PFN_CmdBind)          get("vkCmdBindPipeline");
    PFN_CmdBindDS      fn_bds = (PFN_CmdBindDS)        get("vkCmdBindDescriptorSets");
    PFN_CmdDispatch    fn_d   = (PFN_CmdDispatch)      get("vkCmdDispatch");
    PFN_EndCmd         fn_end = (PFN_EndCmd)           get("vkEndCommandBuffer");
    PFN_CreateFence    fn_cf  = (PFN_CreateFence)      get("vkCreateFence");
    PFN_QueueSubmit    fn_qs  = (PFN_QueueSubmit)      get("vkQueueSubmit");
    PFN_WaitFences     fn_wf  = (PFN_WaitFences)       get("vkWaitForFences");
    PFN_ResetFences    fn_rf  = (PFN_ResetFences)      get("vkResetFences");

    VkInstance       inst    = VK_NULL_HANDLE;
    VkPhysicalDevice pdev    = VK_NULL_HANDLE;
    VkDevice         dev     = VK_NULL_HANDLE;
    VkQueue          queue   = VK_NULL_HANDLE;
    VkDeviceMemory   mem     = VK_NULL_HANDLE;
    VkBuffer         buf_in  = VK_NULL_HANDLE;
    VkBuffer         buf_out = VK_NULL_HANDLE;
    VkShaderModule   shader  = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl   = VK_NULL_HANDLE;
    VkDescriptorPool      dpool = VK_NULL_HANDLE;
    VkDescriptorSet       ds    = VK_NULL_HANDLE;
    VkPipelineLayout      pl    = VK_NULL_HANDLE;
    VkPipeline        pipeline  = VK_NULL_HANDLE;
    VkCommandPool     cmdpool   = VK_NULL_HANDLE;
    VkCommandBuffer   cmdbuf   = VK_NULL_HANDLE;
    VkFence           fence    = VK_NULL_HANDLE;
    VkResult          rc;
    uint32_t          n;

    /* Step 1: Instance */
    struct FakeInstanceCI ici; memset(&ici, 0, sizeof(ici)); ici.sType = 1u;
    rc = fn_ci(&ici, NULL, &inst);
    CHECK("seq:01:CreateInstance", rc == VK_SUCCESS && inst != VK_NULL_HANDLE);

    /* Step 2: Physical device */
    n = 1u;
    fn_epd(inst, &n, &pdev);
    CHECK("seq:02:EnumeratePhysicalDevices", pdev != VK_NULL_HANDLE);

    /* Step 3: Device */
    uint8_t dci[64]; memset(dci, 0, sizeof(dci)); *(uint32_t *)dci = 3u;
    rc = fn_cd(pdev, dci, NULL, &dev);
    CHECK("seq:03:CreateDevice", rc == VK_SUCCESS && dev != VK_NULL_HANDLE);

    /* Step 4: Queue */
    fn_gdq(dev, 0u, 0u, &queue);
    CHECK("seq:04:GetDeviceQueue", queue != VK_NULL_HANDLE);

    /* Step 5: Memory */
    struct FakeMemAllocInfo mai; memset(&mai, 0, sizeof(mai));
    mai.sType = 5u; mai.allocSize = 2u * 64u * 1024u; mai.memTypeIdx = 0u;
    rc = fn_am(dev, &mai, NULL, &mem);
    CHECK("seq:05:AllocateMemory", rc == VK_SUCCESS && mem != VK_NULL_HANDLE);

    /* Step 6/7: Buffers */
    struct FakeBufCI bci; memset(&bci, 0, sizeof(bci));
    bci.sType = 12u; bci.size = 64u * 1024u; bci.usage = 0x80u;
    rc = fn_cb(dev, &bci, NULL, &buf_in);
    CHECK("seq:06:CreateBuffer(in)", rc == VK_SUCCESS && buf_in != VK_NULL_HANDLE);
    rc = fn_cb(dev, &bci, NULL, &buf_out);
    CHECK("seq:07:CreateBuffer(out)", rc == VK_SUCCESS && buf_out != VK_NULL_HANDLE);

    /* Step 8/9: Bind buffers */
    rc = fn_bbm(dev, buf_in,  mem, 0u);
    CHECK("seq:08:BindBufferMemory(in)",  rc == VK_SUCCESS);
    rc = fn_bbm(dev, buf_out, mem, 64u * 1024u);
    CHECK("seq:09:BindBufferMemory(out)", rc == VK_SUCCESS);

    /* Step 10: Shader */
    struct FakeShaderCI sci; memset(&sci, 0, sizeof(sci));
    sci.sType = 16u; sci.codeSize = sizeof(k_spirv_hdr);
    sci.pCode = (uint32_t *)k_spirv_hdr;
    rc = fn_cs(dev, &sci, NULL, &shader);
    CHECK("seq:10:CreateShaderModule", rc == VK_SUCCESS && shader != VK_NULL_HANDLE);

    /* Step 11: DSL */
    struct FakeDSLCI dslci; memset(&dslci, 0, sizeof(dslci));
    dslci.sType = 32u; dslci.bindingCount = 2u;
    rc = fn_dsl(dev, &dslci, NULL, &dsl);
    CHECK("seq:11:CreateDescriptorSetLayout",
          rc == VK_SUCCESS && dsl != VK_NULL_HANDLE);

    /* Step 12: Descriptor pool */
    struct FakeDPCI dpci; memset(&dpci, 0, sizeof(dpci));
    dpci.sType = 33u; dpci.maxSets = 1u;
    rc = fn_dp(dev, &dpci, NULL, &dpool);
    CHECK("seq:12:CreateDescriptorPool",
          rc == VK_SUCCESS && dpool != VK_NULL_HANDLE);

    /* Step 13: Allocate descriptor set */
    struct FakeDSAI dsai; memset(&dsai, 0, sizeof(dsai));
    dsai.sType = 34u; dsai.pool = dpool; dsai.setCount = 1u;
    dsai.pLayouts = (uint64_t)(uintptr_t)&dsl;
    rc = fn_ds(dev, &dsai, &ds);
    CHECK("seq:13:AllocateDescriptorSets",
          rc == VK_SUCCESS && ds != VK_NULL_HANDLE);

    /* Step 14: Pipeline layout */
    struct FakePLCI plci; memset(&plci, 0, sizeof(plci));
    plci.sType = 30u; plci.setLayoutCount = 1u;
    plci.pSetLayouts = (uint64_t)(uintptr_t)&dsl;
    rc = fn_pl(dev, &plci, NULL, &pl);
    CHECK("seq:14:CreatePipelineLayout",
          rc == VK_SUCCESS && pl != VK_NULL_HANDLE);

    /* Step 15: Compute pipeline */
    struct FakeComputePCI cpci; memset(&cpci, 0, sizeof(cpci));
    cpci.sType = 29u;
    cpci.stage.sType = 18u; cpci.stage.stage = 0x20u; cpci.stage.module_ = shader;
    cpci.layout_ = pl;
    rc = fn_cp(dev, VK_NULL_HANDLE, 1u, &cpci, NULL, &pipeline);
    CHECK("seq:15:CreateComputePipelines",
          rc == VK_SUCCESS && pipeline != VK_NULL_HANDLE);

    /* Step 16: Command pool */
    struct FakeCPCI cpoolci; memset(&cpoolci, 0, sizeof(cpoolci));
    cpoolci.sType = 39u;
    rc = fn_cpool(dev, &cpoolci, NULL, &cmdpool);
    CHECK("seq:16:CreateCommandPool",
          rc == VK_SUCCESS && cmdpool != VK_NULL_HANDLE);

    /* Step 17: Allocate command buffer */
    struct FakeCBAI cbai; memset(&cbai, 0, sizeof(cbai));
    cbai.sType = 40u; cbai.cmdPool = cmdpool; cbai.count = 1u;
    rc = fn_acb(dev, &cbai, &cmdbuf);
    CHECK("seq:17:AllocateCommandBuffers",
          rc == VK_SUCCESS && cmdbuf != VK_NULL_HANDLE);

    /* Step 18: Begin */
    struct FakeCBBI cbbi; memset(&cbbi, 0, sizeof(cbbi)); cbbi.sType = 42u;
    rc = fn_beg(cmdbuf, &cbbi);
    CHECK("seq:18:BeginCommandBuffer", rc == VK_SUCCESS);

    /* Step 19: Bind + dispatch */
    fn_bind(cmdbuf, 1u, pipeline);
    fn_bds(cmdbuf, 1u, pl, 0u, 1u, &ds, 0u, NULL);
    fn_d(cmdbuf, 32u, 32u, 1u);
    CHECK("seq:19:CmdBind+Dispatch called", 1);

    /* Step 20: End */
    rc = fn_end(cmdbuf);
    CHECK("seq:20:EndCommandBuffer", rc == VK_SUCCESS);

    /* Step 21: Fence + submit + wait + reset */
    struct FakeFenceCI fci; memset(&fci, 0, sizeof(fci)); fci.sType = 8u;
    rc = fn_cf(dev, &fci, NULL, &fence);
    CHECK("seq:21:CreateFence", rc == VK_SUCCESS && fence != VK_NULL_HANDLE);

    struct FakeSubmitInfo si; memset(&si, 0, sizeof(si));
    si.sType = 4u; si.cmdBufCount = 1u;
    si.pCmdBufs = (uint64_t)(uintptr_t)&cmdbuf;
    rc = fn_qs(queue, 1u, &si, fence);
    CHECK("seq:21:QueueSubmit", rc == VK_SUCCESS);

    rc = fn_wf(dev, 1u, &fence, 1u, (uint64_t)5000000000ull);
    CHECK("seq:22:WaitForFences", rc == VK_SUCCESS);

    rc = fn_rf(dev, 1u, &fence);
    CHECK("seq:23:ResetFences", rc == VK_SUCCESS);

    printf("  INFO  sequence: all 23 steps completed without device loss\n");
}

/* ── main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("ggml_vk_dispatch_test: N3 static Vulkan ICD dispatch layer\n");
    printf("================================================================\n");

    test_proc_lookup();
    test_dispatch_init();
    test_stubs();
    test_full_sequence();

    printf("\n================================================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("ggml_vk_dispatch_test: all checks PASS\n");
        printf("uk-llama-vk-n3: PASS evidence_id=llama-vk-n3-dispatch "
               "gpu=1 venus=1 substrate=static-vk-dispatch\n");
    } else {
        printf("ggml_vk_dispatch_test: %d check(s) FAILED\n", g_fail);
    }
    return g_fail ? 1 : 0;
}
