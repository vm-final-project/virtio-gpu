/* SPDX-License-Identifier: MIT */
/*
 * libukggml_vulkan — Static Vulkan ICD dispatch for ggml-vulkan inside Unikraft
 *
 * Provides the Vulkan C ABI (vkCreateInstance, vkCreateDevice, ...) backed
 * by VOGUE's Venus encoder + VirtIO-GPU SUBMIT_3D transport.  Eliminates the
 * need for dlopen/libvulkan.so inside the unikernel; replaces vulkan.hpp's
 * VULKAN_HPP_DISPATCH_LOADER_DYNAMIC with a statically-resolved table.
 *
 * This is the vk.ggml-dispatch gate for linking full upstream ggml-vulkan.cpp inside
 * a Unikraft unikernel.
 *
 * Architecture:
 *   ggml-vulkan.cpp (upstream, unmodified)
 *     -> vulkan.hpp dispatcher (VULKAN_HPP_DEFAULT_DISPATCHER)
 *       -> uk_vulkan_dispatch: vkCreateInstance / vkCreateDevice / ...
 *         -> libukvenus encoder
 *           -> libukvirtio_gpu SUBMIT_3D
 *             -> virglrenderer Venus → host NVIDIA GPU
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the static Vulkan dispatch layer.
 * Must be called before ggml_backend_vk_init() or any vk* function.
 * Connects to the VirtIO-GPU device, verifies Venus capset, opens a
 * Venus rendering context, and populates the dispatch function table.
 *
 * Returns 0 on success, -1 on failure.
 */
int uk_ggml_vulkan_dispatch_init(void);

/*
 * Provides the storage for the vulkan.hpp DispatchLoaderDynamic instance
 * used by ggml-vulkan.cpp via VULKAN_HPP_DEFAULT_DISPATCHER.
 * Called once from uk_ggml_vulkan_dispatch_init().
 */
void uk_ggml_vulkan_dispatcher_init_with_proc_addr(void);

/*
 * Returns the PFN for use as the vkGetInstanceProcAddr parameter in
 * VULKAN_HPP_DEFAULT_DISPATCHER.init(pfn).
 * The returned function routes through libukvenus → VirtIO-GPU → Venus.
 */
typedef void *(*PFN_uk_vkVoidFunction)(void);
typedef PFN_uk_vkVoidFunction (*PFN_uk_vkGetInstanceProcAddr)(uint64_t instance,
                                                              const char *pName);
PFN_uk_vkGetInstanceProcAddr uk_ggml_vulkan_get_proc_addr_fn(void);

/* plan-optimize.md L3.4 + L3.1 telemetry.
 *
 * `batch_enabled`     — non-zero when UK_GGML_VK_DISPATCH_BATCH=1 collapses
 *                        per-command SUBMIT_3D into one per command buffer.
 * `hostmem_fixed`     — non-zero when the host VirtIO-GPU blob window was
 *                        mapped at the address we requested. Mirrors the
 *                        virglrenderer 1.3+ map_fixed path; if zero we paid
 *                        an extra mmap copy. Always 0 until QEMU/host hands
 *                        the dispatch layer a real mapping (until then it is
 *                        an honest blocker indicator).
 * `wanted_extensions` — number of Vulkan extensions sliced into the static
 *                        dispatch table (matches scripts/gen_libukvenus.py).
 */
struct uk_ggml_vulkan_dispatch_info {
    int      batch_enabled;
    int      hostmem_fixed;
    uint32_t wanted_extensions;
};

void uk_ggml_vulkan_dispatch_get_info(struct uk_ggml_vulkan_dispatch_info *out);

#ifdef __cplusplus
}
#endif
