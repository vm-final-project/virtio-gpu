#include "vk_internal.h"
/* SPDX-License-Identifier: MIT */
/*
 * Mesa alignment:
 *   Analogue: src/vulkan/runtime object-family entrypoint organization.
 *   Same: keep exported Vulkan ABI grouped behind runtime state and proc lookup.
 *   VOGUE adaptation: static single-image proc table for ggml-vulkan only.
 */
/*
 * libvulkan/runtime/vk_entrypoints.c — Static Vulkan C ABI for ggml-vulkan
 *
 * Implements the Vulkan C functions that ggml-vulkan.cpp calls through
 * vulkan.hpp.  Each function encodes the call to Venus wire format via
 * libukvulkan_venus and submits via VirtIO-GPU SUBMIT_3D.
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
#include <uk/vulkan_venus.h>
#include <uk/venus.h>
#include <uk/vulkan.h>
#include "vk_state.h"

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

/* ── Global Vulkan/Venus state ─────────────────────────────────────────── */
struct uk_vulkan_state        g_vk;
/* Last fence submitted via QueueSubmit; polled by WaitForFences. */
uk_gpu_fence_id               g_last_fence;

/* Real host VkPhysicalDeviceMemoryProperties (520B), filled by the first real
 * round-trip in stub_vkGetPhysicalDeviceMemoryProperties. Used by both that stub
 * and the host-visible memory-type test in stub_vkAllocateMemory. */
uint8_t g_memprops[520];
int     g_memprops_valid;
int     g_props_query_blocked_reported;
int     g_memprops_query_blocked_reported;
int     g_buffer_req_query_blocked_reported;
int     g_buffer_req_tracked_reported;

/* The Venus VkDevice is a singleton created exactly once (duplicate object ids
 * are fatal to the host context). */
/* Device singleton state lives in g_vk.device_created. */

/* ── Handle pool ───────────────────────────────────────────────────────── */
#define UK_VK_HANDLE_BASE  0x0002000000000000ULL
/* Dynamic object ids MUST start above the fixed well-known handles below,
 * otherwise a dynamically-allocated object (buffer/memory/...) reuses the id of
 * the instance/physdev/device/queue. The host (virglrenderer
 * vkr_context_validate_object_id) treats a duplicate object id as fatal and
 * tears the Venus context down — which surfaced as a vkCreateDevice /
 * vkGetDeviceQueue "CS error" once real allocations began. */
#define UK_VK_HANDLE_FIRST_DYNAMIC UK_VULKAN_STATE_HANDLE_FIRST_DYNAMIC

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
struct uk_mutex g_disp_lock =
	UK_MUTEX_INITIALIZER_RECURSIVE(g_disp_lock);

uint64_t uk_vk_alloc_handle(void)
{
    return uk_vulkan_state_alloc_handle(&g_vk);
}

void uk_vk_report_blocked_once(int *reported, const char *reason)
{
    if (reported && *reported)
        return;
    if (reported)
        *reported = 1;
    printf("uk-llama-vk-blocked: %s\n", reason);
    printf("uk-ggml-vk: %s\n", reason);
}

void uk_vk_report_diag_once(int *reported, const char *message)
{
    if (reported && *reported)
        return;
    if (reported)
        *reported = 1;
    printf("uk-ggml-vk: diagnostic:%s\n", message);
}

/* ── Encoder buffer pool ───────────────────────────────────────────────── */
/* Must hold the largest single Venus command we encode. The dominant case is
 * vkCreateShaderModule, whose body is a full SPIR-V module: ggml-vulkan's big
 * matmul / flash-attention shaders reach ~72 KB and some specialised variants
 * are larger, so a 64 KB buffer silently overflowed (uk_venus_submit returns
 * -EOVERFLOW, the create is dropped, and a later pipeline referencing the
 * never-created VkShaderModule fails host object lookup with a CS error).
 * 2 MB covers every ggml-vulkan SPIR-V with wide margin. */
uint8_t g_enc_buf[UK_DISPATCH_BUF_SIZE];

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
int                  g_batch_enabled;     /* 0 = sync, 1 = batched (SUBMIT_3D) */
int                  g_batch_recording;   /* in vkBegin..vkEnd window */
struct uk_venus_encoder g_batch_enc;
uint8_t              g_batch_buf[UK_DISPATCH_BUF_SIZE];

/* Ring stream state (active when g_ring_enabled=1) */
static int                  g_ring_want;         /* requested via env (default 1) */
static int                  g_ring_tried;        /* lazy-init attempted */
int                  g_ring_enabled;
struct uk_venus_ring g_ring;
int                  g_ring_ready;        /* 1 after uk_venus_ring_register() */
int                  g_ring_recording;    /* in vkBegin..vkEnd ring window */
void                 uk_dispatch_ring_lazy_init(void);

int uk_dispatch_batch_active(void)
{
    return g_batch_enabled && g_batch_recording;
}

int uk_dispatch_ring_active(void)
{
    return g_ring_enabled && g_ring_ready && g_ring_recording;
}

/* Helpers for single-call encode + submit, batched when active. */
void uk_disp_lock(void)   { uk_mutex_lock(&g_disp_lock); }
void uk_disp_unlock(void) { uk_mutex_unlock(&g_disp_lock); }

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
                       "(command dropped)\n", (unsigned int)_enc.pos, UK_DISPATCH_BUF_SIZE); \
            uk_venus_submit(g_vk.gpu, g_vk.ctx, &_enc); \
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
void uk_dispatch_destroy_dev_handle(uk_venus_encode_destroy_fn fn,
                                             uint64_t handle)
{
    UK_ENC_BEGIN();
    fn(&_enc, UK_H_DEVICE, handle);
    UK_ENC_SUBMIT();
}

/* ── Dispatch init ─────────────────────────────────────────────────────── */
int uk_vulkan_init(void)
{
    if (g_vk.dispatch_initialized)
        return 0;
    if (uk_vulkan_state_init(&g_vk) != 0)
        return -1;

    /* Open the VirtIO-GPU device + Venus context via the native Venus driver
     * (libukvulkan_venus). uk_vulkan_venus_open() probes the device through
     * libukvirtio_gpu directly (no Linux virtgpu DRM UAPI) and creates the
     * Venus context. */
    if (uk_vulkan_venus_open(&g_vk.venus, 0) != 0) {
        uk_vulkan_state_fini(&g_vk);
        return -1;
    }

    g_vk.gpu = uk_vulkan_venus_gpu(&g_vk.venus);
    g_vk.ctx = uk_vulkan_venus_ctx(&g_vk.venus);
    g_vk.dispatch_initialized = 1;
    g_vk.initialized = 1;

    /* Default to SYNC (per-call submit) so the native dispatch test, which
     * counts submits_3d after each stub, keeps observing one submit per
     * call. Production images enable batching via the environment. */
    {
        const char *s = getenv("UK_GGML_VK_DISPATCH_BATCH");
        g_batch_enabled = (s && (*s == '1' || *s == 'y' || *s == 'Y'));
    }
    g_batch_recording = 0;
    uk_venus_encoder_init(&g_batch_enc, g_batch_buf, UK_DISPATCH_BUF_SIZE);

    /* Ring stream model: OPT-IN (UK_GGML_VK_DISPATCH_RING=1).
     *
     * Measured finding (results/perf/optimization_multienv.md): on this
     * QEMU 11 + virglrenderer Venus + single-vCPU stack the ring stream
     * REGRESSES wall-clock throughput (~-84% decode) even though it reduces the
     * virtqueue submit *count*. Each ring flush still issues a synchronous
     * vkNotifyRingMESA SUBMIT_3D, and the host ring_thread drain latency
     * dominates vs the batched SUBMIT_3D path that virglrenderer processes
     * inline. So the production default is the batched path + the WaitForFences
     * completed_fence poll + the pause busy-wait (both retained, both wins).
     * The ring is kept opt-in for stacks where the host ring_thread is cheaper.
     *
     * The ring is created LAZILY (uk_dispatch_ring_lazy_init) on the first
     * command buffer — the host rejects a host-visible blob until the Venus
     * VkInstance/VkDevice exist (after ggml's vkCreateInstance/Device). */
    {
        const char *r = getenv("UK_GGML_VK_DISPATCH_RING");
        g_ring_want = (r && (*r == '1'));
    }
    return 0;
}

/* Lazily create + register the Venus command ring. Called once, after the
 * Venus device is up (first vkBeginCommandBuffer). On any failure the ring
 * stays disabled and the dispatch falls back to the SUBMIT_3D batch path. */
void uk_dispatch_ring_lazy_init(void)
{
    int rc;
    if (!g_ring_want || g_ring_tried)
        return;
    g_ring_tried = 1;

    rc = uk_venus_ring_create_on_ctx(g_vk.gpu, &g_ring, g_vk.ctx,
                                     UK_VENUS_RING_CTRL_SIZE +
                                     UK_VENUS_RING_DEFAULT_SIZE,
                                     UK_VENUS_RING_DEFAULT_BLOB_ID + 1);
    if (rc != 0) {
        printf("uk-ggml-vk: ring create failed rc=%d, falling back to SUBMIT_3D\n", rc);
        return;
    }
    rc = uk_venus_ring_register(g_vk.gpu, &g_ring, UK_VENUS_RING_DEFAULT_BLOB_ID + 1);
    if (rc != 0) {
        uk_venus_ring_destroy(g_vk.gpu, &g_ring);
        printf("uk-ggml-vk: ring register failed rc=%d, falling back to SUBMIT_3D\n", rc);
        return;
    }
    uk_venus_ring_bind_current(g_vk.gpu, g_vk.ctx);
    g_ring_enabled = 1;
    g_ring_ready   = 1;
    printf("uk-ggml-vk: ring stream enabled (buf=%u B)\n", UK_VENUS_RING_DEFAULT_SIZE);
}

/* Number of vk* commands wired into the static dispatch table (k_procs[]).
 * Kept in sync with the table by a _Static_assert after its definition. */
#define UK_VULKAN_SUPPORTED_COMMAND_COUNT 93u

/* plan-optimize.md L3.4: surface the host-blob mapping state and the L3.1
 * batching flag so the appliance can log them and the perf gate can record
 * them per evidence row. */
void uk_vulkan_get_info(struct uk_vulkan_info *out)
{
    if (!out)
        return;
    out->initialized   = g_vk.initialized && g_vk.dispatch_initialized;
    /* libvulkan statically links a single Vulkan driver implementation; the
     * current backend is the Unikraft-native Venus driver (libukvulkan_venus). */
    out->driver_name   = "venus";
    /* Advertised API subset: the compute-first slice required by upstream
     * ggml-vulkan.cpp (see plan-redesign-vulkan.md claim boundary). */
    out->claim_boundary = "ggml compute subset";
    /* Number of vk* commands wired into the static dispatch table; mirrors the
     * Venus generator slice config in scripts/venus/pin.json. */
    out->supported_command_count = UK_VULKAN_SUPPORTED_COMMAND_COUNT;
    out->batch_enabled = g_batch_enabled;
    out->ring_enabled  = g_ring_enabled && g_ring_ready;
    /* The fixed-blob window is set when the underlying VirtIO-GPU device
     * supports it. We probe the metrics counter for a host_blob_mapped
     * marker once the real driver records it; for now leave it zero so
     * the appliance reports an honest `hostmem_fixed=0` blocker. */
    out->hostmem_fixed = 0;
}

/* Idempotent teardown of the dispatch layer. The VirtIO-GPU device/context are
 * owned by the libukvulkan_venus native driver; here we drop the Venus ring
 * and mark the dispatch uninitialised so a subsequent uk_vulkan_init() re-opens. */
void uk_vulkan_shutdown(void)
{
    if (!g_vk.dispatch_initialized)
        return;
    if (g_ring_ready) {
        uk_venus_ring_unregister(g_vk.gpu, &g_ring);
        uk_venus_ring_destroy(g_vk.gpu, &g_ring);
        g_ring_ready = 0;
    }
    g_vk.dispatch_initialized = 0;
    uk_vulkan_state_fini(&g_vk);
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

/* Keep UK_VULKAN_SUPPORTED_COMMAND_COUNT in sync with k_procs[] (excluding the
 * { NULL, NULL } terminator). uk_vulkan_get_info() reports this count. */
_Static_assert(sizeof(k_procs) / sizeof(k_procs[0]) - 1
               == UK_VULKAN_SUPPORTED_COMMAND_COUNT,
               "UK_VULKAN_SUPPORTED_COMMAND_COUNT must equal k_procs entry count");

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


/* ── Stub implementations ──────────────────────────────────────────────── */

PFN_uk_vkGetInstanceProcAddr uk_vulkan_get_instance_proc_addr_fn(void)
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
