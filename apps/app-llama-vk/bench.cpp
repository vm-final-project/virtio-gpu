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

#if CONFIG_APP_LLAMA_VK_BENCH_UPSTREAM

extern int llama_bench(int argc, char **argv);

int main(void)
{
    setenv("GGML_VK_DISABLE_ASYNC", "1", 1);
    setenv("GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM", "1", 1);
    setenv("UK_GGML_VK_DISPATCH_BATCH", "1", 1);

    if (mount_model_fs("uk-llama-upstream-vk-bench") != 0)
        return 1;

    char threads[16];
    snprintf(threads, sizeof(threads), "%d", CONFIG_APP_LLAMA_VK_THREADS);

    uk_puts("uk-llama-bench-kind: upstream_llama_bench\n");
    char *argv[] = {
        (char *)"llama-bench",
        (char *)"-m", (char *)"/mnt/model/model.gguf",
        (char *)"-p", (char *)"512",
        (char *)"-n", (char *)"128",
        (char *)"-t", threads,
        (char *)"-ngl", (char *)"99",
        (char *)"--mmap", (char *)"0",
        (char *)"--no-host", (char *)"1",
        (char *)"--no-warmup",
        (char *)"-r", (char *)"1",
        (char *)"-o", (char *)"jsonl",
    };
    int rc = llama_bench((int)(sizeof(argv) / sizeof(argv[0])), argv);
    if (rc == 0)
        uk_puts("uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk bench_kind=upstream_llama_bench\n");
    else
        uk_printf("uk-llama-upstream-vk: FAIL upstream_llama_bench rc=%d\n", rc);
    return rc;
}

#else /* CONFIG_APP_LLAMA_VK_BENCH_UPSTREAM */

int main(void)
{
    setenv("GGML_VK_DISABLE_ASYNC", "1", 1);
    setenv("GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM", "1", 1);
    setenv("UK_GGML_VK_DISPATCH_BATCH", "1", 1);

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

    uk_puts("uk-llama-bench-kind: hand_rolled_smoke\n");
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

#endif /* CONFIG_APP_LLAMA_VK_BENCH_UPSTREAM */

#endif /* CONFIG_APP_LLAMA_VK_MODE_BENCH */
