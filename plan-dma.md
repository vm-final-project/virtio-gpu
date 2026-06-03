# plan-dma.md — Replace `libukdma` / `libukdma_pool` with Unikraft official implementations

Status: proposal (not yet executed).
Goal: retire the two hand-written first-party libraries `libukdma` (`LIBUKDMA`)
and `libukdma_pool` (`LIBUKBUF`) and route their responsibilities through the
**existing upstream Unikraft libraries**, which already cover the same ground and
are maintained/tested upstream.

All findings below were verified against the actual checked-out code in
`/mydata/JerryT/unikraft` and `/mydata/JerryT/virtio-gpu` on 2026-06-03.

---

## 1. What these two libraries actually do today

### `libukdma` (`config LIBUKDMA`, header `include/uk/dma.h`)
`libs/libukdma/dma_alloc.c` (53 lines, libc-only) provides:

| Function | What it does | Official equivalent |
|---|---|---|
| `uk_dma_alloc` | `posix_memalign` + optional zero | `uk_posix_memalign` (`uk/alloc.h:251`) |
| `uk_dma_free` | `free` | `uk_free` |
| `uk_dma_build_sg` | single-segment sg: `paddr = (uintptr_t)vaddr` | `uksglist` (`uk_sglist_append`) |
| `uk_dma_sync_for_device` / `_for_cpu` | **no-op** (x86 is cache-coherent) | none needed (drop on x86) |

Address conversion is a plain identity cast. Upstream
`uk_paging_virt_to_phys()` is the *same thing* when `CONFIG_LIBUKPAGING` is off:
`lib/ukpaging/include/uk/paging.h:842` → `return (__paddr_t)address;`. Current
build has `CONFIG_LIBUKPAGING` **off** and `CONFIG_HAVE_PAGING=y` (direct map),
so the two are bit-identical today.

Public types in `uk/dma.h`: `struct uk_dma_buf {vaddr, paddr_or_iova, len,
align, flags}`, `struct uk_dma_sg {paddr_or_iova, len}`, flags
`UK_DMA_F_{CONTIGUOUS,ZEROED,HOST_SHARED}`, enum `uk_dma_dir`.

### `libukdma_pool` (`config LIBUKBUF`, header `include/uk/buf.h`)
`libs/libukdma_pool/buf_pool.c` (72 lines): fixed-size pool of `uk_dma_buf`,
**O(N) linear scan** in both `uk_buf_get` and `uk_buf_put`.

Official equivalent: `ukallocpool` (`lib/ukallocpool/`), a LIFO pool with **O(1)**
`uk_allocpool_take` / `uk_allocpool_return`, plus `uk_allocpool2ukalloc()` to
expose a `uk_alloc` interface.

---

## 2. Consumers (the migration surface)

### `libukdma` consumers
| Site | Usage | File:line |
|---|---|---|
| Public ABI of `libukvirtio_gpu` | `resource_attach_backing(..., const struct uk_dma_sg *sg, size_t nr_sg)` | `libs/libukvirtio_gpu/include/uk/virtio_gpu.h:191` |
| `virtio_gpu_real.c` | reads `sg[i].paddr_or_iova`, `.len` | `libs/libukvirtio_gpu/virtio_gpu_real.c:271-300` |
| `libukegl` | alloc / build_sg / free / sync_for_device | `libs/libukegl/src/egl_glue.c:209,212,271,294` |
| `app-kmscube` | alloc / build_sg / free / sync_for_device | `apps/app-kmscube/main.c:145,150,166,204` |
| fake backend | attach_backing signature mirror | `tests/virtio_gpu_fake.c:221` |
| native tests | compile `dma_alloc.c` directly | `tests/Makefile:127,136,142,145,148,157,173` |
| `Config.uk select LIBUKDMA` | libukegl, libukgbm_compat, app-kmscube, app-vkmark, app-vulkan-smoke, app-glmark2 | (6 files) |

### `libukdma_pool` consumers
**Only `tests/dma_buf_test.c`** uses `uk_buf_pool_*`. No `libs/` or `apps/` code
references it. It is effectively dead production code — pure win to remove.

---

## 3. Decisive constraints (read before touching anything)

1. **Dual build.** The primary CI gate `make native-tests` compiles guest `.c`
   sources **on the host** with libc and `-Ishim` (`tests/Makefile:33`). Upstream
   `uksglist`/`ukallocpool` are **not** host-compilable (they pull `uk/alloc.h`,
   `uk/paging.h`, `uk/vmem.h`, `uk/list.h`). There is already a host-stub
   mechanism: `tests/shim/uk/` (currently only `mutex.h`). Any upstream header
   the guest code starts including must get a matching host shim here, or the
   native gate breaks.

2. **`uksglist` is already enabled** in the build (`CONFIG_LIBUKSGLIST=y` in the
   `.config.*` files). `ukallocpool` is **not** (`# CONFIG_LIBUKALLOCPOOL is not
   set`) — it must be added to the Kraftfile kconfig if/when a pool is needed.

3. **ABI leak.** `uk_dma_sg` is part of `libukvirtio_gpu`'s *public* header.
   Migrating to `uk_sglist`/`uk_sglist_seg` (fields `ss_paddr`, `ss_len`) changes
   the `resource_attach_backing` signature and touches both callers (egl,
   kmscube), the fake backend, and ~6 tests in lock-step.

4. **`uk_dma_buf` is an allocator+mapping wrapper**, but `uksglist` only *builds
   sg lists from already-allocated buffers* — it does not allocate. So the
   alloc/align role must move to `uk_posix_memalign` (guest) / `posix_memalign`
   (host shim), not to `uksglist`.

5. **Governance.** Per CLAUDE.md, after touching libs/apps run `make
   governance-check lib-readme-check app-port-check`. `config/governance.json`
   has entries at lines ~135 (`libukdma`) and ~207 (`libukdma_pool`);
   `README.md:92` lists both. These must be updated/removed in the same change.

---

## 4. Decision point (pick before Phase 2)

**Option A — full migration to upstream types (recommended).**
Delete `uk/dma.h`. Replace `uk_dma_sg` with `uk_sglist`/`uk_sglist_seg` in the
`libukvirtio_gpu` public API; allocate buffers with `uk_posix_memalign`; drop the
no-op sync calls on x86. Cleanest end state, no first-party DMA code left.
Cost: touches the public ABI + 2 apps/libs + fake backend + ~6 tests + shims.

**Option B — keep `uk/dma.h` as a thin compat shim over upstream.**
Keep the `uk_dma_buf`/`uk_dma_sg` types but re-implement the bodies on top of
`uk_posix_memalign` + `uksglist` (and map `uk_dma_sg` ⇄ `uk_sglist_seg`). Far
smaller blast radius (no signature changes), but leaves a first-party header in
place — only *partially* satisfies "replace with official implementations".

This plan executes **Option A** and notes B as the low-risk fallback.

---

## 5. Phased plan

### Phase 0 — Safety net
- `cd virtio-gpu && make native-tests` → capture current PASS baseline.
- `git checkout -b dma-upstream-migration` (work off `main`).
- If `kraft` available: `make kmscube-build` so the unikernel link path is
  covered, not just host tests.

### Phase 1 — Remove `libukdma_pool` (LIBUKBUF) — low risk, do first
1. Delete `libs/libukdma_pool/`.
2. `tests/dma_buf_test.c`: drop the `uk_buf_pool_*` section. Keep or rename the
   remaining DMA-buffer checks (they move with Phase 2). Update
   `tests/Makefile:127` (remove `buf_pool.c` from the recipe) and the
   `native:` / `test-core` group lists (`tests/Makefile:49,71`).
3. Kraftfile: remove `CONFIG_LIBUKBUF: 'y'` (`Kraftfile:11`) and the `libukbuf:`
   library source block (`Kraftfile:28-29`).
4. `config/governance.json`: delete the `libukdma_pool` entry (~lines 207-214).
5. `README.md:92`: drop the `libukdma_pool` mention.
6. Gate: `make native-tests governance-check lib-readme-check`.

> No production code consumes the pool, so there is nothing to re-point at
> `ukallocpool`. If a real buffer pool is wanted later (e.g. Venus ring
> staging), introduce it then via `uk_allocpool_alloc(<parent uk_alloc>, count,
> len, align)` inside `libukvirtio_gpu`/`libukvenus` — not as a standalone lib.
> Add `CONFIG_LIBUKALLOCPOOL: 'y'` to the Kraftfile at that point.

### Phase 2 — Migrate `libukdma` allocation + sg to upstream (Option A)

**2a. Host shim for the native gate (must land first).**
Add `tests/shim/uk/sglist.h` mirroring the subset of
`unikraft/lib/uksglist/include/uk/sglist.h` the guest code uses (`uk_sglist`,
`uk_sglist_seg`, `uk_sglist_init`, `uk_sglist_reset`, `uk_sglist_append`,
`uk_sglist_length`) in libc terms (`ss_paddr = (uintptr_t)buf`). Add a minimal
`uk_posix_memalign` host inline (e.g. `tests/shim/uk/alloc.h`) for guest sources
that switch to it. Mirror the existing `mutex.h` shim style/header comment.

**2b. `libukvirtio_gpu` public API.**
Change `resource_attach_backing` to take `const struct uk_sglist *` (or
`const struct uk_sglist_seg *segs, size_t nr`) instead of `uk_dma_sg`
(`include/uk/virtio_gpu.h:191`). Update `virtio_gpu_real.c:271-300` to read
`segs[i].ss_paddr` / `ss_len`. Drop `#include <uk/dma.h>` (line 4), add
`#include <uk/sglist.h>`.

**2c. Callers.**
- `libukegl/src/egl_glue.c`: replace `uk_dma_buf`/`uk_dma_sg` with a
  `uk_posix_memalign`'d buffer + a `uk_sglist` (single `uk_sglist_append`). Drop
  `uk_dma_sync_for_device` (x86 coherent). Free with `uk_free`.
- `apps/app-kmscube/main.c`: same transformation (`main.c:145-166,204`).
- `libukgbm_compat`: only `select LIBUKDMA` in Config.uk; grep shows no real
  `uk/dma.h` use — just drop the select.

**2d. Config selects.**
Replace `select LIBUKDMA` with `select LIBUKSGLIST` in: `libukegl`,
`libukgbm_compat`, `app-kmscube`, `app-vkmark`, `app-vulkan-smoke`,
`app-glmark2`. `LIBUKALLOC` is always present for `uk_posix_memalign`.

**2e. Tests.**
Remove `../libs/libukdma/dma_alloc.c` from every recipe that lists it
(`tests/Makefile:136,142,145,148,157,173`); guest code now uses the shim'd
`uk_posix_memalign`/`uksglist`. Update `tests/virtio_gpu_fake.c:221`,
`tests/virtio_gpu_2d_render_test.c`, `tests/virtgpu_drm_ioctl_test.c`,
`tests/vk_icd_bootstrap_test.c`, `tests/venus_cs_test.c`,
`tests/virgl_encoder_test.c`, `tests/virtio_gpu_full_api_test.c` to the new
sg type.

**2f. Delete `libs/libukdma/` and update Kraftfile/governance/README.**
- Remove `CONFIG_LIBUKDMA: 'y'` (`Kraftfile:10`) and the `libukdma:` source block
  (`Kraftfile:26-27`); ensure `CONFIG_LIBUKSGLIST: 'y'` present (it is).
- `config/governance.json`: delete `libukdma` entry (~lines 135-141).
- `README.md:92`: replace the DMA-lib mention with "DMA/sg via upstream
  `uksglist`".

### Phase 3 — Verification
```sh
cd virtio-gpu
make native-tests                       # primary gate — must stay PASS
make governance-check lib-readme-check app-port-check
make test-fast
make kmscube-build                      # real unikernel link (needs kraft)
make verify                             # broadest gate (QEMU rows when available)
```
Confirm the kmscube/2D rows in `results/vogue_evaluation_matrix.*` stay PASS (not
regressed to `blocked:*`).

---

## 6. Risk / rollback

- **Highest risk:** the public-ABI change in `libukvirtio_gpu`. Do 2b–2e
  atomically in one commit; the compiler flags every missed caller.
- **Native-gate breakage** from missing shims is the most likely failure — 2a
  must land before 2b–2e on the same branch.
- **If schedule-constrained:** ship Phase 1 (pool removal) alone — self-contained,
  removes dead code, needs no shim work. Then do Phase 2, or fall back to
  **Option B** for `libukdma` to avoid ABI churn.
- Rollback = `git checkout main` (single branch, no submodule edits).

## 7. End state

- `libs/libukdma/` and `libs/libukdma_pool/` deleted.
- Allocation/alignment → `uk_posix_memalign`; scatter-gather → `uksglist`
  (already enabled); virt→phys → `uk_paging_virt_to_phys` (via uksglist, identity
  under current config); cache sync → dropped (x86 coherent); pooling →
  `ukallocpool` if/when a real pool consumer appears.
- Governance, README, Kraftfile, and the native-test harness updated to match.
- Net: ~125 lines of first-party DMA code removed in favour of maintained
  upstream libraries, with an O(N)→O(1) pool improvement available for free.
