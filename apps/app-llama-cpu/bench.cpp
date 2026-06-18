/*
 * CPU bench-only single-purpose appliance.
 *
 * Built when CONFIG_APP_LLAMA_CPU_MODE_BENCH=y. The image boots, mounts
 * /mnt/model via 9pfs, then invokes the upstream llama-bench tool
 * (tools/llama-bench/llama-bench.cpp, entry point llama_bench()) with the SAME
 * workload the non-Unikraft vogue-baselines runtimes run, but CPU-only:
 *
 *     llama-bench -m /mnt/model/model.gguf -ngl 0 -p 512 -n 128 -t <threads>
 *
 * This is the CPU-path counterpart of app-llama-vk/bench.cpp (which runs the
 * identical tool at -ngl 99). Using upstream llama-bench (untimed warmup run +
 * repetitions + averaging with stddev) makes the CPU pp512/tg128 measured the
 * same way as every other runtime in the comparison; it replaces the previous
 * hand-rolled run_bench_loop(), whose single-shot, no-warmup timing was not
 * methodologically symmetric with the Vulkan appliance and the baselines.
 *
 * --mmap 0: the model lives on 9pfs where mmap is unsupported, so load it with
 *   read() (matches the previous load_model_common use_mmap=false). mmap only
 *   affects loading, not the pp512/tg128 throughput numbers. No --no-host here:
 *   that flag is a Vulkan device-local-weights knob with no CPU meaning.
 *
 * Output: llama-bench's markdown table (test column rows "pp512"/"tg128"),
 * parsed by scripts/app-llama-cpu.py, followed by the PASS evidence line.
 */
#include "llama-cpu-common.h"

#if CONFIG_APP_LLAMA_CPU_MODE_BENCH

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <sys/stat.h>
#include <sys/mount.h>

// Upstream entry point (tools/llama-bench/llama-bench.cpp). Its main.cpp is not
// compiled into the appliance; the single-app image calls llama_bench() here.
extern int llama_bench(int argc, char ** argv);

#define CPU_STR2(x) #x
#define CPU_STR(x)  CPU_STR2(x)

int main(void)
{
    // Mount the host-provided GGUF over 9pfs at /mnt/model. llama-bench loads
    // the model itself, so the mount has to happen here (load_model_common,
    // which used to do it, is no longer on the path).
    mkdir("/mnt", 0755);
    mkdir("/mnt/model", 0755);
    if (mount("model", "/mnt/model", "9pfs", 0, "") != 0) {
        uk_printf("uk-llama-upstream: FAIL 9pfs mount failed errno=%d\n", errno);
        return 1;
    }

    // "config" line: printed after boot + 9p mount, just before llama-bench, so
    // scripts/app-llama-cpu.py can time launch->ready (boot_time_s) separately
    // from the pp512/tg128 inference numbers. Mirrors the VK appliance.
    uk_printf("uk-llama-upstream: config threads=%s\n",
              CPU_STR(CONFIG_APP_LLAMA_CPU_THREADS));

    char arg0[]  = "llama-bench";
    char a_m[]   = "-m";        char a_model[] = "/mnt/model/model.gguf";
    char a_ngl[] = "-ngl";      char a_0[]     = "0";
    char a_p[]   = "-p";        char a_512[]   = "512";
    char a_n[]   = "-n";        char a_128[]   = "128";
    char a_t[]   = "-t";        char a_thr[]   = CPU_STR(CONFIG_APP_LLAMA_CPU_THREADS);
    char a_mm[]  = "--mmap";    char a_zero[]  = "0";
    char *argv[] = { arg0, a_m, a_model, a_ngl, a_0, a_p, a_512,
                     a_n, a_128, a_t, a_thr, a_mm, a_zero, nullptr };
    int argc = (int) (sizeof(argv) / sizeof(argv[0])) - 1;

    int rc = llama_bench(argc, argv);

    if (rc == 0)
        uk_puts("uk-llama-upstream: PASS evidence_id=llama-upstream-cpu\n");
    else
        uk_printf("uk-llama-upstream: FAIL llama_bench rc=%d\n", rc);

    return rc;
}

#endif /* CONFIG_APP_LLAMA_CPU_MODE_BENCH */
