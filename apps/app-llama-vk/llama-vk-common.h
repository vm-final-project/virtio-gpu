/*
 * Vulkan/Venus-appliance helpers. Delegates shared utilities to
 * llama-uk-common.h and provides the VK-specific load_model_vk() wrapper
 * which first wires the static Venus dispatch chain, then offloads all
 * layers to the host GPU (n_gpu_layers=99).
 */
#pragma once

#include "llama-uk-common.h"

extern "C" {
#include <uk/vulkan.h>
}

#ifndef CONFIG_APP_LLAMA_VK_THREADS
#define CONFIG_APP_LLAMA_VK_THREADS 1
#endif

static inline llama_model *load_model_vk(const char *model_path, const char *tag)
{
    if (uk_vulkan_init() != 0) {
        uk_printf("%s: FAIL dispatch_init failed\n", tag);
        return nullptr;
    }
    return load_model_common(model_path, tag, 99);
}
