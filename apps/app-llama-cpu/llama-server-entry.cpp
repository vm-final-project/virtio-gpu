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
 *
 * A3/SMP: thread count and threadpool policy are set at runtime to match
 * bench.cpp: one worker per online vCPU, polling threadpool, strict CPU
 * placement, one-hot hex cpumask.
 */
#include "llama-cpu-common.h"
#include <stdio.h>

#if CONFIG_APP_LLAMA_CPU_MODE_SERVER

#ifndef CONFIG_APP_LLAMA_CPU_PARALLEL
#define CONFIG_APP_LLAMA_CPU_PARALLEL 1
#endif
#ifndef CONFIG_APP_LLAMA_CPU_CTX
#define CONFIG_APP_LLAMA_CPU_CTX 512
#endif
#ifndef CONFIG_APP_LLAMA_CPU_PROMPT_CACHE
#define CONFIG_APP_LLAMA_CPU_PROMPT_CACHE 1
#endif
#ifndef CONFIG_APP_LLAMA_CPU_BATCH
#define CONFIG_APP_LLAMA_CPU_BATCH 2048
#endif
#ifndef CONFIG_APP_LLAMA_CPU_UBATCH
#define CONFIG_APP_LLAMA_CPU_UBATCH 512
#endif

#define UK_LLAMA_STR_(x) #x
#define UK_LLAMA_STR(x)  UK_LLAMA_STR_(x)

int llama_server(int argc, char ** argv);

static int llama_server_main(void)
{
    const char *model_path = "/mnt/model/model.gguf";
    llama_model *probe = load_model(model_path, "uk-llama-upstream-server");
    if (!probe) {
        uk_puts("uk-llama-upstream-server: FAIL model_load\n");
        return 1;
    }

    /* A3/SMP: resolve online vCPU count at runtime (mirrors bench.cpp). */
    const unsigned int nvcpu = uk_llama_cpu_online_vcpus();

    uk_printf("uk-llama-upstream-server: READY model=%s threads=%u "
              "slots=%d ctx_per_slot=%d batch_size=%d ubatch_size=%d prompt_cache=%d "
              "mode=single-app no_fork_exec=1\n",
              model_path,
              nvcpu,
              CONFIG_APP_LLAMA_CPU_PARALLEL,
              CONFIG_APP_LLAMA_CPU_CTX,
              CONFIG_APP_LLAMA_CPU_BATCH,
              CONFIG_APP_LLAMA_CPU_UBATCH,
              CONFIG_APP_LLAMA_CPU_PROMPT_CACHE);

    /* Release the readiness-probe model; the upstream server below reloads it
     * through its own model manager. */
    llama_model_free(probe);

    static char arg0[]       = "llama-server";
    static char model_f[]    = "-m";
    static char model[]      = "/mnt/model/model.gguf";
    static char host_f[]     = "--host";
    static char host[]       = "0.0.0.0";
    static char port_f[]     = "--port";
    static char port[]       = "8080";
    static char ctx_f[]      = "--ctx-size";
    static char ctx[16];
    snprintf(ctx, sizeof(ctx), "%d",
             CONFIG_APP_LLAMA_CPU_PARALLEL * CONFIG_APP_LLAMA_CPU_CTX);
    static char batch_f[]    = "--batch-size";
    static char batch[]      = UK_LLAMA_STR(CONFIG_APP_LLAMA_CPU_BATCH);
    static char ubatch_f[]   = "--ubatch-size";
    static char ubatch[]     = UK_LLAMA_STR(CONFIG_APP_LLAMA_CPU_UBATCH);
    static char parallel_f[] = "--parallel";
    static char parallel[]   = UK_LLAMA_STR(CONFIG_APP_LLAMA_CPU_PARALLEL);
    static char threads_f[]  = "--threads";
    /* A3/SMP: runtime thread count — one worker per online vCPU. */
    static char threads[16];
    snprintf(threads, sizeof(threads), "%u", nvcpu);
    static char ngl_f[]      = "--n-gpu-layers";
    static char ngl[]        = "0";
    static char nommap[]     = "--no-mmap";
    static char fa_f[]       = "--flash-attn";
    static char fa[]         = "off";
    /* -fit probes device memory via speculative large allocations; on QEMU/HVF
     * those accesses trigger data aborts with ISV=0 that the hypervisor cannot
     * decode, causing an assertion failure in hvf_handle_exception. Disable. */
    static char nofit[]      = "-fit";
    static char nofit_val[]  = "off";
    /* A3/SMP: polling threadpool + strict CPU placement via upstream CLI args.
     * --cpu-mask is a hex bitmask: set one bit per online vCPU (one-hot per
     * worker).  --cpu-strict 1 pins each ggml worker to its vCPU.
     * --poll 100 keeps workers spinning (no condvar sleep between barriers),
     * matching the ggml_threadpool_params used in bench.cpp. */
    static char cpumask_f[]  = "--cpu-mask";
    static char cpumask[32]; /* hex string, e.g. "f" for 4 vCPUs */
    {
        /* Build a hex mask: bit i set for i in [0, nvcpu). */
        unsigned long long mask = (nvcpu >= 64) ? ~0ULL : ((1ULL << nvcpu) - 1ULL);
        snprintf(cpumask, sizeof(cpumask), "%llx", mask);
    }
    static char strict_f[]   = "--cpu-strict";
    static char strict_v[]   = "1";
    static char poll_f[]     = "--poll";
    static char poll_v[]     = "100";
#if CONFIG_APP_LLAMA_CPU_PROMPT_CACHE
    static char cache[]      = "--cache-prompt";
    char *argv[] = {arg0, model_f, model, host_f, host, port_f, port,
                    ctx_f, ctx, batch_f, batch, ubatch_f, ubatch,
                    parallel_f, parallel, threads_f, threads,
                    ngl_f, ngl, nommap, fa_f, fa, nofit, nofit_val,
                    cpumask_f, cpumask, strict_f, strict_v, poll_f, poll_v,
                    cache};
#else
    char *argv[] = {arg0, model_f, model, host_f, host, port_f, port,
                    ctx_f, ctx, batch_f, batch, ubatch_f, ubatch,
                    parallel_f, parallel, threads_f, threads,
                    ngl_f, ngl, nommap, fa_f, fa, nofit, nofit_val,
                    cpumask_f, cpumask, strict_f, strict_v, poll_f, poll_v};
#endif

    return llama_server((int)(sizeof(argv) / sizeof(argv[0])), argv);
}

int main(void)
{
    return llama_server_main();
}

#endif /* CONFIG_APP_LLAMA_CPU_MODE_SERVER */
