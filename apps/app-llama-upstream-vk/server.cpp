/*
 * Native llama.cpp server appliance.
 * VM boot -> Unikraft -> main() -> llama_server_main() -> upstream llama_server().
 */
#include "common.h"

#if CONFIG_APP_LLAMA_UPSTREAM_VK_MODE_SERVER

#ifndef CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL
#define CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL 1
#endif
#ifndef CONFIG_APP_LLAMA_UPSTREAM_VK_CTX
#define CONFIG_APP_LLAMA_UPSTREAM_VK_CTX 512
#endif
#ifndef CONFIG_APP_LLAMA_UPSTREAM_VK_PROMPT_CACHE
#define CONFIG_APP_LLAMA_UPSTREAM_VK_PROMPT_CACHE 1
#endif
#ifndef CONFIG_APP_LLAMA_UPSTREAM_VK_BATCH
#define CONFIG_APP_LLAMA_UPSTREAM_VK_BATCH 2048
#endif
#ifndef CONFIG_APP_LLAMA_UPSTREAM_VK_UBATCH
#define CONFIG_APP_LLAMA_UPSTREAM_VK_UBATCH 512
#endif

#define UK_LLAMA_STR_(x) #x
#define UK_LLAMA_STR(x)  UK_LLAMA_STR_(x)

int llama_server(int argc, char ** argv);

static int llama_server_main(void)
{
    /* Same ggml-vulkan env levers as the bench appliance: force the
     * synchronous upload path and pure device-local model buffers so weights
     * land on the real V100 over Venus instead of the bounded host-visible
     * VirtIO-GPU window. (See bench.cpp for the rationale.) */
    setenv("GGML_VK_DISABLE_ASYNC", "1", 1);
    setenv("GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM", "1", 1);
    setenv("UK_GGML_VK_DISPATCH_BATCH", "1", 1);

    /* Prove model-loaded readiness over the REAL virtio-gpu-gl Venus path
     * before signalling READY: load_model_vk() mounts the GGUF over 9pfs,
     * initialises the Venus dispatch chain (libukggml_vk -> libukvenus
     * SUBMIT_3D), and loads all layers with n_gpu_layers=99 onto the host GPU.
     * This makes the READY line below an honest model-loaded-readiness signal,
     * not just a dispatch-init marker. */
    llama_model *probe = load_model_vk("/mnt/model/model.gguf",
                                       "uk-llama-upstream-vk-server");
    if (!probe) {
        uk_puts("uk-llama-upstream-vk-server: FAIL model_load (Venus)\n");
        return 1;
    }

    struct uk_ggml_vulkan_dispatch_info info;
    uk_ggml_vulkan_dispatch_get_info(&info);
    uk_printf("uk-llama-upstream-vk-server: READY model=/mnt/model/model.gguf threads=%d backend=vulkan "
              "slots=%d ctx_per_slot=%d batch_size=%d ubatch_size=%d prompt_cache=%d "
              "batch_enabled=%d hostmem_fixed=%d mode=single-app no_fork_exec=1\n",
              CONFIG_APP_LLAMA_UPSTREAM_VK_THREADS,
              CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL,
              CONFIG_APP_LLAMA_UPSTREAM_VK_CTX,
              CONFIG_APP_LLAMA_UPSTREAM_VK_BATCH,
              CONFIG_APP_LLAMA_UPSTREAM_VK_UBATCH,
              CONFIG_APP_LLAMA_UPSTREAM_VK_PROMPT_CACHE,
              info.batch_enabled,
              info.hostmem_fixed);

    /* Release the readiness-probe model; the upstream server below reloads it
     * through its own model manager. (Frees the ~GPU buffers so the reload
     * does not double-reserve V100 memory.) */
    llama_model_free(probe);

    static char arg0[]       = "llama-server";
    static char model_f[]    = "-m";
    static char model[]      = "/mnt/model/model.gguf";
    static char host_f[]     = "--host";
    static char host[]       = "0.0.0.0";
    static char port_f[]     = "--port";
    static char port[]       = "8080";
    static char ctx_f[]      = "--ctx-size";
    /* CONFIG_..._CTX is the per-slot window (see Config.uk). llama-server's
     * --ctx-size is the TOTAL KV budget split across --parallel slots, so the
     * total must be PARALLEL*CTX for each slot to get the configured window. */
    static char ctx[16];
    snprintf(ctx, sizeof(ctx), "%d",
             CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL * CONFIG_APP_LLAMA_UPSTREAM_VK_CTX);
    static char batch_f[]    = "--batch-size";
    static char batch[]      = UK_LLAMA_STR(CONFIG_APP_LLAMA_UPSTREAM_VK_BATCH);
    static char ubatch_f[]   = "--ubatch-size";
    static char ubatch[]     = UK_LLAMA_STR(CONFIG_APP_LLAMA_UPSTREAM_VK_UBATCH);
    static char parallel_f[] = "--parallel";
    static char parallel[]   = UK_LLAMA_STR(CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL);
    static char threads_f[]  = "--threads";
    static char threads[]    = UK_LLAMA_STR(CONFIG_APP_LLAMA_UPSTREAM_VK_THREADS);
    static char ngl_f[]      = "--n-gpu-layers";
    static char ngl[]        = "99";
    /* The GGUF is mounted over 9pfs, which does not support file-backed mmap in
     * the guest (mmap returns "Bad address"); force the read()-based loader, the
     * same path the readiness probe above used with use_mmap=0. */
    static char nommap[]     = "--no-mmap";
    /* Flash attention is forced OFF: the V100 (Volta) Vulkan path has no
     * coopmat2, so FA either falls back or regresses Vulkan throughput ~50%
     * (llama.cpp issue #9572). Deterministic off beats the AUTO default. */
    static char fa_f[]       = "--flash-attn";
    static char fa[]         = "off";
#if CONFIG_APP_LLAMA_UPSTREAM_VK_PROMPT_CACHE
    static char cache[]      = "--cache-prompt";
    char *argv[] = {arg0, model_f, model, host_f, host, port_f, port,
                    ctx_f, ctx, batch_f, batch, ubatch_f, ubatch,
                    parallel_f, parallel, threads_f, threads,
                    ngl_f, ngl, nommap, fa_f, fa, cache};
#else
    char *argv[] = {arg0, model_f, model, host_f, host, port_f, port,
                    ctx_f, ctx, batch_f, batch, ubatch_f, ubatch,
                    parallel_f, parallel, threads_f, threads,
                    ngl_f, ngl, nommap, fa_f, fa};
#endif

    return llama_server((int)(sizeof(argv) / sizeof(argv[0])), argv);
}

int main(void)
{
    return llama_server_main();
}

#endif /* CONFIG_APP_LLAMA_UPSTREAM_VK_MODE_SERVER */
