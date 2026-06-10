/*
 * CPU-appliance helpers. Delegates shared utilities (uk_puts, uk_printf,
 * now_sec, load_model_common, run_bench_loop) to llama-uk-common.h and
 * provides the CPU-specific load_model() wrapper (n_gpu_layers=0).
 */
#pragma once

#include "llama-uk-common.h"

#ifndef CONFIG_APP_LLAMA_CPU_THREADS
#define CONFIG_APP_LLAMA_CPU_THREADS 1
#endif

static inline llama_model *load_model(const char *model_path, const char *tag)
{
    return load_model_common(model_path, tag, 0);
}
