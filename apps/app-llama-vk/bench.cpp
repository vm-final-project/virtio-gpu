/*
 * Vulkan bench-only single-purpose appliance.
 *
 * Built when CONFIG_APP_LLAMA_VK_MODE_BENCH=y. The image boots, mounts
 * /mnt/model via 9pfs, wires the static Venus-backed Vulkan dispatch, then
 * invokes the upstream llama-bench tool (tools/llama-bench/llama-bench.cpp,
 * entry point llama_bench()) with the SAME workload the non-Unikraft
 * vogue-baselines runtimes run:
 *
 *     llama-bench -m /mnt/model/model.gguf -ngl 99 -p 512 -n 128 --no-host 1
 *
 * Using upstream llama-bench (untimed warmup run + repetitions + per-run
 * device synchronization + averaging with stddev) makes VOGUE's pp512/tg128
 * directly comparable with the baselines, which all run this exact tool. It
 * replaces the previous hand-rolled run_bench_loop(), whose single-shot,
 * no-warmup timing produced an implausible tg128 (≈3× the baremetal Vulkan
 * upper bound on the same GPU).
 *
 * --no-host 1 keeps model weights in device-local Vulkan0 memory: the VOGUE
 * Venus path documented as required in PORTING.md (the default host-visible
 * blob upload otherwise stalls model load just before READY / needs a far
 * larger hostmem window). Device-local weights also match how a discrete-GPU
 * baremetal llama-bench run places them.
 *
 * Output: llama-bench's markdown table (test column rows "pp512"/"tg128"),
 * parsed by scripts/app-llama-vk.py, followed by the PASS evidence line.
 */
#include "llama-vk-common.h"

#if CONFIG_APP_LLAMA_VK_MODE_BENCH

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <sys/stat.h>
#include <sys/mount.h>

// Upstream entry point (tools/llama-bench/llama-bench.cpp). Its main.cpp is not
// compiled into the appliance; the single-app image calls llama_bench() here.
extern int llama_bench(int argc, char ** argv);

// VOGUE per-phase Venus-path profiling (libukvirtio_gpu). Upstream llama-bench
// owns its own load/warmup/measure loop with no hook we can use to split
// prompt vs decode, so we reset before and report after the whole run: the
// counters aggregate into the "prompt" phase ("decode" stays empty). The
// active-vs-wait / encode / flush / L2-L4 breakdown is still meaningful as a
// whole-run total.
extern "C" void vogue_prof_reset(void);
extern "C" void vogue_prof_report(void);

#define VK_STR2(x) #x
#define VK_STR(x)  VK_STR2(x)

int main(void)
{
    // Venus path tuning (unchanged from the previous bench entry): force
    // synchronous dispatch + device-local weights so timing reflects real GPU
    // completion rather than queued submissions.
    setenv("GGML_VK_DISABLE_ASYNC", "1", 1);
    setenv("GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM", "1", 1);
    setenv("UK_GGML_VK_DISPATCH_BATCH", "1", 1);

    // Wire the static Venus-backed Vulkan dispatch before llama-bench
    // enumerates Vulkan devices (load_model_vk() used to do this internally).
    if (uk_vulkan_init() != 0) {
        uk_puts("uk-llama-upstream-vk: FAIL dispatch_init failed\n");
        return 1;
    }

    // Mount the host-provided GGUF over 9pfs at /mnt/model. load_model_vk() used
    // to do this inside load_model_common(); llama-bench loads the model itself,
    // so the mount has to happen here or it sees no /mnt/model/model.gguf.
    mkdir("/mnt", 0755);
    mkdir("/mnt/model", 0755);
    if (mount("model", "/mnt/model", "9pfs", 0, "") != 0) {
        uk_printf("uk-llama-upstream-vk: FAIL 9pfs mount failed errno=%d\n", errno);
        return 1;
    }

    struct uk_vulkan_info info;
    uk_vulkan_get_info(&info);
    uk_printf("uk-llama-upstream-vk: config threads=%s batch_enabled=%d "
              "ring_enabled=%d hostmem_fixed=%d\n",
              VK_STR(CONFIG_APP_LLAMA_VK_THREADS),
              info.batch_enabled, info.ring_enabled, info.hostmem_fixed);

    // --mmap 0: the model lives on 9pfs and mmap is unsupported here, so load
    //   it with read() (matches the previous load_model_common use_mmap=false).
    //   mmap only affects loading, not the pp512/tg128 throughput numbers.
    // --no-host 1: keep weights device-local (see file header).
    char arg0[]  = "llama-bench";
    char a_m[]   = "-m";        char a_model[] = "/mnt/model/model.gguf";
    char a_ngl[] = "-ngl";      char a_99[]    = "99";
    char a_p[]   = "-p";        char a_512[]   = "512";
    char a_n[]   = "-n";        char a_128[]   = "128";
    char a_t[]   = "-t";        char a_thr[]   = VK_STR(CONFIG_APP_LLAMA_VK_THREADS);
    char a_nh[]  = "--no-host"; char a_one[]   = "1";
    char a_mm[]  = "--mmap";    char a_zero[]  = "0";
    char *argv[] = { arg0, a_m, a_model, a_ngl, a_99, a_p, a_512,
                     a_n, a_128, a_t, a_thr, a_nh, a_one, a_mm, a_zero, nullptr };
    int argc = (int) (sizeof(argv) / sizeof(argv[0])) - 1;

    /* Wall clock over the exact span the profiling covers, so the host harness
     * can derive the app (ggml/llama CPU + model-load I/O) share as
     * wall - (vulkan active + wait). */
    double _wall0 = now_sec();
    vogue_prof_reset();
    int rc = llama_bench(argc, argv);
    vogue_prof_report();
    uk_printf("VOGUE-TIMING wall[all]: total_ns=%llu\n",
              (unsigned long long)((now_sec() - _wall0) * 1e9));

    if (rc == 0)
        uk_puts("uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk\n");
    else
        uk_printf("uk-llama-upstream-vk: FAIL llama_bench rc=%d\n", rc);

    return rc;
}

#endif /* CONFIG_APP_LLAMA_VK_MODE_BENCH */
