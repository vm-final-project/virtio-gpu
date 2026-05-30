// SPDX-License-Identifier: MIT
//
// uk_ggml_vk_loader.cpp — vulkan.hpp DispatchLoaderDynamic storage for vk.ggml-dispatch.
//
// ggml-vulkan.cpp includes vulkan.hpp with:
//   #define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
//   VULKAN_HPP_DEFAULT_DISPATCHER_TYPE = vk::DispatchLoaderDynamic
//
// It expects one TU to provide the storage macro:
//   VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
//
// We provide that here, then initialize the dispatcher from our static
// vkGetInstanceProcAddr (uk_vkGetInstanceProcAddr), which routes through
// libukvenus → VirtIO-GPU SUBMIT_3D.
//
// Link order requirement:
//   libukggml_vulkan.a must come before ggml-vulkan.o in the final link so
//   this TU's weak-symbol override for the dispatcher storage wins.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_EXCEPTIONS 1
#define VULKAN_HPP_NO_NODISCARD_WARNINGS 1

// Suppress validation layer / extension string checks inside vulkan.hpp when
// building against our stub vk_platform headers.
#ifndef VULKAN_HPP_ASSERT
#define VULKAN_HPP_ASSERT(expr) ((void)(expr))
#endif

#include <vulkan/vulkan.hpp>

// Storage definition — exactly one TU must define this.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

extern "C" {
// Provided by uk_vulkan_dispatch.c: our static Venus-backed proc-addr fn.
typedef void *(*PFN_uk_vkVoidFunction)(void);
typedef PFN_uk_vkVoidFunction (*PFN_uk_vkGetInstanceProcAddr)(uint64_t instance,
                                                              const char *pName);
PFN_uk_vkGetInstanceProcAddr uk_ggml_vulkan_get_proc_addr_fn(void);
} // extern "C"

// Called from uk_ggml_vulkan_dispatch_init() after the Venus context is open.
// Wires vulkan.hpp's global dispatcher to use our static proc-addr table.
extern "C" void uk_ggml_vulkan_dispatcher_init_with_proc_addr(void)
{
    // Reinterpret our uint64_t-typed proc-addr fn as PFN_vkGetInstanceProcAddr.
    // Both have the signature (instance, name) → PFN_vkVoidFunction; the only
    // difference is the handle type width, which is 64-bit on both paths.
    auto raw_pfn = uk_ggml_vulkan_get_proc_addr_fn();
    auto pfn = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void *>(raw_pfn));

    // Init without an instance so the loader-level functions resolve first;
    // ggml-vulkan.cpp will call init(instance) again after vkCreateInstance.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(pfn);
}
