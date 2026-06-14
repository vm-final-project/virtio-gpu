/*
 * CPU-appliance helpers. Delegates shared utilities (uk_puts, uk_printf,
 * now_sec, load_model_common, run_bench_loop) to llama-uk-common.h and
 * provides the CPU-specific load_model() wrapper (n_gpu_layers=0).
 */
#pragma once

#include <uk/config.h>
#include <uk/schedcoop.h>   /* uk_schedcoop_smp_online_count() */

#include "llama-uk-common.h"

#ifndef CONFIG_APP_LLAMA_CPU_THREADS
#define CONFIG_APP_LLAMA_CPU_THREADS 1
#endif

#define UK_LLAMA_CPU_WORKER_THREADS CONFIG_APP_LLAMA_CPU_THREADS

/* Number of vCPUs actually online at runtime. One ggml worker per vCPU is the
 * hard ceiling: polling workers > online vCPUs deadlock the cooperative
 * scheduler (spinning workers never yield). */
static inline unsigned int uk_llama_cpu_online_vcpus(void)
{
    unsigned int n = uk_schedcoop_smp_online_count();
    return n ? n : 1;
}

static inline llama_model *load_model(const char *model_path, const char *tag)
{
    return load_model_common(model_path, tag, 0);
}
