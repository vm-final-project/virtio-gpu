# llama-common Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the duplicated `uk_puts`, `uk_printf`, `now_sec`, model-load logic, and pp/tg bench measurement loop from `app-llama-cpu` and `app-llama-vk` into a shared `apps/llama-common/llama-uk-common.h`, so there is one source of truth and no risk of silent divergence.

**Architecture:** A new `apps/llama-common/` directory holds a single header `llama-uk-common.h` with the three utility inlines, a `load_model_common()` that accepts `n_gpu_layers` as a parameter, and a `run_bench_loop()` that runs pp512+tg128 and returns a `bench_result` struct. Each app-specific header includes the shared header and provides a thin wrapper (`load_model` for CPU; `load_model_vk` for VK). Each `bench.cpp` calls `run_bench_loop()` instead of duplicating the measurement loop. Each app's `Makefile.uk` gains one `-I` pointing at the shared directory; no other build changes are needed.

**Tech Stack:** C++17, Unikraft Makefile.uk, llama.cpp C API (`llama.h`), `<uk/vulkan.h>` (VK app only)

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| **Create** | `apps/llama-common/llama-uk-common.h` | `uk_puts`, `uk_printf`, `now_sec`, `load_model_common()`, `run_bench_loop()` |
| **Modify** | `apps/app-llama-cpu/llama-cpu-common.h` | Replace duplicated body with `#include` + thin `load_model()` wrapper |
| **Modify** | `apps/app-llama-vk/llama-vk-common.h` | Replace duplicated body with `#include` + thin `load_model_vk()` wrapper |
| **Modify** | `apps/app-llama-cpu/Makefile.uk` | Add `-I$(APPLLAMA_CPU_BASE)/../llama-common` to `CINCLUDES`/`CXXINCLUDES` |
| **Modify** | `apps/app-llama-vk/Makefile.uk` | Add `-I$(APPLLAMA_VK_BASE)/../llama-common` to `CINCLUDES`/`CXXINCLUDES` |
| **Modify** | `apps/app-llama-cpu/bench.cpp` | Replace inline pp/tg loop with `run_bench_loop()` call |
| **Modify** | `apps/app-llama-vk/bench.cpp` | Replace inline pp/tg loop with `run_bench_loop()` call |

`server.cpp` in both apps is **not touched** — the argv construction differs by Kconfig macro prefix and cannot be cleanly shared.

---

## Task 1: Create `apps/llama-common/llama-uk-common.h`

**Files:**
- Create: `apps/llama-common/llama-uk-common.h`

- [ ] **Step 1: Create the directory and file**

```bash
mkdir -p apps/llama-common
```

Write `apps/llama-common/llama-uk-common.h` with this exact content:

```cpp
/*
 * Shared helpers for all llama.cpp single-purpose appliances.
 *
 * Provides: uk_puts, uk_printf, now_sec, load_model_common.
 * Each app-specific header includes this file and adds its own
 * load_model / load_model_vk wrapper.
 */
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <uk/console.h>
}

#include "llama.h"

static inline void uk_puts(const char *s)
{
    uk_console_out(s, strlen(s));
}

static inline void uk_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uk_console_out(buf, strlen(buf));
}

static inline double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/*
 * Mount /mnt/model over 9pfs, initialise the llama backend, and load the
 * GGUF at model_path. n_gpu_layers controls GPU offload: pass 0 for CPU-only,
 * 99 to offload all layers to the Vulkan device.
 *
 * Returns a valid llama_model* on success, nullptr on failure (backend freed).
 */
static inline llama_model *load_model_common(const char *model_path,
                                              const char *tag,
                                              int n_gpu_layers)
{
    mkdir("/mnt", 0755);
    mkdir("/mnt/model", 0755);
    if (mount("model", "/mnt/model", "9pfs", 0, "") != 0) {
        uk_printf("%s: FAIL 9pfs mount failed errno=%d\n", tag, errno);
        return nullptr;
    }

    llama_backend_init();
    llama_numa_init(GGML_NUMA_STRATEGY_DISABLED);

    llama_model_params mparams = llama_model_default_params();
    mparams.use_mmap     = false;
    mparams.n_gpu_layers = n_gpu_layers;

    double t0 = now_sec();
    llama_model *model = llama_model_load_from_file(model_path, mparams);
    double load_ms = (now_sec() - t0) * 1000.0;

    uk_printf("%s: model_load path=%s use_mmap=%d n_gpu_layers=%d elapsed_ms=%.2f\n",
              tag, model_path, (int)mparams.use_mmap, n_gpu_layers, load_ms);

    if (!model) {
        uk_printf("%s: FAIL model_load failed path=%s\n", tag, model_path);
        llama_backend_free();
        return nullptr;
    }
    return model;
}
```

Also append `run_bench_loop` to the same file:

```cpp
#include <vector>

struct bench_result { double pp512; double tg128; };

/*
 * Run pp512 + tg128 on an already-initialised llama_context and return
 * tokens/sec for each pass. The caller is responsible for creating the
 * context with n_ctx >= 640 and n_batch >= 512.
 */
static inline bench_result run_bench_loop(llama_context *ctx)
{
    const int n_pp = 512;
    std::vector<llama_token> tokens(n_pp, 0);
    llama_batch batch = llama_batch_get_one(tokens.data(), n_pp);

    double t0 = now_sec();
    llama_decode(ctx, batch);
    double pp_ms = (now_sec() - t0) * 1000.0;

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

    return { (n_pp / pp_ms) * 1000.0, (n_tg / tg_ms) * 1000.0 };
}
```

- [ ] **Step 2: Verify the file exists and is well-formed C++**

```bash
grep -c "static inline" apps/llama-common/llama-uk-common.h
```
Expected output: `5` (uk_puts, uk_printf, now_sec, load_model_common, run_bench_loop)

- [ ] **Step 3: Commit**

```bash
git add apps/llama-common/llama-uk-common.h
git commit -m "refactor: add apps/llama-common/llama-uk-common.h with shared llama helpers"
```

---

## Task 2: Refactor `apps/app-llama-cpu/llama-cpu-common.h`

**Files:**
- Modify: `apps/app-llama-cpu/llama-cpu-common.h`

- [ ] **Step 1: Replace the file body**

The new file keeps the same include guard and `CONFIG_APP_LLAMA_CPU_THREADS` default, but delegates all shared logic to `llama-uk-common.h`. Replace the entire file with:

```cpp
/*
 * CPU-appliance helpers. Delegates shared utilities (uk_puts, uk_printf,
 * now_sec, load_model_common) to llama-uk-common.h and provides the CPU-
 * specific load_model() wrapper (n_gpu_layers=0).
 */
#pragma once

#include "llama-uk-common.h"

#ifndef CONFIG_APP_LLAMA_CPU_THREADS
#define CONFIG_APP_LLAMA_CPU_THREADS 1
#endif

static inline llama_model *load_model(const char *model_path, const char *tag)
{
    return load_model_common(model_path, tag, 0);
}
```

- [ ] **Step 2: Verify no duplication remains**

```bash
grep -c "uk_console_out\|clock_gettime\|llama_backend_init" apps/app-llama-cpu/llama-cpu-common.h
```
Expected output: `0`

- [ ] **Step 3: Commit**

```bash
git add apps/app-llama-cpu/llama-cpu-common.h
git commit -m "refactor: llama-cpu-common.h delegates to llama-uk-common.h"
```

---

## Task 3: Refactor `apps/app-llama-vk/llama-vk-common.h`

**Files:**
- Modify: `apps/app-llama-vk/llama-vk-common.h`

- [ ] **Step 1: Replace the file body**

The VK wrapper adds `uk_vulkan_init()` before delegating to `load_model_common`. Replace the entire file with:

```cpp
/*
 * Vulkan/Venus-appliance helpers. Delegates shared utilities to
 * llama-uk-common.h and provides the VK-specific load_model_vk() wrapper
 * which first wires the static Venus dispatch chain, then offloads all
 * layers to the host GPU (n_gpu_layers=99).
 */
#pragma once

#include "llama-uk-common.h"

extern "C" {
#include <uk/vulkan.h>
}

#ifndef CONFIG_APP_LLAMA_VK_THREADS
#define CONFIG_APP_LLAMA_VK_THREADS 1
#endif

static inline llama_model *load_model_vk(const char *model_path, const char *tag)
{
    if (uk_vulkan_init() != 0) {
        uk_printf("%s: FAIL dispatch_init failed\n", tag);
        return nullptr;
    }
    return load_model_common(model_path, tag, 99);
}
```

- [ ] **Step 2: Verify no duplication remains**

```bash
grep -c "uk_console_out\|clock_gettime\|llama_backend_init" apps/app-llama-vk/llama-vk-common.h
```
Expected output: `0`

- [ ] **Step 3: Commit**

```bash
git add apps/app-llama-vk/llama-vk-common.h
git commit -m "refactor: llama-vk-common.h delegates to llama-uk-common.h"
```

---

## Task 4: Add include path in `apps/app-llama-cpu/Makefile.uk`

**Files:**
- Modify: `apps/app-llama-cpu/Makefile.uk`

The CPU app needs to find `llama-uk-common.h`. Add one `-I` to the existing `APPLLAMA_CPU_CINCLUDES-y` and `APPLLAMA_CPU_CXXINCLUDES-y` lines.

- [ ] **Step 1: Locate the include lines**

```bash
grep -n "APPLLAMA_CPU_CINCLUDES\|APPLLAMA_CPU_CXXINCLUDES" apps/app-llama-cpu/Makefile.uk | head -6
```

- [ ] **Step 2: Add the shared include path**

Find the first `APPLLAMA_CPU_CINCLUDES-y` assignment and append `-I$(APPLLAMA_CPU_BASE)/../llama-common`. It should look like:

```makefile
APPLLAMA_CPU_CINCLUDES-y   += -I$(GGML_INC) -I$(GGML_SRC) -I$(GGML_SRC)/ggml-cpu \
                                   -isystem $(APPLLAMA_CPU_BUILTIN_INC) \
                                   -I$(APPLLAMA_CPU_BASE)/../llama-common
APPLLAMA_CPU_CXXINCLUDES-y += -I$(LLAMA_ROOT)/include -I$(LLAMA_SRC) \
                                   -I$(LLAMA_SRC)/models \
                                   -I$(GGML_INC) -I$(GGML_SRC) -I$(GGML_SRC)/ggml-cpu \
                                   -isystem $(APPLLAMA_CPU_BUILTIN_INC) \
                                   -I$(APPLLAMA_CPU_BASE)/../llama-common
```

- [ ] **Step 3: Verify the path appears exactly twice (C and C++)**

```bash
grep -c "llama-common" apps/app-llama-cpu/Makefile.uk
```
Expected output: `2`

- [ ] **Step 4: Commit**

```bash
git add apps/app-llama-cpu/Makefile.uk
git commit -m "build: add llama-common include path to app-llama-cpu"
```

---

## Task 5: Add include path in `apps/app-llama-vk/Makefile.uk`

**Files:**
- Modify: `apps/app-llama-vk/Makefile.uk`

- [ ] **Step 1: Locate the include lines**

```bash
grep -n "APPLLAMA_VK_CINCLUDES\|APPLLAMA_VK_CXXINCLUDES" apps/app-llama-vk/Makefile.uk | head -6
```

- [ ] **Step 2: Add the shared include path**

Find the `APPLLAMA_VK_CINCLUDES-y` and `APPLLAMA_VK_CXXINCLUDES-y` blocks and append `-I$(APPLLAMA_VK_BASE)/../llama-common` to each:

```makefile
APPLLAMA_VK_CINCLUDES-y   += -I$(APPLLAMA_VK_BASE)/compat \
                              -I$(GGML_INC) -I$(GGML_SRC) -I$(GGML_SRC)/ggml-cpu \
                              -I$(APPLLAMA_VK_BASE)/../llama-common
APPLLAMA_VK_CXXINCLUDES-y += -I$(APPLLAMA_VK_BASE)/compat \
                              -I$(LLAMA_ROOT)/include -I$(LLAMA_SRC) \
                              ... (existing entries) \
                              -I$(APPLLAMA_VK_BASE)/../llama-common
```

- [ ] **Step 3: Verify the path appears exactly twice**

```bash
grep -c "llama-common" apps/app-llama-vk/Makefile.uk
```
Expected output: `2`

- [ ] **Step 4: Commit**

```bash
git add apps/app-llama-vk/Makefile.uk
git commit -m "build: add llama-common include path to app-llama-vk"
```

---

## Task 6: Refactor `bench.cpp` in both apps to use `run_bench_loop()`

**Files:**
- Modify: `apps/app-llama-cpu/bench.cpp`
- Modify: `apps/app-llama-vk/bench.cpp`

- [ ] **Step 1: Rewrite `apps/app-llama-cpu/bench.cpp`**

Replace the entire file with:

```cpp
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

int main(void)
{
    const char *model_path = "/mnt/model/model.gguf";
    llama_model *model = load_model(model_path, "uk-llama-upstream");
    if (!model)
        return 1;

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx            = 512 + 128;
    cparams.n_batch          = 512;
    cparams.n_threads        = CONFIG_APP_LLAMA_CPU_THREADS;
    cparams.n_threads_batch  = CONFIG_APP_LLAMA_CPU_THREADS;

    llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        uk_puts("uk-llama-upstream: FAIL ctx_init failed\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    bench_result r = run_bench_loop(ctx);
    uk_printf("uk-llama-upstream: pp512=%.1f tg128=%.1f\n", r.pp512, r.tg128);
    uk_puts("uk-llama-upstream: PASS evidence_id=llama-upstream-cpu\n");

    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}

#endif /* CONFIG_APP_LLAMA_CPU_MODE_BENCH */
```

- [ ] **Step 2: Rewrite `apps/app-llama-vk/bench.cpp`**

Replace the entire file with:

```cpp
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

#endif /* CONFIG_APP_LLAMA_VK_MODE_BENCH */
```

- [ ] **Step 3: Verify the old inline loop is gone from both files**

```bash
grep -c "llama_memory_clear\|n_pp\|n_tg\|tg_pos" \
    apps/app-llama-cpu/bench.cpp apps/app-llama-vk/bench.cpp
```
Expected output: `0`

- [ ] **Step 4: Verify both files call run_bench_loop**

```bash
grep -c "run_bench_loop" apps/app-llama-cpu/bench.cpp apps/app-llama-vk/bench.cpp
```
Expected output:
```
apps/app-llama-cpu/bench.cpp:1
apps/app-llama-vk/bench.cpp:1
```

- [ ] **Step 5: Commit**

```bash
git add apps/app-llama-cpu/bench.cpp apps/app-llama-vk/bench.cpp
git commit -m "refactor: bench.cpp delegates pp/tg loop to run_bench_loop()"
```

---

## Task 7: Build verification

This project compiles inside a Unikraft toolchain, so standard `make` on macOS will not produce a full image. The checks below verify at the source level that the refactor is self-consistent.

- [ ] **Step 1: Check no file still contains the old duplicated bodies**

```bash
grep -rn "uk_console_out\|clock_gettime\|llama_backend_init\|llama_numa_init" \
    apps/app-llama-cpu/llama-cpu-common.h \
    apps/app-llama-vk/llama-vk-common.h
```
Expected output: *(empty — no matches)*

- [ ] **Step 2: Confirm every caller of load_model / load_model_vk still compiles symbolically**

```bash
grep -rn "load_model\b" apps/app-llama-cpu/
grep -rn "load_model_vk\b" apps/app-llama-vk/
```
Expected: `bench.cpp` and `server.cpp` in each app each call the correct function, and each app's `*-common.h` defines it.

- [ ] **Step 3: Confirm load_model_common is defined only once**

```bash
grep -rn "load_model_common" apps/
```
Expected: defined in `apps/llama-common/llama-uk-common.h`, called only from `llama-cpu-common.h` and `llama-vk-common.h`.

- [ ] **Step 4: Confirm there is no uk_puts / uk_printf / now_sec / run_bench_loop definition outside llama-uk-common.h**

```bash
grep -rn "static inline void uk_puts\|static inline void uk_printf\|static inline double now_sec\|static inline bench_result run_bench_loop" \
    apps/app-llama-cpu/ apps/app-llama-vk/
```
Expected output: *(empty)*

- [ ] **Step 5: Confirm run_bench_loop is defined only once**

```bash
grep -rn "run_bench_loop" apps/
```
Expected: defined in `apps/llama-common/llama-uk-common.h`, called once each in `app-llama-cpu/bench.cpp` and `app-llama-vk/bench.cpp`.

- [ ] **Step 5: Final commit (if any fixups were needed)**

```bash
git add -p
git commit -m "refactor: fixup after build verification"
```

---

## Sync-prevention note

With this structure, the rule is simple: **`uk_puts`, `uk_printf`, `now_sec`, the mount+load logic, and the pp/tg measurement loop live in exactly one file** (`llama-uk-common.h`). Any future change to e.g. the log format, 9pfs mount logic, or benchmark token counts is made once and both apps pick it up automatically. The CPU/VK split only remains in:

- The `n_gpu_layers` value (0 vs 99) — in `load_model` / `load_model_vk`
- The `uk_vulkan_init()` call — in `load_model_vk` only
- The three `setenv` Vulkan env variables — in `bench.cpp` and `server.cpp` of the VK app, where they legitimately belong to the VK runtime path

These differences are intentional and correctly scoped; they do not need to be shared.
