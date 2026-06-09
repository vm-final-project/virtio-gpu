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

#include <vector>

int main(void)
{
    const char *model_path = "/mnt/model/model.gguf";
    llama_model *model = load_model(model_path, "uk-llama-upstream");
    if (!model)
        return 1;

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 512 + 128;
    cparams.n_batch = 512;
    cparams.n_threads = CONFIG_APP_LLAMA_CPU_THREADS;
    cparams.n_threads_batch = CONFIG_APP_LLAMA_CPU_THREADS;

    llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        uk_puts("uk-llama-upstream: FAIL ctx_init failed\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    const int n_pp = 512;
    std::vector<llama_token> tokens(n_pp, 0);
    llama_batch batch = llama_batch_get_one(tokens.data(), n_pp);

    double t0 = now_sec();
    llama_decode(ctx, batch);
    double pp_ms = (now_sec() - t0) * 1000.0;
    double pp512 = (n_pp / pp_ms) * 1000.0;

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
    double tg128 = (n_tg / tg_ms) * 1000.0;

    uk_printf("uk-llama-upstream: pp512=%.1f tg128=%.1f\n", pp512, tg128);
    uk_puts("uk-llama-upstream: PASS evidence_id=llama-upstream-cpu\n");

    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}

#endif /* CONFIG_APP_LLAMA_CPU_MODE_BENCH */
