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

#define UK_LLAMA_STR_(x) #x
#define UK_LLAMA_STR(x)  UK_LLAMA_STR_(x)

int llama_server(int argc, char ** argv);

static int prepare_runtime(void)
{
    mkdir("/mnt", 0755);
    mkdir("/mnt/model", 0755);
    if (mount("model", "/mnt/model", "9pfs", 0, "") != 0) {
        uk_printf("uk-llama-upstream-vk-server: FAIL 9pfs mount errno=%d\n", errno);
        return 1;
    }
    if (uk_ggml_vulkan_dispatch_init() != 0) {
        uk_puts("uk-llama-upstream-vk-server: FAIL dispatch_init\n");
        return 1;
    }
    return 0;
}

static int llama_server_main(void)
{
    if (prepare_runtime() != 0)
        return 1;

    struct uk_ggml_vulkan_dispatch_info info;
    uk_ggml_vulkan_dispatch_get_info(&info);
    uk_printf("uk-llama-upstream-vk-server: READY model=/mnt/model/model.gguf threads=%d backend=vulkan "
              "slots=%d ctx_per_slot=%d prompt_cache=%d hostmem_fixed=%d mode=single-app no_fork_exec=1\n",
              CONFIG_APP_LLAMA_UPSTREAM_VK_THREADS,
              CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL,
              CONFIG_APP_LLAMA_UPSTREAM_VK_CTX,
              CONFIG_APP_LLAMA_UPSTREAM_VK_PROMPT_CACHE,
              info.hostmem_fixed);

    static char arg0[]       = "llama-server";
    static char model_f[]    = "-m";
    static char model[]      = "/mnt/model/model.gguf";
    static char host_f[]     = "--host";
    static char host[]       = "0.0.0.0";
    static char port_f[]     = "--port";
    static char port[]       = "8080";
    static char ctx_f[]      = "--ctx-size";
    static char ctx[]        = UK_LLAMA_STR(CONFIG_APP_LLAMA_UPSTREAM_VK_CTX);
    static char parallel_f[] = "--parallel";
    static char parallel[]   = UK_LLAMA_STR(CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL);
    static char threads_f[]  = "--threads";
    static char threads[]    = UK_LLAMA_STR(CONFIG_APP_LLAMA_UPSTREAM_VK_THREADS);
    static char ngl_f[]      = "--n-gpu-layers";
    static char ngl[]        = "99";
#if CONFIG_APP_LLAMA_UPSTREAM_VK_PROMPT_CACHE
    static char cache[]      = "--cache-prompt";
    char *argv[] = {arg0, model_f, model, host_f, host, port_f, port,
                    ctx_f, ctx, parallel_f, parallel, threads_f, threads,
                    ngl_f, ngl, cache};
#else
    char *argv[] = {arg0, model_f, model, host_f, host, port_f, port,
                    ctx_f, ctx, parallel_f, parallel, threads_f, threads,
                    ngl_f, ngl};
#endif

    return llama_server((int)(sizeof(argv) / sizeof(argv[0])), argv);
}

int main(void)
{
    return llama_server_main();
}

#endif /* CONFIG_APP_LLAMA_UPSTREAM_VK_MODE_SERVER */
