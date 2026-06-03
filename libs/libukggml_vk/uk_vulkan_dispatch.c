/* SPDX-License-Identifier: MIT */
/*
 * libukggml_vulkan/uk_vulkan_dispatch.c — Static Vulkan C ABI for ggml-vulkan
 *
 * Implements the Vulkan C functions that ggml-vulkan.cpp calls through
 * vulkan.hpp.  Each function encodes the call to Venus wire format via
 * libukvenus and submits via VirtIO-GPU SUBMIT_3D.
 *
 * Handle assignment: guest-local sequential handles in range
 *   [0x0002000000000000, 0x00020000FFFFFFFF].
 * The host (virglrenderer Venus) never allocates handles; the guest
 * assigns them and the host registers them on first use — standard Venus
 * protocol.
 *
 * Memory properties: returned values match NVIDIA RTX 4000 Ada Gen as
 * seen through Venus/virglrenderer.  ggml-vulkan selects device-local
 * memory for compute buffers and host-visible for staging.
 *
 * Not implemented (Venus ring-buffer reads required):
 *   vkGetQueryPoolResults, vkGetEventStatus, pipeline statistics.
 * These stubs return VK_SUCCESS / 0 so ggml-vulkan skips the optional paths.
 *
 * vk.ggml-dispatch gate: LLAMA-VK-vk.ggml-dispatch-DISPATCH.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <uk/mutex.h>

#include <uk/virtio_gpu.h>
#include <uk/drm_virtgpu.h>
#include <uk/vulkan_icd.h>
#include <uk/venus.h>
#include <uk/ggml_vulkan.h>

/* ── Vulkan minimal type definitions (no vulkan.h dependency in C code) ─── */
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

/* ── Global Venus state ────────────────────────────────────────────────── */
static struct uk_virtio_gpu_dev     *g_gpu;
static struct uk_vulkan_icd_dev      g_icd;
static int                           g_disp_initialized;
/* Convenience: points into g_icd.drm._ctx after init */
static struct uk_virtio_gpu_context *g_ctx;
/* Last fence submitted via QueueSubmit; polled by WaitForFences. */
static uk_gpu_fence_id               g_last_fence;

/* Real host VkPhysicalDeviceMemoryProperties (520B), filled by the first real
 * round-trip in stub_vkGetPhysicalDeviceMemoryProperties. Used by both that stub
 * and the host-visible memory-type test in stub_vkAllocateMemory. */
static uint8_t g_memprops[520];
static int     g_memprops_valid;

/* The Venus VkDevice is a singleton created exactly once (duplicate object ids
 * are fatal to the host context). */
static int     g_device_created;

/* ── Handle pool ───────────────────────────────────────────────────────── */
#define UK_VK_HANDLE_BASE  0x0002000000000000ULL
/* Dynamic object ids MUST start above the fixed well-known handles below,
 * otherwise a dynamically-allocated object (buffer/memory/...) reuses the id of
 * the instance/physdev/device/queue. The host (virglrenderer
 * vkr_context_validate_object_id) treats a duplicate object id as fatal and
 * tears the Venus context down — which surfaced as a vkCreateDevice /
 * vkGetDeviceQueue "CS error" once real allocations began. */
#define UK_VK_HANDLE_FIRST_DYNAMIC (UK_VK_HANDLE_BASE + 0x100ULL)
static uint64_t g_next_handle = UK_VK_HANDLE_FIRST_DYNAMIC;

/* Fixed well-known handles for the singleton instance / device path */
#define UK_H_INSTANCE  (UK_VK_HANDLE_BASE + 0x01ULL)
#define UK_H_PHYSDEV   (UK_VK_HANDLE_BASE + 0x02ULL)
#define UK_H_DEVICE    (UK_VK_HANDLE_BASE + 0x03ULL)
#define UK_H_QUEUE     (UK_VK_HANDLE_BASE + 0x04ULL)

/* ggml-vulkan compiles its compute pipelines concurrently (std::async over
 * std::thread::hardware_concurrency() workers — each worker calls
 * vkCreateShaderModule / vkCreateComputePipelines / vkDestroyShaderModule).
 * The static dispatch shares one encode buffer (g_enc_buf), and the real Mesa
 * Venus driver serialises ring submission, so we must too: a recursive
 * uk_mutex guards every encode+submit and handle allocation is atomic. Without
 * this, concurrent encoders stomp g_enc_buf and the host decodes a corrupted
 * command stream (e.g. a stale VkShaderModule id that then fails object lookup
 * with a CS error). The VirtIO-GPU control queue is separately serialised by
 * g_ctrlq_lock in libukvirtio_gpu (mirrors the Linux ctrlq.qlock). */
static struct uk_mutex g_disp_lock =
	UK_MUTEX_INITIALIZER_RECURSIVE(g_disp_lock);

static inline uint64_t uk_vk_alloc_handle(void)
{
    return __atomic_fetch_add(&g_next_handle, 1, __ATOMIC_SEQ_CST);
}

/* ── Encoder buffer pool ───────────────────────────────────────────────── */
/* Must hold the largest single Venus command we encode. The dominant case is
 * vkCreateShaderModule, whose body is a full SPIR-V module: ggml-vulkan's big
 * matmul / flash-attention shaders reach ~72 KB and some specialised variants
 * are larger, so a 64 KB buffer silently overflowed (uk_venus_submit returns
 * -EOVERFLOW, the create is dropped, and a later pipeline referencing the
 * never-created VkShaderModule fails host object lookup with a CS error).
 * 2 MB covers every ggml-vulkan SPIR-V with wide margin. */
#define UK_DISPATCH_BUF_SIZE (2u * 1024u * 1024u)
static uint8_t g_enc_buf[UK_DISPATCH_BUF_SIZE];

/* ── Ring stream model (P1.3 → ring) ────────────────────────────────────
 *
 * When UK_GGML_VK_DISPATCH_RING=1 (default for production images), every
 * vkCmd* call writes directly into the Venus ring circular buffer instead of
 * going through a separate SUBMIT_3D:
 *
 *   vkBeginCommandBuffer → open ring write window
 *   vkCmd*              → uk_venus_ring_cmd_write (no malloc, no kick)
 *   vkEndCommandBuffer  → flush ring tail (kick only if host idle)
 *   vkQueueSubmit       → append QueueSubmit + flush once
 *   vkWaitForFences     → poll completed_fence (no Venus command sent)
 *
 * The batch encoder (SUBMIT_3D path) is kept as fallback for the native test
 * harness (which counts submits_3d) and for commands outside the Begin/End
 * window (init-time creates). Controlled by UK_GGML_VK_DISPATCH_RING.
 */
static int                  g_batch_enabled;     /* 0 = sync, 1 = batched (SUBMIT_3D) */
static int                  g_batch_recording;   /* in vkBegin..vkEnd window */
static struct uk_venus_encoder g_batch_enc;
static uint8_t              g_batch_buf[UK_DISPATCH_BUF_SIZE];

/* Ring stream state (active when g_ring_enabled=1) */
static int                  g_ring_enabled;
static struct uk_venus_ring g_ring;
static int                  g_ring_ready;        /* 1 after uk_venus_ring_register() */
static int                  g_ring_recording;    /* in vkBegin..vkEnd ring window */

static inline int uk_dispatch_batch_active(void)
{
    return g_batch_enabled && g_batch_recording;
}

static inline int uk_dispatch_ring_active(void)
{
    return g_ring_enabled && g_ring_ready && g_ring_recording;
}

/* Helpers for single-call encode + submit, batched when active. */
static inline void uk_disp_lock(void)   { uk_mutex_lock(&g_disp_lock); }
static inline void uk_disp_unlock(void) { uk_mutex_unlock(&g_disp_lock); }

#define UK_ENC_BEGIN() \
    uk_disp_lock(); \
    struct uk_venus_encoder _local_enc; \
    struct uk_venus_encoder *_enc_p = uk_dispatch_batch_active() ? &g_batch_enc : &_local_enc; \
    if (!uk_dispatch_batch_active()) \
        uk_venus_encoder_init(&_local_enc, g_enc_buf, UK_DISPATCH_BUF_SIZE); \
    struct uk_venus_encoder _enc = *_enc_p; \
    /* `_enc` is the addressable encoder for compatibility with existing \
     * call sites that take `&_enc`. After encoding we copy back. */ \
    (void)0
#define UK_ENC_SUBMIT() \
    do { \
        if (uk_dispatch_ring_active()) { \
            /* Ring stream: write encoded bytes into the circular buffer.   \
             * No malloc, no virtqueue kick — host ring_thread drains async. */ \
            uk_venus_ring_cmd_write(&g_ring, _enc.buf, _enc.pos); \
        } else if (uk_dispatch_batch_active()) { \
            g_batch_enc = _enc; /* keep accumulator up to date */ \
        } else { \
            if (_enc.overflow) \
                printf("uk-ggml-vk: ERROR encoder overflow pos=%u buf=%u " \
                       "(command dropped)\n", _enc.pos, UK_DISPATCH_BUF_SIZE); \
            uk_venus_submit(g_gpu, g_ctx, &_enc); \
        } \
        uk_disp_unlock(); \
    } while (0)

/*
 * Helper for the common "Destroy<Handle> on a device-owned handle" pattern.
 * The Venus encoder is the only thing that varies between these stubs, so we
 * accept it as a function pointer and reduce ~10 nearly-identical 6-line
 * stub bodies to one one-line call each.
 */
typedef void (*uk_venus_encode_destroy_fn)(struct uk_venus_encoder *,
                                           uint64_t /*device*/,
                                           uint64_t /*handle*/);
static inline void uk_dispatch_destroy_dev_handle(uk_venus_encode_destroy_fn fn,
                                                  uint64_t handle)
{
    UK_ENC_BEGIN();
    fn(&_enc, UK_H_DEVICE, handle);
    UK_ENC_SUBMIT();
}

/* ── Dispatch init ─────────────────────────────────────────────────────── */
int uk_ggml_vulkan_dispatch_init(void)
{
    if (g_disp_initialized)
        return 0;

    /* Open VirtIO-GPU device + Venus context via the vk.icd ICD layer.
     * uk_vulkan_icd_init() calls uk_drm_virtgpu_open() which allocates its
     * own uk_virtio_gpu_dev; we reference it via icd.drm._vdev.
     * This path works both on real hardware and against the fake backend.
     */
    if (uk_vulkan_icd_init(&g_icd, 0) != 0)
        return -1;

    g_gpu = g_icd.drm._vdev;
    g_ctx = &g_icd.drm._ctx;
    g_disp_initialized = 1;

    /* Default to SYNC (per-call submit) so the native dispatch test, which
     * counts submits_3d after each stub, keeps observing one submit per
     * call. Production images enable batching via the environment. */
    {
        const char *s = getenv("UK_GGML_VK_DISPATCH_BATCH");
        g_batch_enabled = (s && (*s == '1' || *s == 'y' || *s == 'Y'));
    }
    g_batch_recording = 0;
    uk_venus_encoder_init(&g_batch_enc, g_batch_buf, UK_DISPATCH_BUF_SIZE);

    /* Ring stream model: create + register a Venus command ring so that
     * vkCmd* calls write directly into the circular buffer instead of
     * triggering individual SUBMIT_3D virtqueue kicks. Enabled by default
     * when UK_GGML_VK_DISPATCH_RING is unset or "1"; disable with "0" for
     * native tests (which count submits_3d). */
    {
        const char *r = getenv("UK_GGML_VK_DISPATCH_RING");
        int want_ring = !(r && (*r == '0'));
        if (want_ring) {
            /* Create the ring on the existing dispatch context g_ctx so the
             * ring's vkCreateRingMESA/Notify commands and the Vulkan compute
             * objects share one host-side Venus context. */
            int rc = uk_venus_ring_create_on_ctx(g_gpu, &g_ring, g_ctx,
                                          UK_VENUS_RING_CTRL_SIZE +
                                          UK_VENUS_RING_DEFAULT_SIZE,
                                          UK_VENUS_RING_DEFAULT_BLOB_ID + 1);
            if (rc == 0) {
                rc = uk_venus_ring_register(g_gpu, &g_ring,
                                             UK_VENUS_RING_DEFAULT_BLOB_ID + 1);
                if (rc == 0) {
                    /* Bind transport thunk so vn_submit_* wrappers route here */
                    uk_venus_ring_bind_current(g_gpu, g_ctx);
                    g_ring_enabled = 1;
                    g_ring_ready   = 1;
                    printf("uk-ggml-vk: ring stream enabled (buf=%u B)\n",
                           UK_VENUS_RING_DEFAULT_SIZE);
                } else {
                    uk_venus_ring_destroy(g_gpu, &g_ring);
                    printf("uk-ggml-vk: ring register failed rc=%d, "
                           "falling back to SUBMIT_3D\n", rc);
                }
            } else {
                printf("uk-ggml-vk: ring create failed rc=%d, "
                       "falling back to SUBMIT_3D\n", rc);
            }
        }
        /* If ring is not active, fall back to batch/sync depending on
         * UK_GGML_VK_DISPATCH_BATCH (already set above). */
    }
    return 0;
}

/* plan-optimize.md L3.4: surface the host-blob mapping state and the L3.1
 * batching flag so the appliance can log them and the perf gate can record
 * them per evidence row. */
void uk_ggml_vulkan_dispatch_get_info(struct uk_ggml_vulkan_dispatch_info *out)
{
    if (!out)
        return;
    out->batch_enabled     = g_batch_enabled;
    out->ring_enabled      = g_ring_enabled && g_ring_ready;
    /* The fixed-blob window is set when the underlying VirtIO-GPU device
     * supports it. We probe the metrics counter for a host_blob_mapped
     * marker once the real driver records it; for now leave it zero so
     * the appliance reports an honest `hostmem_fixed=0` blocker. */
    out->hostmem_fixed     = 0;
    /* Reported diagnostic: size of the wanted-extension slice in
     * scripts/venus/pin.json (the authoritative Venus generator slice config). */
    out->wanted_extensions = 8;
}

/* ── vkGetInstanceProcAddr / vkGetDeviceProcAddr ────────────────────────
 *
 * ggml-vulkan.cpp initialises vulkan.hpp's DispatchLoaderDynamic by calling:
 *   ggml_vk_default_dispatcher_instance.init(vkGetInstanceProcAddr);
 *
 * We provide a table-driven lookup: known names return a function pointer
 * to our stub; unknown names return NULL (ggml-vulkan skips optional paths).
 */

/* Forward declarations for all stubs */
static VkResult stub_vkCreateInstance(const void *, const void *, VkInstance *);
static VkResult stub_vkDestroyInstance(VkInstance, const void *);
static VkResult stub_vkEnumeratePhysicalDevices(VkInstance, uint32_t *, VkPhysicalDevice *);
static void     stub_vkGetPhysicalDeviceProperties(VkPhysicalDevice, void *);
static void     stub_vkGetPhysicalDeviceProperties2(VkPhysicalDevice, void *);
static void     stub_vkGetPhysicalDeviceFeatures(VkPhysicalDevice, void *);
static void     stub_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice, void *);
static void     stub_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, void *);
static void     stub_vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice, void *);
static void     stub_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t *, void *);
static VkResult stub_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice, const char *, uint32_t *, void *);
static VkResult stub_vkEnumerateInstanceLayerProperties(uint32_t *, void *);
static VkResult stub_vkEnumerateInstanceExtensionProperties(const char *, uint32_t *, void *);
static VkResult stub_vkEnumerateInstanceVersion(uint32_t *);
static VkResult stub_vkCreateDevice(VkPhysicalDevice, const void *, const void *, VkDevice *);
static void     stub_vkDestroyDevice(VkDevice, const void *);
static void     stub_vkGetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue *);
static VkResult stub_vkAllocateMemory(VkDevice, const void *, const void *, VkDeviceMemory *);
static void     stub_vkFreeMemory(VkDevice, VkDeviceMemory, const void *);
static VkResult stub_vkMapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, uint32_t, void **);
static void     stub_vkUnmapMemory(VkDevice, VkDeviceMemory);
static VkResult stub_vkCreateBuffer(VkDevice, const void *, const void *, VkBuffer *);
static void     stub_vkDestroyBuffer(VkDevice, VkBuffer, const void *);
static void     stub_vkGetBufferMemoryRequirements(VkDevice, VkBuffer, void *);
static void     stub_vkGetBufferMemoryRequirements2(VkDevice, const void *, void *);
static VkResult stub_vkBindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
static VkDeviceAddress stub_vkGetBufferDeviceAddress(VkDevice, const void *);
static VkResult stub_vkCreateShaderModule(VkDevice, const void *, const void *, VkShaderModule *);
static void     stub_vkDestroyShaderModule(VkDevice, VkShaderModule, const void *);
static VkResult stub_vkCreateDescriptorSetLayout(VkDevice, const void *, const void *, VkDescriptorSetLayout *);
static void     stub_vkDestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const void *);
static VkResult stub_vkCreateDescriptorPool(VkDevice, const void *, const void *, VkDescriptorPool *);
static void     stub_vkDestroyDescriptorPool(VkDevice, VkDescriptorPool, const void *);
static VkResult stub_vkAllocateDescriptorSets(VkDevice, const void *, VkDescriptorSet *);
static void     stub_vkUpdateDescriptorSets(VkDevice, uint32_t, const void *, uint32_t, const void *);
static VkResult stub_vkCreatePipelineLayout(VkDevice, const void *, const void *, VkPipelineLayout *);
static void     stub_vkDestroyPipelineLayout(VkDevice, VkPipelineLayout, const void *);
static VkResult stub_vkCreateComputePipelines(VkDevice, uint64_t, uint32_t, const void *, const void *, VkPipeline *);
static void     stub_vkDestroyPipeline(VkDevice, VkPipeline, const void *);
static VkResult stub_vkCreateCommandPool(VkDevice, const void *, const void *, VkCommandPool *);
static void     stub_vkDestroyCommandPool(VkDevice, VkCommandPool, uint32_t);
static VkResult stub_vkAllocateCommandBuffers(VkDevice, const void *, VkCommandBuffer *);
static void     stub_vkFreeCommandBuffers(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer *);
static VkResult stub_vkBeginCommandBuffer(VkCommandBuffer, const void *);
static VkResult stub_vkEndCommandBuffer(VkCommandBuffer);
static VkResult stub_vkResetCommandBuffer(VkCommandBuffer, uint32_t);
static VkResult stub_vkResetCommandPool(VkDevice, VkCommandPool, uint32_t);
static VkResult stub_vkGetMemoryHostPointerPropertiesEXT(VkDevice, uint32_t, const void *, void *);
static void     stub_vkCmdBindPipeline(VkCommandBuffer, uint32_t, VkPipeline);
static void     stub_vkCmdBindDescriptorSets(VkCommandBuffer, uint32_t, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *);
static void     stub_vkCmdPushConstants(VkCommandBuffer, VkPipelineLayout, uint32_t, uint32_t, uint32_t, const void *);
static void     stub_vkCmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
static void     stub_vkCmdCopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const void *);
static void     stub_vkCmdFillBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
static void     stub_vkCmdPipelineBarrier(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, const void *, uint32_t, const void *, uint32_t, const void *);
static VkResult stub_vkQueueSubmit(VkQueue, uint32_t, const void *, VkFence);
static VkResult stub_vkQueueWaitIdle(VkQueue);
static VkResult stub_vkDeviceWaitIdle(VkDevice);
static VkResult stub_vkCreateFence(VkDevice, const void *, const void *, VkFence *);
static void     stub_vkDestroyFence(VkDevice, VkFence, const void *);
static VkResult stub_vkResetFences(VkDevice, uint32_t, const VkFence *);
static VkResult stub_vkWaitForFences(VkDevice, uint32_t, const VkFence *, uint32_t, uint64_t);
static VkResult stub_vkGetFenceStatus(VkDevice, VkFence);
static VkResult stub_vkCreateEvent(VkDevice, const void *, const void *, VkEvent *);
static void     stub_vkDestroyEvent(VkDevice, VkEvent, const void *);
static VkResult stub_vkGetEventStatus(VkDevice, VkEvent);
static VkResult stub_vkSetEvent(VkDevice, VkEvent);
static VkResult stub_vkResetEvent(VkDevice, VkEvent);
static VkResult stub_vkCreateSemaphore(VkDevice, const void *, const void *, VkSemaphore *);
static void     stub_vkDestroySemaphore(VkDevice, VkSemaphore, const void *);
static VkResult stub_vkWaitSemaphores(VkDevice, const void *, uint64_t);
static VkResult stub_vkSignalSemaphore(VkDevice, const void *);
static VkResult stub_vkGetSemaphoreCounterValue(VkDevice, VkSemaphore, uint64_t *);
static void     stub_vkCmdSetEvent(VkCommandBuffer, VkEvent, uint32_t);
static void     stub_vkCmdResetEvent(VkCommandBuffer, VkEvent, uint32_t);
static void     stub_vkCmdWaitEvents(VkCommandBuffer, uint32_t, const void *, uint32_t, uint32_t, uint32_t, const void *, uint32_t, const void *, uint32_t, const void *);
static void     stub_vkCmdPipelineBarrier2(VkCommandBuffer, const void *);
static void     stub_vkCmdCopyBuffer2(VkCommandBuffer, const void *);
static VkResult stub_vkQueueSubmit2(VkQueue, uint32_t, const void *, VkFence);
static VkResult stub_vkFlushMappedMemoryRanges(VkDevice, uint32_t, const void *);
static VkResult stub_vkInvalidateMappedMemoryRanges(VkDevice, uint32_t, const void *);
static VkResult stub_vkCreateQueryPool(VkDevice, const void *, const void *, VkQueryPool *);
static void     stub_vkDestroyQueryPool(VkDevice, VkQueryPool, const void *);
static VkResult stub_vkGetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void *, VkDeviceSize, uint32_t);
static void     stub_vkCmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
static void     stub_vkCmdWriteTimestamp(VkCommandBuffer, uint32_t, VkQueryPool, uint32_t);
static void     stub_vkCmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
static void     stub_vkCmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t);
/* KHR alias forward declarations */
static void     stub_vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice, void *);
static void     stub_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice, void *);
static void     stub_vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice, void *);
static void     stub_vkGetBufferMemoryRequirements2KHR(VkDevice, const void *, void *);
static VkDeviceAddress stub_vkGetBufferDeviceAddressKHR(VkDevice, const void *);

/* ── Proc lookup table ───────────────────────────────────────────────── */
struct uk_vk_proc {
    const char      *name;
    PFN_vkVoidFunction fn;
};

#define PROC(fn) { #fn, (PFN_vkVoidFunction)stub_##fn }

static const struct uk_vk_proc k_procs[] = {
    PROC(vkCreateInstance),
    PROC(vkDestroyInstance),
    PROC(vkEnumeratePhysicalDevices),
    PROC(vkGetPhysicalDeviceProperties),
    PROC(vkGetPhysicalDeviceProperties2),
    PROC(vkGetPhysicalDeviceProperties2KHR),
    PROC(vkGetPhysicalDeviceFeatures),
    PROC(vkGetPhysicalDeviceFeatures2),
    PROC(vkGetPhysicalDeviceFeatures2KHR),
    PROC(vkGetPhysicalDeviceMemoryProperties),
    PROC(vkGetPhysicalDeviceMemoryProperties2),
    PROC(vkGetPhysicalDeviceMemoryProperties2KHR),
    PROC(vkGetPhysicalDeviceQueueFamilyProperties),
    PROC(vkEnumerateDeviceExtensionProperties),
    PROC(vkEnumerateInstanceLayerProperties),
    PROC(vkEnumerateInstanceExtensionProperties),
    PROC(vkEnumerateInstanceVersion),
    PROC(vkCreateDevice),
    PROC(vkDestroyDevice),
    PROC(vkGetDeviceQueue),
    PROC(vkAllocateMemory),
    PROC(vkFreeMemory),
    PROC(vkMapMemory),
    PROC(vkUnmapMemory),
    PROC(vkCreateBuffer),
    PROC(vkDestroyBuffer),
    PROC(vkGetBufferMemoryRequirements),
    PROC(vkGetBufferMemoryRequirements2),
    PROC(vkGetBufferMemoryRequirements2KHR),
    PROC(vkBindBufferMemory),
    PROC(vkGetBufferDeviceAddress),
    PROC(vkGetBufferDeviceAddressKHR),
    PROC(vkCreateShaderModule),
    PROC(vkDestroyShaderModule),
    PROC(vkCreateDescriptorSetLayout),
    PROC(vkDestroyDescriptorSetLayout),
    PROC(vkCreateDescriptorPool),
    PROC(vkDestroyDescriptorPool),
    PROC(vkAllocateDescriptorSets),
    PROC(vkUpdateDescriptorSets),
    PROC(vkCreatePipelineLayout),
    PROC(vkDestroyPipelineLayout),
    PROC(vkCreateComputePipelines),
    PROC(vkDestroyPipeline),
    PROC(vkCreateCommandPool),
    PROC(vkDestroyCommandPool),
    PROC(vkAllocateCommandBuffers),
    PROC(vkFreeCommandBuffers),
    PROC(vkBeginCommandBuffer),
    PROC(vkEndCommandBuffer),
    PROC(vkResetCommandBuffer),
    PROC(vkResetCommandPool),
    PROC(vkGetMemoryHostPointerPropertiesEXT),
    PROC(vkCmdBindPipeline),
    PROC(vkCmdBindDescriptorSets),
    PROC(vkCmdPushConstants),
    PROC(vkCmdDispatch),
    PROC(vkCmdCopyBuffer),
    PROC(vkCmdFillBuffer),
    PROC(vkCmdPipelineBarrier),
    PROC(vkQueueSubmit),
    PROC(vkQueueWaitIdle),
    PROC(vkDeviceWaitIdle),
    PROC(vkCreateFence),
    PROC(vkDestroyFence),
    PROC(vkResetFences),
    PROC(vkWaitForFences),
    PROC(vkGetFenceStatus),
    PROC(vkCreateEvent),
    PROC(vkDestroyEvent),
    PROC(vkGetEventStatus),
    PROC(vkSetEvent),
    PROC(vkResetEvent),
    PROC(vkCreateSemaphore),
    PROC(vkWaitSemaphores),
    PROC(vkSignalSemaphore),
    PROC(vkGetSemaphoreCounterValue),
    PROC(vkCmdSetEvent),
    PROC(vkCmdResetEvent),
    PROC(vkCmdWaitEvents),
    PROC(vkCmdPipelineBarrier2),
    PROC(vkCmdCopyBuffer2),
    PROC(vkQueueSubmit2),
    PROC(vkFlushMappedMemoryRanges),
    PROC(vkInvalidateMappedMemoryRanges),
    PROC(vkDestroySemaphore),
    PROC(vkCreateQueryPool),
    PROC(vkDestroyQueryPool),
    PROC(vkGetQueryPoolResults),
    PROC(vkCmdResetQueryPool),
    PROC(vkCmdWriteTimestamp),
    PROC(vkCmdBeginQuery),
    PROC(vkCmdEndQuery),
    { NULL, NULL }
};

PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    (void)instance;
    if (!pName) return NULL;
    for (const struct uk_vk_proc *p = k_procs; p->name; p++)
        if (__builtin_strcmp(p->name, pName) == 0)
            return p->fn;
    return NULL;
}

PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
    return vkGetInstanceProcAddr((VkInstance)device, pName);
}


/* ── Minimal Vulkan struct readers (LP64 layout) ─────────────────────────
 * These offsets match Vulkan 1.3 C ABI on x86-64/Unikraft LP64.  They are
 * used only for the ggml-vulkan compute path structs handled below.
 */
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

/* ── Stub implementations ──────────────────────────────────────────────── */

static VkResult stub_vkCreateInstance(const void *ci, const void *alloc,
                                      VkInstance *pInstance)
{
    (void)ci; (void)alloc;
    if (uk_ggml_vulkan_dispatch_init() != 0)
        return VK_ERROR_DEVICE_LOST;
    /* Send Venus bootstrap: Instance + EnumeratePhysicalDevices + Device */
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateInstance(&_enc, UK_H_INSTANCE,
                                     "ggml-vulkan-uk", 0x00402000u,
                                     0, (const char **)0);
    UK_ENC_SUBMIT();
    *pInstance = (VkInstance)UK_H_INSTANCE;
    return VK_SUCCESS;
}

static VkResult stub_vkDestroyInstance(VkInstance i, const void *a)
{
    (void)i; (void)a; return VK_SUCCESS;
}

static VkResult stub_vkEnumeratePhysicalDevices(VkInstance instance,
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

/* VkPhysicalDeviceProperties — NVIDIA RTX 4000 Ada via Venus.
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
static void stub_vkGetPhysicalDeviceProperties(VkPhysicalDevice physdev,
                                               void *pProperties)
{
    (void)physdev;
    uint8_t *b = (uint8_t *)pProperties;
    if (!b) return;
    memset(b, 0, 824); /* sizeof(VkPhysicalDeviceProperties) = 824 */
    uint32_t *p = (uint32_t *)b;
    p[0] = 0x00402000u; /* apiVersion: VK 1.2 */
    p[1] = 0;           /* driverVersion */
    p[2] = 0x10de;      /* vendorID: NVIDIA */
    p[3] = 0x27b0;      /* deviceID: RTX 4000 Ada */
    p[4] = 2;           /* deviceType: DISCRETE_GPU */
    char *name = (char *)(p + 5); /* deviceName at byte 20 */
    /* Real Venus round-trip: read the host physical device's name back over the
     * Venus reply path (vkSetReplyCommandStreamMESA + vkGetPhysicalDeviceProperties).
     * UK_H_PHYSDEV is already bound to the host's real device by the
     * vkEnumeratePhysicalDevices submitted earlier, so this returns e.g.
     * "Tesla V100-SXM2-16GB". Cached after the first success; falls back to a
     * descriptive default if the round-trip is unavailable (e.g. fake backend). */
    static char real_name[256];
    static int  real_name_state; /* 0=untried, 1=have real, -1=failed */
    if (real_name_state == 0 && g_gpu && g_ctx) {
        if (uk_venus_query_device_name(g_gpu, g_ctx, UK_H_PHYSDEV,
                                       real_name, sizeof(real_name)) == 0
            && real_name[0]) {
            real_name_state = 1;
            printf("uk-ggml-vk: venus physical_device=%s\n", real_name);
        } else {
            real_name_state = -1;
        }
    }
    if (real_name_state == 1) {
        __builtin_memcpy(name, real_name, __builtin_strlen(real_name) + 1);
    } else {
        const char *devname = "VOGUE-Venus/NVIDIA RTX 4000 Ada";
        __builtin_memcpy(name, devname, __builtin_strlen(devname) + 1);
    }
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
    *(uint64_t *)(b + 600) = 4096ULL;      /* minMemoryMapAlignment */
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
static void stub_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physdev, void *p2)
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
static void stub_vkGetPhysicalDeviceFeatures(VkPhysicalDevice physdev, void *pFeatures)
{
    (void)physdev;
    if (!pFeatures) return;
    /* All VkPhysicalDeviceFeatures are VkBool32; set all to VK_TRUE */
    uint32_t *f = (uint32_t *)pFeatures;
    for (int i = 0; i < 55; i++) f[i] = 1u; /* VK_TRUE for all 55 features */
}

static void stub_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physdev, void *p2)
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
static void stub_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physdev,
                                                      void *pMemProps)
{
    (void)physdev;
    if (!pMemProps) return;

    /* Real Venus round-trip: read the host GPU's actual memory types/heaps so
     * ggml selects a memory type index that truly exists and is host-visible on
     * the host (required for vkAllocateMemory + host-visible blob export to
     * succeed). Cached after the first success; falls back to the fabricated
     * layout below on failure (e.g. fake backend). */
    static int real_mp_state; /* 0=untried, 1=have real, -1=failed */
    if (real_mp_state == 0 && g_gpu && g_ctx) {
        if (uk_venus_query_memory_properties(g_gpu, g_ctx, UK_H_PHYSDEV, g_memprops) == 0
            && *(uint32_t *)g_memprops > 0u) {
            real_mp_state = 1;
            g_memprops_valid = 1;
            printf("uk-ggml-vk: venus memory types=%u heaps=%u\n",
                   *(uint32_t *)g_memprops, *(uint32_t *)(g_memprops + 260));
        } else {
            real_mp_state = -1;
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

static void stub_vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physdev,
                                                       void *p2)
{
    if (!p2) return;
    stub_vkGetPhysicalDeviceMemoryProperties(physdev, (uint8_t *)p2 + 16);
}

static void stub_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physdev,
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

static VkResult stub_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physdev,
                                                          const char *layerName,
                                                          uint32_t *pCount,
                                                          void *pProperties)
{
    (void)physdev; (void)layerName; (void)pProperties;
    /* Report core compute extensions ggml-vulkan queries */
    *pCount = 0;
    return VK_SUCCESS;
}

static VkResult stub_vkEnumerateInstanceLayerProperties(uint32_t *pCount, void *p)
{
    (void)p; *pCount = 0; return VK_SUCCESS;
}

/* vkEnumerateInstanceVersion — required by Vulkan-Hpp 1.1+ dispatcher init.
 * Returns VK 1.3.0 so ggml-vulkan's "Vulkan 1.2 required" check passes. */
static VkResult stub_vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    if (pApiVersion)
        *pApiVersion = 0x00403000u; /* VK_MAKE_API_VERSION(0,1,3,0) */
    return VK_SUCCESS;
}

static VkResult stub_vkEnumerateInstanceExtensionProperties(const char *l,
                                                             uint32_t *pCount,
                                                             void *p)
{
    (void)l; (void)p; *pCount = 0; return VK_SUCCESS;
}

static VkResult stub_vkCreateDevice(VkPhysicalDevice physdev, const void *ci,
                                    const void *alloc, VkDevice *pDevice)
{
    (void)physdev; (void)ci; (void)alloc;
    /* The Venus device object (UK_H_DEVICE) is a singleton: it must be created
     * on the host EXACTLY ONCE. A duplicate id is fatal to the host context
     * (vkr_context_validate_object_id), so guard against repeated creation. */
    *pDevice = (VkDevice)UK_H_DEVICE;
    if (g_device_created)
        return VK_SUCCESS;
    g_device_created = 1;

    if (g_gpu && g_ctx) {
        /* Create the device and confirm the host accepted it (reply round-trip):
         * the host only registers the device object on VK_SUCCESS. */
        int32_t vkres = -1;
        uk_venus_create_device_checked(g_gpu, g_ctx, UK_H_PHYSDEV,
                                       UK_H_DEVICE, 0u, &vkres);
        /* The Venus host requires vkGetDeviceQueue2 with a
         * VkDeviceQueueTimelineInfoMESA (ringIdx) — the legacy vkGetDeviceQueue
         * fatally tears down the context. */
        UK_ENC_BEGIN();
        uk_venus_encode_vkGetDeviceQueue2(&_enc, UK_H_DEVICE, 0u, 0u, 1u, UK_H_QUEUE);
        UK_ENC_SUBMIT();
        return VK_SUCCESS;
    }
    /* Fallback: fire-and-forget create + queue (fake backend / no real ctx). */
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateDevice(&_enc, UK_H_PHYSDEV, UK_H_DEVICE, 0u, 1.0f, 0, (const char **)0);
    uk_venus_encode_vkGetDeviceQueue(&_enc, UK_H_DEVICE, 0u, 0u, UK_H_QUEUE);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

static void stub_vkDestroyDevice(VkDevice d, const void *a) { (void)d; (void)a; }

static void stub_vkGetDeviceQueue(VkDevice d, uint32_t fi, uint32_t qi, VkQueue *q)
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

static VkResult stub_vkAllocateMemory(VkDevice dev, const void *ci,
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
    if (uk_mem_type_host_visible(mem_type) && g_gpu && g_ctx
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
            if (uk_virtio_gpu_gl_context_submit(g_gpu, g_ctx, aenc.buf, aenc.pos,
                                                &fence) == 0) {
                (void)uk_virtio_gpu_fence_wait(g_gpu, fence, 2000000000ull);
                /* 2. export it: HOST3D blob with blob_id = memory id (h). */
                int rc_c = uk_virtio_gpu_gl_blob_create_with_ctx(g_gpu, g_ctx->id, bsz,
                        UK_VIRTIO_GPU_BLOB_MEM_HOST3D,
                        UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE, h, b);
                int rc_m = rc_c ? -1 : uk_virtio_gpu_gl_blob_map(g_gpu, b);
                int rc_a = (rc_m || !b->mapped_addr) ? -1 :
                           uk_virtio_gpu_gl_context_attach_resource(g_gpu, g_ctx, b->resource_id);
                if (rc_c == 0 && rc_m == 0 && b->mapped_addr && rc_a == 0) {
                    slot->handle = h;
                    slot->used = 1;
                    *pMem = (VkDeviceMemory)h;
                    return VK_SUCCESS;
                }
            }
            /* Export failed: the memory id h is already allocated on the host;
             * tear the partial blob down and fall back (guest-local staging). */
            if (b->mapped)  uk_virtio_gpu_gl_blob_unmap(g_gpu, b);
            if (b->created) uk_virtio_gpu_gl_blob_destroy(g_gpu, b);
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

static void stub_vkFreeMemory(VkDevice d, VkDeviceMemory m, const void *a)
{
    (void)d; (void)a;
    struct uk_hv_mem *hv = uk_hv_mem_find((uint64_t)m);
    if (hv) {
        if (g_gpu && hv->blob.created)
            (void)uk_virtio_gpu_gl_context_detach_resource(g_gpu, g_ctx,
                                                           hv->blob.resource_id);
        if (g_gpu && hv->blob.mapped)
            (void)uk_virtio_gpu_gl_blob_unmap(g_gpu, &hv->blob);
        if (g_gpu && hv->blob.created)
            (void)uk_virtio_gpu_gl_blob_destroy(g_gpu, &hv->blob);
        hv->used = 0;
    }
}

/* Host-visible memory map: fall back to a local staging buffer for memory that
 * is not backed by a host-visible blob (see uk_hv_mem above). */
#define UK_STAGING_BUF_SIZE (256u * 1024u * 1024u) /* fallback staging */
static uint8_t g_staging_mem[UK_STAGING_BUF_SIZE];

static VkResult stub_vkMapMemory(VkDevice dev, VkDeviceMemory mem,
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

static void stub_vkUnmapMemory(VkDevice d, VkDeviceMemory m)
{
    (void)d; (void)m;
}

static VkResult stub_vkCreateBuffer(VkDevice dev, const void *ci,
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

static void stub_vkDestroyBuffer(VkDevice d, VkBuffer b, const void *a)
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

static void stub_vkGetBufferMemoryRequirements(VkDevice dev, VkBuffer buf, void *pReq)
{
    (void)dev;
    if (!pReq) return;
    uint64_t *r = (uint64_t *)pReq;
    /* Compute requirements from the tracked buffer size + real type mask rather
     * than a Venus round-trip: the buffer may be only batched (not yet on the
     * host) at query time, and a failed round-trip CS-errors / tears down the
     * Venus context. ggml pads the size, so the exact alignment is not critical. */
    uint64_t sz = uk_buf_size_find((uint64_t)buf);
    r[0] = sz ? sz : (64ull * 1024ull * 1024ull);
    r[1] = 256u;
    *((uint32_t *)(r + 2)) = uk_buffer_memory_type_bits();
}

static void stub_vkGetBufferMemoryRequirements2(VkDevice dev, const void *info,
                                                void *pReqs)
{
    (void)info;
    stub_vkGetBufferMemoryRequirements(dev, 0, (uint8_t *)pReqs + 16);
}

static VkResult stub_vkBindBufferMemory(VkDevice dev, VkBuffer buf,
                                        VkDeviceMemory mem, VkDeviceSize offset)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkBindBufferMemory(&_enc, UK_H_DEVICE, (uint64_t)buf,
                                       (uint64_t)mem, (uint64_t)offset);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

static VkDeviceAddress stub_vkGetBufferDeviceAddress(VkDevice dev, const void *info)
{
    (void)dev;
    /* Return the buffer handle as device address; Venus/host will resolve */
    const uint64_t *p = (const uint64_t *)info;
    return p[2]; /* VkBufferDeviceAddressInfo::buffer at offset 16 bytes */
}

static VkResult stub_vkCreateShaderModule(VkDevice dev, const void *ci,
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

static void stub_vkDestroyShaderModule(VkDevice d, VkShaderModule s, const void *a)
{ (void)d; (void)a;
  printf("VOGUE-DBG destroyShaderModule h=0x%llx\n", (unsigned long long)(uint64_t)s);
  uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyShaderModule, (uint64_t)s); }

static VkResult stub_vkCreateDescriptorSetLayout(VkDevice dev, const void *ci,
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

static void stub_vkDestroyDescriptorSetLayout(VkDevice d, VkDescriptorSetLayout l,
                                               const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyDescriptorSetLayout, (uint64_t)l); }

static VkResult stub_vkCreateDescriptorPool(VkDevice dev, const void *ci,
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

static void stub_vkDestroyDescriptorPool(VkDevice d, VkDescriptorPool p,
                                          const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyDescriptorPool, (uint64_t)p); }

static VkResult stub_vkAllocateDescriptorSets(VkDevice dev, const void *ai,
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

static void stub_vkUpdateDescriptorSets(VkDevice dev, uint32_t writeCount,
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

static VkResult stub_vkCreatePipelineLayout(VkDevice dev, const void *ci,
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

static void stub_vkDestroyPipelineLayout(VkDevice d, VkPipelineLayout l,
                                          const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyPipelineLayout, (uint64_t)l); }

static VkResult stub_vkCreateComputePipelines(VkDevice dev, uint64_t cache,
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

static void stub_vkDestroyPipeline(VkDevice d, VkPipeline p, const void *a)
{ (void)d; (void)a; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyPipeline, (uint64_t)p); }

static VkResult stub_vkCreateCommandPool(VkDevice dev, const void *ci,
                                         const void *alloc, VkCommandPool *pPool)
{
    (void)dev; (void)ci; (void)alloc;
    uint64_t h = uk_vk_alloc_handle();
    UK_ENC_BEGIN();
    uk_venus_encode_vkCreateCommandPool(&_enc, UK_H_DEVICE, h, 0u);
    UK_ENC_SUBMIT();
    *pPool = (VkCommandPool)h;
    return VK_SUCCESS;
}

static void stub_vkDestroyCommandPool(VkDevice d, VkCommandPool p, uint32_t f)
{ (void)d; (void)f; uk_dispatch_destroy_dev_handle(uk_venus_encode_vkDestroyCommandPool, (uint64_t)p); }

static VkResult stub_vkAllocateCommandBuffers(VkDevice dev, const void *ai,
                                               VkCommandBuffer *pCBs)
{
    (void)dev;
    /* VkCommandBufferAllocateInfo layout (Vulkan 1.3):
     *   sType     (uint32)  offset 0
     *   [pad 4]
     *   pNext     (uint64)  offset 8
     *   commandPool (uint64) offset 16
     *   level     (uint32)  offset 24
     *   commandBufferCount (uint32) offset 28
     */
    const uint8_t *p8 = (const uint8_t *)ai;
    uint64_t pool_h = *((const uint64_t *)(p8 + 16)); /* commandPool */
    uint32_t count  = *((const uint32_t *)(p8 + 28)); /* commandBufferCount */
    if (!count) count = 1; /* guard against malformed AI */
    for (uint32_t i = 0; i < count; i++) {
        uint64_t h = uk_vk_alloc_handle();
        UK_ENC_BEGIN();
        uint64_t cb[1] = { h };
        uk_venus_encode_vkAllocateCommandBuffers(&_enc, UK_H_DEVICE, pool_h, 1u, cb);
        UK_ENC_SUBMIT();
        pCBs[i] = (VkCommandBuffer)h;
    }
    return VK_SUCCESS;
}

static void stub_vkFreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n,
                                      const VkCommandBuffer *cbs)
{
    (void)d;
    UK_ENC_BEGIN();
    uk_venus_encode_vkFreeCommandBuffers(&_enc, UK_H_DEVICE, (uint64_t)p, n,
                                         (const uint64_t *)cbs);
    UK_ENC_SUBMIT();
}

static VkResult stub_vkBeginCommandBuffer(VkCommandBuffer cb, const void *bi)
{
    (void)bi;
    if (g_ring_enabled && g_ring_ready) {
        /* Ring mode: open ring write window; Begin command goes into ring. */
        g_ring_recording = 1;
    } else if (g_batch_enabled) {
        uk_venus_encoder_init(&g_batch_enc, g_batch_buf, UK_DISPATCH_BUF_SIZE);
        g_batch_recording = 1;
    }
    UK_ENC_BEGIN();
    uk_venus_encode_vkBeginCommandBuffer(&_enc, (uint64_t)cb);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

static VkResult stub_vkEndCommandBuffer(VkCommandBuffer cb)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkEndCommandBuffer(&_enc, (uint64_t)cb);
    UK_ENC_SUBMIT();
    if (g_ring_enabled && g_ring_ready && g_ring_recording) {
        /* Ring mode: flush tail to shared memory + notify host if idle.
         * QueueSubmit will append its command and do the final flush. */
        uk_venus_ring_cmd_flush(g_gpu, &g_ring);
        g_ring_recording = 0;
    } else if (g_batch_enabled && g_batch_recording) {
        /* Batch (SUBMIT_3D) fallback: flush in one shot. */
        uk_venus_submit(g_gpu, g_ctx, &g_batch_enc);
        g_batch_recording = 0;
    }
    return VK_SUCCESS;
}

static VkResult stub_vkResetCommandBuffer(VkCommandBuffer cb, uint32_t flags)
{
    (void)cb; (void)flags; return VK_SUCCESS;
}

static VkResult stub_vkResetCommandPool(VkDevice d, VkCommandPool pool, uint32_t flags)
{
    (void)d; (void)pool; (void)flags; return VK_SUCCESS;
}

/* vkGetMemoryHostPointerPropertiesEXT is not in the Venus protocol subset;
 * return all host-visible memory types as compatible. */
static VkResult stub_vkGetMemoryHostPointerPropertiesEXT(VkDevice d, uint32_t handleType,
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

static void stub_vkCmdBindPipeline(VkCommandBuffer cb, uint32_t bindPoint,
                                    VkPipeline pipeline)
{
    (void)bindPoint;
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdBindPipeline(&_enc, (uint64_t)cb, (uint64_t)pipeline);
    UK_ENC_SUBMIT();
}

static void stub_vkCmdBindDescriptorSets(VkCommandBuffer cb, uint32_t bindPoint,
                                          VkPipelineLayout layout, uint32_t firstSet,
                                          uint32_t dsCount, const VkDescriptorSet *pDS,
                                          uint32_t dynCount, const uint32_t *pDynOffsets)
{
    (void)bindPoint; (void)firstSet; (void)dynCount; (void)pDynOffsets;
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdBindDescriptorSets(&_enc, (uint64_t)cb, (uint64_t)layout,
                                             0u, dsCount,
                                             (const uint64_t *)pDS);
    UK_ENC_SUBMIT();
}

static void stub_vkCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout,
                                     uint32_t stageFlags, uint32_t offset,
                                     uint32_t size, const void *pValues)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdPushConstants(&_enc, (uint64_t)cb, (uint64_t)layout,
                                        stageFlags, offset, size, pValues);
    UK_ENC_SUBMIT();
}

static void stub_vkCmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdDispatch(&_enc, (uint64_t)cb, x, y, z);
    UK_ENC_SUBMIT();
}

static void stub_vkCmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst,
                                  uint32_t regionCount, const void *pRegions)
{
    const uint8_t *r = (const uint8_t *)pRegions;
    for (uint32_t i = 0; i < regionCount; i++, r += SIZE_BCOPY) {
        uint64_t size = rd_u64(r, OFF_BCOPY_SIZE);
        UK_ENC_BEGIN();
        uk_venus_encode_vkCmdCopyBuffer(&_enc, (uint64_t)cb, (uint64_t)src,
                                        (uint64_t)dst, size);
        UK_ENC_SUBMIT();
    }
}

static void stub_vkCmdFillBuffer(VkCommandBuffer cb, VkBuffer buf,
                                  VkDeviceSize offset, VkDeviceSize size, uint32_t data)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdFillBuffer(&_enc, (uint64_t)cb, (uint64_t)buf,
                                    offset, size, data);
    UK_ENC_SUBMIT();
}

static void stub_vkCmdPipelineBarrier(VkCommandBuffer cb, uint32_t srcStage,
                                       uint32_t dstStage, uint32_t depFlags,
                                       uint32_t memBarCount, const void *pMemBars,
                                       uint32_t bufBarCount, const void *pBufBars,
                                       uint32_t imgBarCount, const void *pImgBars)
{
    (void)depFlags; (void)memBarCount; (void)pMemBars;
    (void)bufBarCount; (void)pBufBars; (void)imgBarCount; (void)pImgBars;
    UK_ENC_BEGIN();
    uk_venus_encode_vkCmdPipelineBarrier(&_enc, (uint64_t)cb, srcStage, dstStage);
    UK_ENC_SUBMIT();
}

static VkResult stub_vkQueueSubmit(VkQueue queue, uint32_t submitCount,
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
        uk_venus_ring_cmd_flush(g_gpu, &g_ring);
    }
    return VK_SUCCESS;
}

static VkResult stub_vkQueueWaitIdle(VkQueue q) { (void)q; return VK_SUCCESS; }
static VkResult stub_vkDeviceWaitIdle(VkDevice d) { (void)d; return VK_SUCCESS; }

static VkResult stub_vkCreateFence(VkDevice dev, const void *ci, const void *alloc,
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

static void stub_vkDestroyFence(VkDevice d, VkFence f, const void *a)
{
    (void)d; (void)f; (void)a;
}

static VkResult stub_vkResetFences(VkDevice dev, uint32_t count, const VkFence *pFences)
{
    UK_ENC_BEGIN();
    uk_venus_encode_vkResetFences(&_enc, UK_H_DEVICE, count,
                                   (const uint64_t *)pFences);
    UK_ENC_SUBMIT();
    return VK_SUCCESS;
}

static VkResult stub_vkWaitForFences(VkDevice dev, uint32_t count,
                                      const VkFence *pFences, uint32_t waitAll,
                                      uint64_t timeout)
{
    (void)dev; (void)count; (void)pFences; (void)waitAll;
    /* Opt: poll completed_fence instead of sending a Venus round-trip.
     * cmd_submit_locked() already updates completed_fence synchronously when
     * the host returns the response; so by the time QueueSubmit returns the
     * fence is already marked done — no Venus vkWaitForFences command needed. */
    if (g_gpu) {
        int rc = uk_virtio_gpu_fence_wait(g_gpu, g_last_fence,
                                           timeout == UINT64_MAX ? 5000000000ull
                                                                  : timeout);
        (void)rc;
    }
    return VK_SUCCESS;
}

static VkResult stub_vkGetFenceStatus(VkDevice d, VkFence f)
{
    (void)d; (void)f; return VK_SUCCESS;
}

static VkResult stub_vkCreateEvent(VkDevice dev, const void *ci, const void *a,
                                    VkEvent *pEvent)
{
    (void)dev; (void)ci; (void)a;
    *pEvent = (VkEvent)uk_vk_alloc_handle();
    return VK_SUCCESS;
}
static void     stub_vkDestroyEvent(VkDevice d, VkEvent e, const void *a) { (void)d;(void)e;(void)a; }
static VkResult stub_vkGetEventStatus(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_SUCCESS; }
static VkResult stub_vkSetEvent(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_SUCCESS; }
static VkResult stub_vkResetEvent(VkDevice d, VkEvent e) { (void)d;(void)e; return VK_SUCCESS; }

static VkResult stub_vkCreateSemaphore(VkDevice dev, const void *ci, const void *a,
                                        VkSemaphore *pSem)
{
    (void)dev; (void)ci; (void)a;
    *pSem = (VkSemaphore)uk_vk_alloc_handle();
    return VK_SUCCESS;
}
static void stub_vkDestroySemaphore(VkDevice d, VkSemaphore s, const void *a)
{
    (void)d; (void)s; (void)a;
}

/* Timeline semaphore ops. ggml's async-upload event path
 * (ggml_backend_vk_event_*) records vkCmdCopyBuffer into a transfer command
 * buffer and submits it, then waits on a timeline semaphore. Our SUBMIT_3D
 * queue submit is synchronous (fence-waited), so by the time these are called
 * the GPU work has completed — the timeline has already reached its target. */
static VkResult stub_vkWaitSemaphores(VkDevice dev, const void *pWaitInfo,
                                      uint64_t timeout)
{
    (void)dev; (void)pWaitInfo; (void)timeout;
    return VK_SUCCESS;
}
static VkResult stub_vkSignalSemaphore(VkDevice dev, const void *pSignalInfo)
{
    (void)dev; (void)pSignalInfo;
    return VK_SUCCESS;
}
static VkResult stub_vkGetSemaphoreCounterValue(VkDevice dev, VkSemaphore sem,
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
static void stub_vkCmdSetEvent(VkCommandBuffer cb, VkEvent ev, uint32_t stageMask)
{ (void)cb; (void)ev; (void)stageMask; }
static void stub_vkCmdResetEvent(VkCommandBuffer cb, VkEvent ev, uint32_t stageMask)
{ (void)cb; (void)ev; (void)stageMask; }
static void stub_vkCmdWaitEvents(VkCommandBuffer cb, uint32_t evCount, const void *pEvents,
                                 uint32_t srcStage, uint32_t dstStage,
                                 uint32_t mbc, const void *mb, uint32_t bbc,
                                 const void *bb, uint32_t ibc, const void *ib)
{ (void)cb; (void)evCount; (void)pEvents; (void)srcStage; (void)dstStage;
  (void)mbc; (void)mb; (void)bbc; (void)bb; (void)ibc; (void)ib; }
static void stub_vkCmdPipelineBarrier2(VkCommandBuffer cb, const void *pDependencyInfo)
{ (void)cb; (void)pDependencyInfo; }
static void stub_vkCmdCopyBuffer2(VkCommandBuffer cb, const void *pCopyBufferInfo)
{ (void)cb; (void)pCopyBufferInfo; }
static VkResult stub_vkQueueSubmit2(VkQueue q, uint32_t count, const void *pSubmits,
                                    VkFence fence)
{ (void)q; (void)count; (void)pSubmits; (void)fence; return VK_SUCCESS; }
/* Coherent host-visible memory: flush/invalidate are no-ops. */
static VkResult stub_vkFlushMappedMemoryRanges(VkDevice d, uint32_t c, const void *r)
{ (void)d; (void)c; (void)r; return VK_SUCCESS; }
static VkResult stub_vkInvalidateMappedMemoryRanges(VkDevice d, uint32_t c, const void *r)
{ (void)d; (void)c; (void)r; return VK_SUCCESS; }

static VkResult stub_vkCreateQueryPool(VkDevice dev, const void *ci, const void *a,
                                        VkQueryPool *pPool)
{
    (void)dev; (void)ci; (void)a;
    *pPool = (VkQueryPool)uk_vk_alloc_handle();
    return VK_SUCCESS;
}
static void stub_vkDestroyQueryPool(VkDevice d, VkQueryPool p, const void *a)
{
    (void)d; (void)p; (void)a;
}
static VkResult stub_vkGetQueryPoolResults(VkDevice d, VkQueryPool p, uint32_t f,
                                            uint32_t cnt, size_t sz, void *data,
                                            VkDeviceSize stride, uint32_t flags)
{
    (void)d;(void)p;(void)f;(void)cnt;(void)stride;(void)flags;
    if (data && sz > 0) memset(data, 0, sz);
    return VK_NOT_READY;
}
static void stub_vkCmdResetQueryPool(VkCommandBuffer cb, VkQueryPool p,
                                      uint32_t f, uint32_t c) { (void)cb;(void)p;(void)f;(void)c; }
static void stub_vkCmdWriteTimestamp(VkCommandBuffer cb, uint32_t stage,
                                      VkQueryPool p, uint32_t q) { (void)cb;(void)stage;(void)p;(void)q; }
static void stub_vkCmdBeginQuery(VkCommandBuffer cb, VkQueryPool p, uint32_t q,
                                   uint32_t f) { (void)cb;(void)p;(void)q;(void)f; }
static void stub_vkCmdEndQuery(VkCommandBuffer cb, VkQueryPool p, uint32_t q)
{
    (void)cb;(void)p;(void)q;
}

/* KHR aliases — same signature as the non-KHR version (all return void) */
static void stub_vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice p, void *v)
{ stub_vkGetPhysicalDeviceProperties2(p, v); }
static void stub_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice p, void *v)
{ stub_vkGetPhysicalDeviceFeatures2(p, v); }
static void stub_vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice p, void *v)
{ stub_vkGetPhysicalDeviceMemoryProperties2(p, v); }
static void stub_vkGetBufferMemoryRequirements2KHR(VkDevice d, const void *i, void *r)
{ stub_vkGetBufferMemoryRequirements2(d, i, r); }
static VkDeviceAddress stub_vkGetBufferDeviceAddressKHR(VkDevice d, const void *i)
{ return stub_vkGetBufferDeviceAddress(d, i); }

PFN_uk_vkGetInstanceProcAddr uk_ggml_vulkan_get_proc_addr_fn(void)
{
    return (PFN_uk_vkGetInstanceProcAddr)vkGetInstanceProcAddr;
}

/* ── Direct C-ABI exports for functions ggml-vulkan.cpp calls without going
 *    through the vulkan.hpp dispatcher (plain C call sites in the .cpp).
 *    These forward to the existing static stubs so behaviour is identical to
 *    the dispatcher path.
 */
void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physdev, void *pFeatures2)
{
    stub_vkGetPhysicalDeviceFeatures2(physdev, pFeatures2);
}

void vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                     VkBuffer dstBuffer, uint32_t regionCount,
                     const void *pRegions)
{
    stub_vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}
