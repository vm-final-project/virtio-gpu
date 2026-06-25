/* SPDX-License-Identifier: MIT */
/*
 * libvulkan/include/vulkan/vulkan.h — forward to the pinned Khronos
 * Vulkan-Headers checkout.
 *
 * libvulkan owns the exported vk* entry points, NOT the Vulkan type
 * definitions. The canonical Vulkan types/prototypes come from the pinned
 * Khronos Vulkan-Headers checkout (config/deps.json:
 * VULKAN_HEADERS_INCLUDE). This shim simply forwards to that checkout so a
 * client that puts libvulkan/include ahead of the Khronos headers still
 * resolves the real Vulkan API.
 *
 * NOTE: the in-image dispatch TU (runtime/vk_entrypoints.c) defines its own
 * minimal handle typedefs and does not include this header; the Vulkan-Hpp
 * loader includes <vulkan/vulkan.hpp> directly against the Khronos headers.
 */
#pragma once
#include_next <vulkan/vulkan.h>
