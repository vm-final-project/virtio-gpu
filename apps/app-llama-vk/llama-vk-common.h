/*
 * Shared helpers for the Vulkan/Venus bench and server single-purpose
 * appliances. Both modes route through libvulkan → libukvulkan_venus → QEMU
 * virtio-gpu-gl-pci,venus=true.
 */
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <uk/console.h>
}

#include "llama.h"
#include <uk/vulkan.h>

#ifndef CONFIG_APP_LLAMA_VK_THREADS
#define CONFIG_APP_LLAMA_VK_THREADS 1
#endif

static inline void uk_puts(const char *s)
{
    uk_console_out(s, strlen(s));
}

static inline void uk_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uk_console_out(buf, strlen(buf));
}

static inline double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static inline llama_model *load_model_vk(const char *model_path, const char *tag)
{
    mkdir("/mnt", 0755);
    mkdir("/mnt/model", 0755);
    if (mount("model", "/mnt/model", "9pfs", 0, "") != 0) {
        uk_printf("%s: FAIL 9pfs mount failed errno=%d\n", tag, errno);
        return nullptr;
    }

    if (uk_vulkan_init() != 0) {
        uk_printf("%s: FAIL dispatch_init failed\n", tag);
        return nullptr;
    }

    llama_backend_init();
    llama_numa_init(GGML_NUMA_STRATEGY_DISABLED);

    llama_model_params mparams = llama_model_default_params();
    mparams.use_mmap     = false;
    mparams.n_gpu_layers = 99;

    double t0 = now_sec();
    llama_model *model = llama_model_load_from_file(model_path, mparams);
    double load_ms = (now_sec() - t0) * 1000.0;

    uk_printf("%s: model_load path=%s use_mmap=%d huge_pages=%d "
              "elapsed_ms=%.2f\n",
              tag, model_path, (int)mparams.use_mmap, 0, load_ms);

    if (!model) {
        uk_printf("%s: FAIL model_load failed path=%s\n", tag, model_path);
        llama_backend_free();
        return nullptr;
    }
    return model;
}
