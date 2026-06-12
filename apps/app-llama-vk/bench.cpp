/*
 * Vulkan bench-only single-purpose appliance.
 *
 * Built when CONFIG_APP_LLAMA_VK_MODE_BENCH=y. The image boots,
 * mounts /mnt/model via 9pfs, wires the static Venus-backed Vulkan dispatch,
 * runs pp512 + tg128 with -ngl 99, prints the evidence line, and exits.
 *
 * Output parsed by make llama-vk-run:
 *   uk-llama-upstream-vk: pp512=<f> tg128=<f>
 *   uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk
 */
#include "llama-vk-common.h"

#if CONFIG_APP_LLAMA_VK_MODE_BENCH

#include <cstdlib>
#include <cstring>

int main(int argc, char *argv[])
{
    setenv("GGML_VK_DISABLE_ASYNC", "1", 1);
    setenv("GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM", "1", 1);
    setenv("UK_GGML_VK_DISPATCH_BATCH", "1", 1);

    /* Profiling toggle (docs/plan-profile.md Phase 2): the runner appends
     * "ggml-vk-perf-logger[=freq]" to the kernel cmdline app args; scan the
     * whole argv so it works with or without a "--" separator. */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "ggml-vk-perf-logger", 19) != 0)
            continue;
        setenv("GGML_VK_PERF_LOGGER", "1", 1);
        if (argv[i][19] == '=' && argv[i][20])
            setenv("GGML_VK_PERF_LOGGER_FREQUENCY", argv[i] + 20, 1);
        uk_puts("uk-llama-upstream-vk: GGML_VK_PERF_LOGGER enabled\n");
    }

    const char *model_path = "/mnt/model/model.gguf";
    llama_model *model = load_model_vk(model_path, "uk-llama-upstream-vk");
    if (!model)
        return 1;

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx            = 512 + 128;
    cparams.n_batch          = 512;
    cparams.n_ubatch         = 512;
    cparams.n_threads        = CONFIG_APP_LLAMA_VK_THREADS;
    cparams.n_threads_batch  = CONFIG_APP_LLAMA_VK_THREADS;

    llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        uk_puts("uk-llama-upstream-vk: FAIL ctx_init failed\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    bench_result r = run_bench_loop(ctx);

    struct uk_vulkan_info info;
    uk_vulkan_get_info(&info);
    uk_printf("uk-llama-upstream-vk: config threads=%d n_ctx=%d n_batch=%d n_ubatch=%d "
              "batch_enabled=%d ring_enabled=%d hostmem_fixed=%d\n",
              CONFIG_APP_LLAMA_VK_THREADS,
              cparams.n_ctx, cparams.n_batch, cparams.n_ubatch,
              info.batch_enabled, info.ring_enabled, info.hostmem_fixed);
    uk_printf("uk-llama-upstream-vk: pp512=%.1f tg128=%.1f\n", r.pp512, r.tg128);
    uk_puts("uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk\n");

    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}

#endif /* CONFIG_APP_LLAMA_VK_MODE_BENCH */
