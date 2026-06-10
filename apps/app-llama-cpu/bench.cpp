/*
 * Bench-only single-purpose appliance.
 *
 * Built when CONFIG_APP_LLAMA_CPU_MODE_BENCH=y. The image boots, mounts
 * /mnt/model via 9pfs, runs pp512 + tg128, prints the evidence line, and
 * exits. No HTTP, no shell, no second mode in the binary.
 *
 * Output parsed by make llama-cpu-run:
 *   uk-llama-upstream: pp512=<f> tg128=<f>
 *   uk-llama-upstream: PASS evidence_id=llama-upstream-cpu
 */
#include "llama-cpu-common.h"

#if CONFIG_APP_LLAMA_CPU_MODE_BENCH

int main(void)
{
    const char *model_path = "/mnt/model/model.gguf";
    llama_model *model = load_model(model_path, "uk-llama-upstream");
    if (!model)
        return 1;

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx            = 512 + 128;
    cparams.n_batch          = 512;
    cparams.n_threads        = CONFIG_APP_LLAMA_CPU_THREADS;
    cparams.n_threads_batch  = CONFIG_APP_LLAMA_CPU_THREADS;

    llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        uk_puts("uk-llama-upstream: FAIL ctx_init failed\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    bench_result r = run_bench_loop(ctx);
    uk_printf("uk-llama-upstream: pp512=%.1f tg128=%.1f\n", r.pp512, r.tg128);
    uk_puts("uk-llama-upstream: PASS evidence_id=llama-upstream-cpu\n");

    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}

#endif /* CONFIG_APP_LLAMA_CPU_MODE_BENCH */
