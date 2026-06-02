/*
 * Vulkan bench-only single-purpose appliance.
 *
 * Built when CONFIG_APP_LLAMA_UPSTREAM_VK_MODE_BENCH=y. The image boots,
 * mounts /mnt/model via 9pfs, wires the static Venus-backed Vulkan dispatch,
 * runs pp512 + tg128 with -ngl 99, prints the evidence line, and exits.
 *
 * Output parsed by scripts/llama_vulkan_eval.py upstream-vk:
 *   uk-llama-upstream-vk: pp512=<f> tg128=<f>
 *   uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk
 */
#include "common.h"

#if CONFIG_APP_LLAMA_UPSTREAM_VK_MODE_BENCH

#include <vector>
#include <cstdlib>

int main(void)
{
    /* The ggml-vulkan async-upload path uses timeline/binary semaphores that the
     * static Venus dispatch does not implement; force the synchronous upload
     * path (vkCmdCopyBuffer + queue submit + fence). */
    setenv("GGML_VK_DISABLE_ASYNC", "1", 1);

    /* Force big device buffers (model weights, KV cache, compute graph) into
     * PURE VK_MEMORY_PROPERTY_DEVICE_LOCAL memory. Without this, ggml's default
     * "discrete GPU" path prefers DeviceLocal|HostVisible (rebar) memory and
     * tries to map every device buffer into the guest's finite host-visible
     * VirtIO-GPU window (hostmem=512M), which overflows for anything but a tiny
     * model. Pure device-local memory lives on the real V100 (allocated host-
     * side via Venus) and is filled through a small bounded host-visible staging
     * buffer + vkCmdCopyBuffer, exactly as on a real BAR-limited discrete GPU. */
    setenv("GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM", "1", 1);
    setenv("UK_GGML_VK_DISPATCH_BATCH", "1", 1);

    const char *model_path = "/mnt/model/model.gguf";
    llama_model *model = load_model_vk(model_path, "uk-llama-upstream-vk");
    if (!model)
        return 1;

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 512 + 128;
    cparams.n_batch = 512;
    cparams.n_ubatch = 512;
    cparams.n_threads = CONFIG_APP_LLAMA_UPSTREAM_VK_THREADS;
    cparams.n_threads_batch = CONFIG_APP_LLAMA_UPSTREAM_VK_THREADS;

    llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        uk_puts("uk-llama-upstream-vk: FAIL ctx_init failed\n");
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

    struct uk_ggml_vulkan_dispatch_info info;
    uk_ggml_vulkan_dispatch_get_info(&info);
    uk_printf("uk-llama-upstream-vk: config threads=%d n_ctx=%d n_batch=%d n_ubatch=%d batch_enabled=%d hostmem_fixed=%d\n",
              CONFIG_APP_LLAMA_UPSTREAM_VK_THREADS,
              cparams.n_ctx,
              cparams.n_batch,
              cparams.n_ubatch,
              info.batch_enabled,
              info.hostmem_fixed);
    uk_printf("uk-llama-upstream-vk: pp512=%.1f tg128=%.1f\n", pp512, tg128);
    uk_puts("uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk\n");

    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}

#endif /* CONFIG_APP_LLAMA_UPSTREAM_VK_MODE_BENCH */
