#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <uk/mutex.h>
#include <uk/virtio_gpu.h>
#include <uk/vulkan_venus.h>
#include <uk/venus.h>
#include <uk/vulkan.h>
#include "vk_state.h"


/* Minimal Vulkan type definitions */
typedef uint64_t VkInstance;
typedef uint64_t VkPhysicalDevice;
typedef uint64_t VkDevice;
typedef uint64_t VkQueue;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkBuffer;
typedef uint64_t VkImage;
typedef uint64_t VkShaderModule;
typedef uint64_t VkDescriptorSetLayout;
typedef uint64_t VkDescriptorPool;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkPipelineLayout;
typedef uint64_t VkPipeline;
typedef uint64_t VkCommandPool;
typedef uint64_t VkCommandBuffer;
typedef uint64_t VkFence;
typedef uint64_t VkSemaphore;
typedef uint64_t VkEvent;
typedef uint64_t VkQueryPool;
typedef uint32_t VkResult;
typedef uint64_t VkDeviceAddress;
typedef uint64_t VkDeviceSize;
typedef void    *PFN_vkVoidFunction;

#define VK_SUCCESS           0
#define VK_NOT_READY         1
#define VK_TIMEOUT           2
#define VK_ERROR_DEVICE_LOST (-4)
#define VK_NULL_HANDLE       0ULL
#define VK_TRUE              1
#define VK_FALSE             0

#define UK_VK_HANDLE_BASE  0x0002000000000000ULL
#define UK_H_INSTANCE  (UK_VK_HANDLE_BASE + 0x01ULL)
#define UK_H_PHYSDEV   (UK_VK_HANDLE_BASE + 0x02ULL)
#define UK_H_DEVICE    (UK_VK_HANDLE_BASE + 0x03ULL)
#define UK_H_QUEUE     (UK_VK_HANDLE_BASE + 0x04ULL)

#include <string.h>

#define UK_DISPATCH_BUF_SIZE (2u * 1024u * 1024u)

#define OFF_MEM_ALLOC_SIZE        16u
#define OFF_MEM_ALLOC_TYPE        24u
#define OFF_BUF_SIZE              24u
#define OFF_BUF_USAGE             32u
#define OFF_SHADER_CODE_SIZE      24u
#define OFF_SHADER_PCODE          32u
#define OFF_DSL_BINDING_COUNT     20u
#define OFF_DSL_PBINDINGS         24u
#define OFF_DSLB_BINDING           0u
#define OFF_DSLB_TYPE              4u
#define OFF_DSLB_COUNT             8u
#define OFF_DSLB_STAGE            12u
#define SIZE_DSLB                 24u
#define OFF_DP_MAX_SETS           20u
#define OFF_DP_POOL_COUNT         24u
#define OFF_DP_POOL_SIZES         32u
#define SIZE_DP_POOL_SIZE          8u
#define OFF_DSA_POOL              16u
#define OFF_DSA_SET_COUNT         24u
#define OFF_DSA_LAYOUTS           32u
#define OFF_PL_SET_COUNT          20u
#define OFF_PL_SET_LAYOUTS        24u
#define OFF_PL_PUSH_COUNT         32u
#define OFF_PL_PUSH_RANGES        40u
#define OFF_PUSH_STAGE             0u
#define OFF_PUSH_OFFSET            4u
#define OFF_PUSH_SIZE              8u
#define OFF_CP_STAGE              24u
#define OFF_CP_LAYOUT             72u
#define SIZE_CP_INFO              96u
#define OFF_STAGE_MODULE          24u
#define OFF_STAGE_PNAME           32u
#define OFF_SUBMIT_CMD_COUNT      40u
#define OFF_SUBMIT_CMDS           48u
#define SIZE_SUBMIT_INFO          72u
#define OFF_WRITE_DST_SET         16u
#define OFF_WRITE_BINDING         24u
#define OFF_WRITE_DESC_COUNT      32u
#define OFF_WRITE_DESC_TYPE       36u
#define OFF_WRITE_BUFFER_INFO     48u
#define SIZE_WRITE_DESC           64u
#define SIZE_DESC_BUF_INFO        24u
#define OFF_BCOPY_SIZE            16u
#define SIZE_BCOPY                24u

extern struct uk_vulkan_state g_vk;
extern struct uk_mutex g_disp_lock;

extern uint8_t g_enc_buf[];
extern struct uk_venus_encoder g_batch_enc;

extern struct uk_venus_ring g_ring;

extern int g_ring_enabled;
extern int g_ring_ready;
extern int g_ring_recording;
extern int g_batch_enabled;
extern int g_batch_recording;
extern uint8_t g_batch_buf[];

extern uint8_t g_memprops[];
extern int g_memprops_valid;
extern int g_props_query_blocked_reported;
extern int g_memprops_query_blocked_reported;
extern int g_buffer_req_query_blocked_reported;
extern int g_buffer_req_tracked_reported;


#define UK_HV_MEM_MAX 128
struct uk_hv_mem {
    uint64_t handle;
    struct uk_virtio_gpu_blob blob;
    uint8_t  used;
};
extern struct uk_hv_mem g_hv_mem[UK_HV_MEM_MAX];

static inline int uk_mem_type_host_visible(uint32_t idx)
{
    if (g_memprops_valid && idx < 32u) {
        uint32_t flags = *(uint32_t *)(g_memprops + 4 + idx * 8);
        return (flags & 0x2u) != 0u;
    }
    return idx == 1u || idx == 2u;
}

struct uk_hv_mem *uk_hv_mem_find(uint64_t handle);
extern uk_gpu_fence_id g_last_fence;

int uk_dispatch_batch_active(void);
int uk_dispatch_ring_active(void);
void uk_disp_lock(void);

void uk_disp_unlock(void);
void uk_dispatch_destroy_dev_handle(void (*fn)(struct uk_venus_encoder *, uint64_t, uint64_t), uint64_t handle);
uint64_t uk_vk_alloc_handle(void);
void uk_vk_report_blocked_once(int *reported, const char *reason);
void uk_vk_report_diag_once(int *reported, const char *message);
void uk_dispatch_ring_lazy_init(void);

void uk_buf_size_put(uint64_t handle, uint64_t size);
uint64_t uk_buf_size_find(uint64_t handle);

static inline uint32_t rd_u32(const void *base, size_t off)
{
    uint32_t v = 0;
    if (base) memcpy(&v, (const uint8_t *)base + off, sizeof(v));
    return v;
}
static inline uint64_t rd_u64(const void *base, size_t off)
{
    uint64_t v = 0;
    if (base) memcpy(&v, (const uint8_t *)base + off, sizeof(v));
    return v;
}
static inline const void *rd_ptr(const void *base, size_t off)
{
    return (const void *)(uintptr_t)rd_u64(base, off);
}

#define UK_ENC_BEGIN() \
    uk_disp_lock(); \
    struct uk_venus_encoder _local_enc; \
    struct uk_venus_encoder *_enc_p = uk_dispatch_batch_active() ? &g_batch_enc : &_local_enc; \
    if (!uk_dispatch_batch_active()) \
        uk_venus_encoder_init(&_local_enc, g_enc_buf, 4096); \
    struct uk_venus_encoder _enc = *_enc_p; \
    (void)0

#define UK_ENC_SUBMIT() \
    do { \
        if (uk_dispatch_ring_active()) { \
            uk_venus_ring_cmd_write(&g_ring, _enc.buf, _enc.pos); \
        } else if (uk_dispatch_batch_active()) { \
            g_batch_enc = _enc; \
        } else { \
            uk_venus_submit(g_vk.gpu, g_vk.ctx, &_enc); \
        } \
        uk_disp_unlock(); \
    } while (0)
extern VkResult stub_vkCreateInstance(const void *, const void *, VkInstance *);
extern VkResult stub_vkDestroyInstance(VkInstance, const void *);
extern VkResult stub_vkEnumeratePhysicalDevices(VkInstance, uint32_t *, VkPhysicalDevice *);
extern void     stub_vkGetPhysicalDeviceProperties(VkPhysicalDevice, void *);
extern void     stub_vkGetPhysicalDeviceProperties2(VkPhysicalDevice, void *);
extern void     stub_vkGetPhysicalDeviceFeatures(VkPhysicalDevice, void *);
extern void     stub_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice, void *);
extern void     stub_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, void *);
extern void     stub_vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice, void *);
extern void     stub_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t *, void *);
extern VkResult stub_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice, const char *, uint32_t *, void *);
extern VkResult stub_vkEnumerateInstanceLayerProperties(uint32_t *, void *);
extern VkResult stub_vkEnumerateInstanceExtensionProperties(const char *, uint32_t *, void *);
extern VkResult stub_vkEnumerateInstanceVersion(uint32_t *);
extern VkResult stub_vkCreateDevice(VkPhysicalDevice, const void *, const void *, VkDevice *);
extern void     stub_vkDestroyDevice(VkDevice, const void *);
extern void     stub_vkGetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue *);
extern VkResult stub_vkAllocateMemory(VkDevice, const void *, const void *, VkDeviceMemory *);
extern void     stub_vkFreeMemory(VkDevice, VkDeviceMemory, const void *);
extern VkResult stub_vkMapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, uint32_t, void **);
extern void     stub_vkUnmapMemory(VkDevice, VkDeviceMemory);
extern VkResult stub_vkCreateBuffer(VkDevice, const void *, const void *, VkBuffer *);
extern void     stub_vkDestroyBuffer(VkDevice, VkBuffer, const void *);
extern void     stub_vkGetBufferMemoryRequirements(VkDevice, VkBuffer, void *);
extern void     stub_vkGetBufferMemoryRequirements2(VkDevice, const void *, void *);
extern VkResult stub_vkBindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
extern VkDeviceAddress stub_vkGetBufferDeviceAddress(VkDevice, const void *);
extern VkResult stub_vkCreateShaderModule(VkDevice, const void *, const void *, VkShaderModule *);
extern void     stub_vkDestroyShaderModule(VkDevice, VkShaderModule, const void *);
extern VkResult stub_vkCreateDescriptorSetLayout(VkDevice, const void *, const void *, VkDescriptorSetLayout *);
extern void     stub_vkDestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const void *);
extern VkResult stub_vkCreateDescriptorPool(VkDevice, const void *, const void *, VkDescriptorPool *);
extern void     stub_vkDestroyDescriptorPool(VkDevice, VkDescriptorPool, const void *);
extern VkResult stub_vkAllocateDescriptorSets(VkDevice, const void *, VkDescriptorSet *);
extern void     stub_vkUpdateDescriptorSets(VkDevice, uint32_t, const void *, uint32_t, const void *);
extern VkResult stub_vkCreatePipelineLayout(VkDevice, const void *, const void *, VkPipelineLayout *);
extern void     stub_vkDestroyPipelineLayout(VkDevice, VkPipelineLayout, const void *);
extern VkResult stub_vkCreateComputePipelines(VkDevice, uint64_t, uint32_t, const void *, const void *, VkPipeline *);
extern void     stub_vkDestroyPipeline(VkDevice, VkPipeline, const void *);
extern VkResult stub_vkCreateCommandPool(VkDevice, const void *, const void *, VkCommandPool *);
extern void     stub_vkDestroyCommandPool(VkDevice, VkCommandPool, uint32_t);
extern VkResult stub_vkAllocateCommandBuffers(VkDevice, const void *, VkCommandBuffer *);
extern void     stub_vkFreeCommandBuffers(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer *);
extern VkResult stub_vkBeginCommandBuffer(VkCommandBuffer, const void *);
extern VkResult stub_vkEndCommandBuffer(VkCommandBuffer);
extern VkResult stub_vkResetCommandBuffer(VkCommandBuffer, uint32_t);
extern VkResult stub_vkResetCommandPool(VkDevice, VkCommandPool, uint32_t);
extern VkResult stub_vkGetMemoryHostPointerPropertiesEXT(VkDevice, uint32_t, const void *, void *);
extern void     stub_vkCmdBindPipeline(VkCommandBuffer, uint32_t, VkPipeline);
extern void     stub_vkCmdBindDescriptorSets(VkCommandBuffer, uint32_t, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *);
extern void     stub_vkCmdPushConstants(VkCommandBuffer, VkPipelineLayout, uint32_t, uint32_t, uint32_t, const void *);
extern void     stub_vkCmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
extern void     stub_vkCmdCopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const void *);
extern void     stub_vkCmdFillBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
extern void     stub_vkCmdPipelineBarrier(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, const void *, uint32_t, const void *, uint32_t, const void *);
extern VkResult stub_vkQueueSubmit(VkQueue, uint32_t, const void *, VkFence);
extern VkResult stub_vkQueueWaitIdle(VkQueue);
extern VkResult stub_vkDeviceWaitIdle(VkDevice);
extern VkResult stub_vkCreateFence(VkDevice, const void *, const void *, VkFence *);
extern void     stub_vkDestroyFence(VkDevice, VkFence, const void *);
extern VkResult stub_vkResetFences(VkDevice, uint32_t, const VkFence *);
extern VkResult stub_vkWaitForFences(VkDevice, uint32_t, const VkFence *, uint32_t, uint64_t);
extern VkResult stub_vkGetFenceStatus(VkDevice, VkFence);
extern VkResult stub_vkCreateEvent(VkDevice, const void *, const void *, VkEvent *);
extern void     stub_vkDestroyEvent(VkDevice, VkEvent, const void *);
extern VkResult stub_vkGetEventStatus(VkDevice, VkEvent);
extern VkResult stub_vkSetEvent(VkDevice, VkEvent);
extern VkResult stub_vkResetEvent(VkDevice, VkEvent);
extern VkResult stub_vkCreateSemaphore(VkDevice, const void *, const void *, VkSemaphore *);
extern void     stub_vkDestroySemaphore(VkDevice, VkSemaphore, const void *);
extern VkResult stub_vkWaitSemaphores(VkDevice, const void *, uint64_t);
extern VkResult stub_vkSignalSemaphore(VkDevice, const void *);
extern VkResult stub_vkGetSemaphoreCounterValue(VkDevice, VkSemaphore, uint64_t *);
extern void     stub_vkCmdSetEvent(VkCommandBuffer, VkEvent, uint32_t);
extern void     stub_vkCmdResetEvent(VkCommandBuffer, VkEvent, uint32_t);
extern void     stub_vkCmdWaitEvents(VkCommandBuffer, uint32_t, const void *, uint32_t, uint32_t, uint32_t, const void *, uint32_t, const void *, uint32_t, const void *);
extern void     stub_vkCmdPipelineBarrier2(VkCommandBuffer, const void *);
extern void     stub_vkCmdCopyBuffer2(VkCommandBuffer, const void *);
extern VkResult stub_vkQueueSubmit2(VkQueue, uint32_t, const void *, VkFence);
extern VkResult stub_vkFlushMappedMemoryRanges(VkDevice, uint32_t, const void *);
extern VkResult stub_vkInvalidateMappedMemoryRanges(VkDevice, uint32_t, const void *);
extern VkResult stub_vkCreateQueryPool(VkDevice, const void *, const void *, VkQueryPool *);
extern void     stub_vkDestroyQueryPool(VkDevice, VkQueryPool, const void *);
extern VkResult stub_vkGetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void *, VkDeviceSize, uint32_t);
extern void     stub_vkCmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
extern void     stub_vkCmdWriteTimestamp(VkCommandBuffer, uint32_t, VkQueryPool, uint32_t);
extern void     stub_vkCmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
extern void     stub_vkCmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t);
extern void     stub_vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice, void *);
extern void     stub_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice, void *);
extern void     stub_vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice, void *);
extern void     stub_vkGetBufferMemoryRequirements2KHR(VkDevice, const void *, void *);
extern VkDeviceAddress stub_vkGetBufferDeviceAddressKHR(VkDevice, const void *);
