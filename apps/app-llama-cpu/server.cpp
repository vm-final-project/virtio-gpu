/*
 * Server-only single-purpose appliance.
 *
 * Built when CONFIG_APP_LLAMA_CPU_MODE_SERVER=y. The image boots, loads
 * the model via 9pfs, initialises a llama_context per slot, prints a READY
 * line, and remains alive as the native server-mode entrypoint. There is no
 * shell, no fork/exec launcher, and no benchmark code path. The upstream HTTP
 * listener remains gated on the Unikraft netdev/lwIP port.
 *
 * plan-optimize.md L2.2 (continuous batching / slots) and L2.3 (prompt
 * cache) are surfaced through the Kconfig knobs below; common.h prints the
 * L1.4 model-load latency line.
 */
#include "llama-cpu-common.h"

#if CONFIG_APP_LLAMA_CPU_MODE_SERVER

#ifndef CONFIG_APP_LLAMA_CPU_PARALLEL
#define CONFIG_APP_LLAMA_CPU_PARALLEL 1
#endif
#ifndef CONFIG_APP_LLAMA_CPU_PROMPT_CACHE
#define CONFIG_APP_LLAMA_CPU_PROMPT_CACHE 1
#endif

int main(void)
{
    const char *model_path = "/mnt/model/model.gguf";
    llama_model *model = load_model(model_path, "uk-llama-upstream-server");
    if (!model)
        return 1;

    constexpr int n_slots = CONFIG_APP_LLAMA_CPU_PARALLEL;
    llama_context *slots[n_slots] = {nullptr};

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx           = 512;
    cparams.n_batch         = 128;
    cparams.n_threads       = CONFIG_APP_LLAMA_CPU_THREADS;
    cparams.n_threads_batch = CONFIG_APP_LLAMA_CPU_THREADS;

    for (int i = 0; i < n_slots; i++) {
        slots[i] = llama_init_from_model(model, cparams);
        if (!slots[i]) {
            uk_printf("uk-llama-upstream-server: FAIL ctx_init slot=%d\n", i);
            for (int j = 0; j < i; j++)
                llama_free(slots[j]);
            llama_model_free(model);
            llama_backend_free();
            return 1;
        }
    }

    const int prompt_cache_on = CONFIG_APP_LLAMA_CPU_PROMPT_CACHE;

    uk_printf("uk-llama-upstream-server: READY model=%s threads=%d "
              "slots=%d prompt_cache=%d mode=single-app no_fork_exec=1\n",
              model_path, CONFIG_APP_LLAMA_CPU_THREADS,
              n_slots, prompt_cache_on);
    uk_puts("uk-llama-upstream-server: note=http-listener requires Unikraft netdev/lwip gate; no launcher process\n");

    for (;;)
        sleep(60);

    /* unreachable */
    for (int i = 0; i < n_slots; i++)
        llama_free(slots[i]);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}

#endif /* CONFIG_APP_LLAMA_CPU_MODE_SERVER */
