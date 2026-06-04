/* SPDX-License-Identifier: MIT */
/*
 * tests/shim/uk/mutex.h — host-native build shim for <uk/mutex.h>.
 *
 * The deterministic host-native suite compiles guest libvulkan /
 * libukvirtio_gpu sources directly against the fake VirtIO-GPU backend
 * (tests/virtio_gpu_fake.c). Those sources lock encode+submit with a
 * recursive Unikraft mutex (uk/uklock). The native harness runs each test
 * binary single-threaded and serially, so the lock has no contention to
 * guard here; this shim provides the same uk_mutex API as
 * unikraft/lib/uklock/include/uk/mutex.h reduced to a no-op recursive lock
 * so the real guest code compiles and links unchanged.
 *
 * Only the subset the guest code uses is provided:
 *   struct uk_mutex, UK_MUTEX_INITIALIZER_RECURSIVE,
 *   uk_mutex_init, uk_mutex_lock, uk_mutex_unlock, uk_mutex_trylock.
 */
#ifndef _TESTS_SHIM_UK_MUTEX_H_
#define _TESTS_SHIM_UK_MUTEX_H_

struct uk_mutex {
	int locks; /* recursion depth; advisory only on the host */
};

#define UK_MUTEX_INITIALIZER(name)           { 0 }
#define UK_MUTEX_INITIALIZER_RECURSIVE(name) { 0 }

static inline void uk_mutex_init(struct uk_mutex *m)       { m->locks = 0; }
static inline void uk_mutex_init_config(struct uk_mutex *m, unsigned int f)
{
	(void)f;
	m->locks = 0;
}
static inline void uk_mutex_lock(struct uk_mutex *m)       { m->locks++; }
static inline void uk_mutex_unlock(struct uk_mutex *m)     { m->locks--; }
static inline int  uk_mutex_trylock(struct uk_mutex *m)    { m->locks++; return 1; }
static inline int  uk_mutex_is_locked(struct uk_mutex *m)  { return m->locks > 0; }

#endif /* _TESTS_SHIM_UK_MUTEX_H_ */
