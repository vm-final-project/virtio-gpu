/* SPDX-License-Identifier: MIT */
/*
 * libvulkan — application-facing Vulkan ABI/runtime boundary for Unikraft.
 *
 * libvulkan owns the exported vk* symbols, vkGetInstanceProcAddr /
 * vkGetDeviceProcAddr, the Vulkan-Hpp DispatchLoaderDynamic glue, and the
 * common Vulkan runtime state needed by the supported (compute-first) subset.
 * It dispatches from the public Vulkan entry points to the statically linked
 * driver implementation (currently libukvulkan_venus).
 *
 * Upstream Vulkan clients should include the normal Vulkan headers and call
 * vk* entry points; they only need <uk/vulkan.h> when the Unikraft appliance
 * chooses explicit boot-time initialisation / diagnostics.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialise the static Vulkan dispatch layer: connect to the VirtIO-GPU
 * device, verify the Venus capset, open a Venus context, and populate the
 * dispatch function table. Must be called before any vk* function (or
 * ggml_backend_vk_init()). Returns 0 on success, -1 on failure.
 */
int uk_vulkan_init(void);

/* Tear down the dispatch layer (idempotent). */
void uk_vulkan_shutdown(void);

/*
 * Diagnostic snapshot of the libvulkan runtime + active driver, surfaced for
 * appliance logging and the perf/evidence gates.
 *
 * `driver_name`             — backend driver, e.g. "venus".
 * `claim_boundary`          — advertised API subset, e.g. "ggml compute subset".
 * `supported_command_count` — number of vk* commands wired into the dispatch.
 * `batch_enabled`           — non-zero when per-command SUBMIT_3D is collapsed
 *                             into one submit per command buffer.
 * `ring_enabled`            — non-zero when the Venus ring stream model is active.
 * `hostmem_fixed`           — non-zero when the host VirtIO-GPU blob window was
 *                             mapped at the requested address.
 */
struct uk_vulkan_info {
    int         initialized;
    const char *driver_name;
    const char *claim_boundary;
    uint32_t    supported_command_count;
    int         batch_enabled;
    int         ring_enabled;
    int         hostmem_fixed;
};

void uk_vulkan_get_info(struct uk_vulkan_info *out);

/*
 * Vulkan-Hpp DispatchLoaderDynamic glue.
 *
 * uk_vulkan_get_instance_proc_addr_fn() returns the static Venus-backed
 * vkGetInstanceProcAddr; uk_vulkan_dispatcher_init_with_proc_addr() wires
 * vulkan.hpp's VULKAN_HPP_DEFAULT_DISPATCHER to it (defined in the C++ loader TU).
 */
typedef void *(*PFN_uk_vkVoidFunction)(void);
typedef PFN_uk_vkVoidFunction (*PFN_uk_vkGetInstanceProcAddr)(uint64_t instance,
                                                              const char *pName);
PFN_uk_vkGetInstanceProcAddr uk_vulkan_get_instance_proc_addr_fn(void);
void uk_vulkan_dispatcher_init_with_proc_addr(void);

#ifdef __cplusplus
}
#endif
