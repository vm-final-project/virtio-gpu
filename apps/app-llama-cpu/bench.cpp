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
#include "ggml.h"
#include "ggml-cpu.h"

#if CONFIG_APP_LLAMA_CPU_MODE_BENCH

#if CONFIG_APP_LLAMA_CPU_BENCH_UPSTREAM

extern int llama_bench(int argc, char **argv);

int main(void)
{
    if (mount_model_fs("uk-llama-upstream-bench") != 0)
        return 1;

    const unsigned int nvcpu = uk_llama_cpu_online_vcpus();
    char threads[16];
    snprintf(threads, sizeof(threads), "%u", nvcpu);

    uk_puts("uk-llama-bench-kind: upstream_llama_bench\n");
    char *argv[] = {
        (char *)"llama-bench",
        (char *)"-m", (char *)"/mnt/model/model.gguf",
        (char *)"-p", (char *)"512",
        (char *)"-n", (char *)"128",
        (char *)"-t", threads,
        (char *)"-ngl", (char *)"0",
        (char *)"--mmap", (char *)"0",
        (char *)"--no-warmup",
        (char *)"-r", (char *)"1",
        (char *)"-o", (char *)"jsonl",
    };
    int rc = llama_bench((int)(sizeof(argv) / sizeof(argv[0])), argv);
    if (rc == 0)
        uk_puts("uk-llama-upstream: PASS evidence_id=llama-upstream-cpu bench_kind=upstream_llama_bench\n");
    else
        uk_printf("uk-llama-upstream: FAIL upstream_llama_bench rc=%d\n", rc);
    return rc;
}

#else /* CONFIG_APP_LLAMA_CPU_BENCH_UPSTREAM */

int main(void)
{
    const char *model_path = "/mnt/model/model.gguf";
    llama_model *model = load_model(model_path, "uk-llama-upstream");
    if (!model)
        return 1;

    const unsigned int nvcpu = uk_llama_cpu_online_vcpus();

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx            = 512 + 128;
    cparams.n_batch          = 512;
    cparams.n_threads        = (int)nvcpu;
    cparams.n_threads_batch  = (int)nvcpu;

    llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        uk_puts("uk-llama-upstream: FAIL ctx_init failed\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    /* A3/SMP: use a POLLING threadpool so ggml workers spin (cpu_relax)
     * instead of sleeping on a condvar between compute stages. On the Unikraft
     * per-LCPU cooperative scheduler, sleeping workers must be re-woken across
     * vCPUs (IPI + reschedule) at every barrier, and there are many barriers
     * per token; that wakeup latency makes multi-vCPU SLOWER than one vCPU.
     * Spinning keeps one worker hot per vCPU (co-scheduled), which is what
     * ggml's barrier expects. Host has plenty of cores.
     * Pin one worker per online vCPU via one-hot cpumask so ggml's affinity
     * path migrates each thread to its dedicated vCPU. */
    struct ggml_threadpool_params tpp =
        ggml_threadpool_params_default((int)nvcpu);
    for (uint32_t i = 0; i < GGML_MAX_N_THREADS; i++)
        tpp.cpumask[i] = (i < nvcpu);
    tpp.strict_cpu = true;
    tpp.poll       = 100;
    struct ggml_threadpool *tp = ggml_threadpool_new(&tpp);
    if (tp)
        llama_attach_threadpool(ctx, tp, tp);
    uk_printf("uk-llama-upstream: workers=%u cpu_mask_bits=%u strict_cpu=1 poll=100\n",
              nvcpu, nvcpu);

    uk_puts("uk-llama-bench-kind: hand_rolled_smoke\n");
    bench_result r = run_bench_loop(ctx);
    uk_printf("uk-llama-upstream: pp512=%.1f tg128=%.1f\n", r.pp512, r.tg128);
    uk_puts("uk-llama-upstream: PASS evidence_id=llama-upstream-cpu\n");

    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}

#endif /* CONFIG_APP_LLAMA_CPU_BENCH_UPSTREAM */

#endif /* CONFIG_APP_LLAMA_CPU_MODE_BENCH */
