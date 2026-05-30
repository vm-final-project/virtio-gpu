# ggml/llama.cpp syscall surface audit

Source: `/home/jerrytsai/llama.cpp` — `src/`, `ggml/include/`, `ggml/src/`,
`common/`, `include/`.

## Classification

### Class A — Native Unikraft lib (no stub needed)

| Syscall / API | Unikraft provider | Notes |
|---|---|---|
| `mmap`, `munmap`, `mprotect` | `lib/posix-mmap` | Model file mmap (x86_64 path). |
| `madvise` | `lib/posix-mmap` | `MADV_*` is safe-no-op in posix-mmap. |
| `pthread_create`, `pthread_join` | `lib/pthread-embedded` | Already wired in llama Kraftfiles. |
| `pthread_mutex_*`, `pthread_cond_*` | `lib/pthread-embedded` + `lib/posix-futex` | Standard threading. |
| `clock_gettime`, `gettimeofday`, `nanosleep` | `lib/posix-time` via musl | Timing. |
| `sched_yield` | `lib/posix-process` via musl | Scheduler yield. |
| `open`, `openat`, `read`, `pread`, `write`, `fstat`, `lseek`, `fcntl` | `lib/posix-vfs` + `lib/posix-fd*` | File I/O via 9pfs/initramfs. |

### Class B — Upstream `lib-musl` (no stub needed)

| Syscall / API | Provider | Notes |
|---|---|---|
| `getrandom`, `arc4random` | upstream lib-musl | Maps to Unikraft entropy. |
| `sem_*` | upstream lib-musl → pthread-embedded | Semaphores. |
| `exit_group` | upstream lib-musl | Program exit. |
| `sysconf(_SC_NPROCESSORS_ONLN/_PAGE_SIZE/_PAGESIZE/_PHYS_PAGES)` | upstream lib-musl | Strong symbols; map onto Unikraft. |
| `getauxval(AT_HWCAP/AT_HWCAP2/AT_PLATFORM)` | upstream lib-musl | Default zero-return is safe; disables optional SIMD probing. |
| `prctl(PR_SET_PTRACER, ...)` | upstream lib-musl | Debugging hint; no-op success. |
| `pthread_setaffinity_np` | upstream lib-musl + pthread-embedded | CPU pinning is no-op. |

### Class D — Platform-specific, not compiled for x86_64 (ignored)

| Source | Syscall | Reason |
|---|---|---|
| `ggml/src/ggml-cpu/spacemit/spine_mem_pool.cpp` | `mmap`, `ioctl(HUGETLB_*)` | `#ifdef __spacemit__` only. |
| `ggml/src/ggml-hexagon/ggml-hexagon.cpp` | `mmap()` | Hexagon DSP backend. |
| `common/console.cpp` | `ioctl(TIOCGWINSZ)` | Interactive CLI only; not linked into bench/server appliances. |

## Summary

The full POSIX surface required by upstream ggml/llama.cpp on x86_64 is now
served by Unikraft's native libs plus the upstream `lib-musl` external library.
No project-local POSIX shim is needed.

## Verification

```bash
git -C /home/jerrytsai/llama.cpp diff --stat
# Expected: empty (no upstream modifications)
```
