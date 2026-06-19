/*
 * Native llama.cpp server appliance.
 * VM boot -> Unikraft -> main() -> llama_server_main() -> upstream llama_server().
 * This appliance is local-model only: the guest always mounts /mnt/model and
 * serves /mnt/model/model.gguf. There is no Hugging Face or remote-URL
 * download path in this build.
 */
#include "llama-vk-common.h"

#if CONFIG_APP_LLAMA_VK_MODE_SERVER

#ifndef CONFIG_APP_LLAMA_VK_PARALLEL
#define CONFIG_APP_LLAMA_VK_PARALLEL 1
#endif
#ifndef CONFIG_APP_LLAMA_VK_CTX
#define CONFIG_APP_LLAMA_VK_CTX 512
#endif
#ifndef CONFIG_APP_LLAMA_VK_PROMPT_CACHE
#define CONFIG_APP_LLAMA_VK_PROMPT_CACHE 1
#endif
#ifndef CONFIG_APP_LLAMA_VK_BATCH
#define CONFIG_APP_LLAMA_VK_BATCH 2048
#endif
#ifndef CONFIG_APP_LLAMA_VK_UBATCH
#define CONFIG_APP_LLAMA_VK_UBATCH 512
#endif

#define UK_LLAMA_STR_(x) #x
#define UK_LLAMA_STR(x)  UK_LLAMA_STR_(x)

int llama_server(int argc, char ** argv);

/* VOGUE per-phase profiling report, defined in libukvirtio_gpu. */
extern "C" void vogue_prof_report(void);

static int llama_server_main(void)
{
    /* Same ggml-vulkan env levers as the bench appliance: force the
     * synchronous upload path and pure device-local model buffers so weights
     * land on the real V100 over Venus instead of the bounded host-visible
     * VirtIO-GPU window. (See bench.cpp for the rationale.) */
    setenv("GGML_VK_DISABLE_ASYNC", "1", 1);
    setenv("GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM", "1", 1);
    setenv("UK_GGML_VK_DISPATCH_BATCH", "1", 1);

    /* Keep each command buffer's Venus stream in ONE SUBMIT_3D. The default
     * batch flush (~64 KB) splits a large graph — the first prompt-processing
     * forward pass — across several SUBMIT_3Ds, and on the NVIDIA Venus host the
     * resulting split command buffer fails the following vkQueueSubmit with a CS
     * error that tears the context down mid-request. Raising the flush ceiling
     * keeps the whole graph in one command-stream unit. (Bench-only images keep
     * the 64 KB default.) */
    setenv("UK_GGML_VK_BATCH_KB", "1024", 1);

    /* Correctness-critical for the server (NOT set by the throughput bench):
     * make vkWaitForFences a real reply-bearing Venus round-trip so the guest
     * blocks until the host GPU has actually signalled the fence before reading
     * results back. Without it the legacy virtio-gpu fence retires at SUBMIT_3D
     * decode time (submission, not completion), so ggml's logits readback
     * (vkQueueSubmit(copy,fence) -> vkWaitForFences -> memcpy(out,staging))
     * copies stale/zero bytes and the model emits empty/garbage text — which
     * llama-bench never catches because it does not validate output. The bench
     * image leaves this off and keeps its submission-rate throughput. */
    setenv("UK_GGML_VK_GPU_SYNC", "1", 1);


    /* Readiness-probe load over the REAL virtio-gpu-gl Venus path. Besides
     * proving the Venus model load works before READY, this warms up shared
     * ggml-vulkan singleton state (device, memory types, pipeline cache) that
     * the upstream server's own load below needs — without it the server's
     * llama_model_load_from_file fails outright (verified). */
    llama_model *probe = load_model_vk("/mnt/model/model.gguf",
                                       "uk-llama-upstream-vk-server");
    if (!probe) {
        uk_puts("uk-llama-upstream-vk-server: FAIL model_load (Venus)\n");
        return 1;
    }
    llama_model_free(probe);

    struct uk_vulkan_info info;
    uk_vulkan_get_info(&info);
    uk_printf("uk-llama-upstream-vk-server: READY model=/mnt/model/model.gguf threads=%d backend=vulkan "
              "slots=%d ctx_per_slot=%d batch_size=%d ubatch_size=%d prompt_cache=%d "
              "batch_enabled=%d ring_enabled=%d hostmem_fixed=%d mode=single-app no_fork_exec=1\n",
              CONFIG_APP_LLAMA_VK_THREADS,
              CONFIG_APP_LLAMA_VK_PARALLEL,
              CONFIG_APP_LLAMA_VK_CTX,
              CONFIG_APP_LLAMA_VK_BATCH,
              CONFIG_APP_LLAMA_VK_UBATCH,
              CONFIG_APP_LLAMA_VK_PROMPT_CACHE,
              info.batch_enabled,
              info.ring_enabled,
              info.hostmem_fixed);

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
             CONFIG_APP_LLAMA_VK_PARALLEL * CONFIG_APP_LLAMA_VK_CTX);
    static char batch_f[]    = "--batch-size";
    static char batch[]      = UK_LLAMA_STR(CONFIG_APP_LLAMA_VK_BATCH);
    static char ubatch_f[]   = "--ubatch-size";
    static char ubatch[]     = UK_LLAMA_STR(CONFIG_APP_LLAMA_VK_UBATCH);
    static char parallel_f[] = "--parallel";
    static char parallel[]   = UK_LLAMA_STR(CONFIG_APP_LLAMA_VK_PARALLEL);
    static char threads_f[]  = "--threads";
    static char threads[]    = UK_LLAMA_STR(CONFIG_APP_LLAMA_VK_THREADS);
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
    /* Keep all weights in device-local Vulkan0 memory: the default pinned
     * Vulkan_Host buffer is a host-visible VirtIO-GPU blob whose upload does not
     * complete on a software host Vulkan driver over Venus (deadlocks load).
     * Must match the readiness-probe load (load_model_common sets no_host). */
    static char nohost[]     = "--no-host";
    /* Disable llama.cpp's automatic "fit params to device memory" pass. With
     * n_gpu_layers=99 + --no-host the placement is already fully specified, and
     * the auto-fit pass runs an extra device-memory measurement/graph-reserve
     * over Venus that stalls the load on the A30 (the upstream log itself
     * suggests "-fit off" when this step misbehaves). */
    static char fit_f[]      = "-fit";
    static char fit[]        = "off";
#if CONFIG_APP_LLAMA_VK_PROMPT_CACHE
    static char cache[]      = "--cache-prompt";
    char *argv[] = {arg0, model_f, model, host_f, host, port_f, port,
                    ctx_f, ctx, batch_f, batch, ubatch_f, ubatch,
                    parallel_f, parallel, threads_f, threads,
                    ngl_f, ngl, nommap, fa_f, fa, nohost, fit_f, fit, cache};
#else
    char *argv[] = {arg0, model_f, model, host_f, host, port_f, port,
                    ctx_f, ctx, batch_f, batch, ubatch_f, ubatch,
                    parallel_f, parallel, threads_f, threads,
                    ngl_f, ngl, nommap, fa_f, fa, nohost, fit_f, fit};
#endif

    int rc = llama_server((int)(sizeof(argv) / sizeof(argv[0])), argv);
    /* Only reached on a graceful server exit (not the harness SIGTERM path). */
#if defined(CONFIG_LIBUKVIRTIO_GPU_PROFILING) && CONFIG_LIBUKVIRTIO_GPU_PROFILING
    vogue_prof_report();
#endif
    return rc;
}

int main(void)
{
    return llama_server_main();
}

#endif /* CONFIG_APP_LLAMA_VK_MODE_SERVER */
