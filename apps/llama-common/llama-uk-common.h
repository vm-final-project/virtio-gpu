/*
 * Shared helpers for all llama.cpp single-purpose appliances.
 *
 * Provides: uk_puts, uk_printf, now_sec, load_model_common, run_bench_loop.
 * Each app-specific header includes this file and adds its own
 * load_model / load_model_vk wrapper.
 */
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <uk/console.h>
}

#include "llama.h"

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

static inline int mount_model_fs(const char *tag)
{
    mkdir("/mnt", 0755);
    mkdir("/mnt/model", 0755);
    if (mount("model", "/mnt/model", "9pfs", 0, "") != 0) {
        uk_printf("%s: FAIL 9pfs mount failed errno=%d\n", tag, errno);
        return -1;
    }
    return 0;
}

/*
 * Mount /mnt/model over 9pfs, initialise the llama backend, and load the
 * GGUF at model_path. n_gpu_layers controls GPU offload: pass 0 for CPU-only,
 * 99 to offload all layers to the Vulkan device.
 *
 * Returns a valid llama_model* on success, nullptr on failure (backend freed).
 */
static inline llama_model *load_model_common(const char *model_path,
                                              const char *tag,
                                              int n_gpu_layers)
{
    if (mount_model_fs(tag) != 0)
        return nullptr;

    llama_backend_init();
    llama_numa_init(GGML_NUMA_STRATEGY_DISABLED);

    llama_model_params mparams = llama_model_default_params();
    mparams.use_mmap     = false;
    mparams.n_gpu_layers = n_gpu_layers;
    /* Bypass the pinned Vulkan_Host buffer type (used by default for weights
     * like token_embd to speed host->GPU batch transfers). Over Venus that
     * buffer is backed by a host-visible VirtIO-GPU blob whose upload/mapping
     * does not complete on a software host driver, deadlocking model load.
     * no_host keeps all weights in device-local Vulkan0 memory instead. */
    mparams.no_host      = true;

    double t0 = now_sec();
    llama_model *model = llama_model_load_from_file(model_path, mparams);
    double load_ms = (now_sec() - t0) * 1000.0;

    uk_printf("%s: model_load path=%s use_mmap=%d n_gpu_layers=%d elapsed_ms=%.2f\n",
              tag, model_path, (int)mparams.use_mmap, n_gpu_layers, load_ms);

    if (!model) {
        uk_printf("%s: FAIL model_load failed path=%s\n", tag, model_path);
        llama_backend_free();
        return nullptr;
    }
    return model;
}

struct bench_result { double pp512; double tg128; };

/*
 * Run pp512 + tg128 on an already-initialised llama_context and return
 * tokens/sec for each pass. The caller is responsible for creating the
 * context with n_ctx >= 640 and n_batch >= 512.
 */
static inline bench_result run_bench_loop(llama_context *ctx)
{
    const int n_pp = 512;
    std::vector<llama_token> tokens(n_pp, 0);
    llama_batch batch = llama_batch_get_one(tokens.data(), n_pp);

    double t0 = now_sec();
    llama_decode(ctx, batch);
    double pp_ms = (now_sec() - t0) * 1000.0;

    llama_memory_clear(llama_get_memory(ctx), false);

    const int n_tg = 128;
    llama_pos tg_pos = 0;
    t0 = now_sec();
    for (int i = 0; i < n_tg; i++) {
        llama_token t = 0;
        batch = llama_batch_get_one(&t, 1);
        if (batch.pos) batch.pos[0] = tg_pos++;
        llama_decode(ctx, batch);
    }
    double tg_ms = (now_sec() - t0) * 1000.0;

    return { (n_pp / pp_ms) * 1000.0, (n_tg / tg_ms) * 1000.0 };
}
