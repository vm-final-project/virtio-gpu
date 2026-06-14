#define _GNU_SOURCE

#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uk/config.h>
#if CONFIG_LIBUKSCHEDCOOP_SMP
#include <uk/schedcoop.h>
#endif

#define DEFAULT_ROUNDS 100000U
struct worker_ctx {
	unsigned int worker;
	unsigned int rounds;
	unsigned int *ready_count;
	bool *start_flag;
	bool *failed;
	unsigned int progress;
	unsigned int actual;
	int error;
};

static inline void ready_inc(unsigned int *value)
{
	__atomic_add_fetch(value, 1U, __ATOMIC_RELEASE);
}

static inline unsigned int ready_load(const unsigned int *value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline void bool_store(bool *value, bool next)
{
	__atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static inline bool bool_load(const bool *value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static bool argv_get_uint(int argc, char **argv, const char *prefix,
			  unsigned int *value)
{
	size_t prefix_len = strlen(prefix);
	char *end = NULL;
	unsigned long parsed;

	for (int i = 0; i < argc; i++) {
		if (!argv || !argv[i] || strncmp(argv[i], prefix, prefix_len))
			continue;
		parsed = strtoul(argv[i] + prefix_len, &end, 10);
		if (!end || *end != '\0' || parsed == 0 || parsed > UINT32_MAX)
			return false;
		*value = (unsigned int) parsed;
		return true;
	}

	return false;
}

static unsigned int online_workers(void)
{
#if CONFIG_LIBUKSCHEDCOOP_SMP
	unsigned int count = uk_schedcoop_smp_online_count();

	return count ? count : 1U;
#else
	return 1U;
#endif
}

static void print_fail(const char *reason)
{
	printf("pthread-affinity: FAIL reason=%s\n", reason);
	fflush(stdout);
}

static int sample_worker(struct worker_ctx *ctx)
{
	unsigned int actual = 0;

	ready_inc(ctx->ready_count);
	printf("pthread-affinity: debug ready worker=%u count=%u\n",
	       ctx->worker, ready_load(ctx->ready_count));
	fflush(stdout);
	while (!bool_load(ctx->start_flag) && !bool_load(ctx->failed))
		sched_yield();

	for (unsigned int i = 0; i < ctx->rounds; i++) {
		actual = (unsigned int) sched_getcpu();
		__atomic_store_n(&ctx->progress, i + 1U, __ATOMIC_RELAXED);
		if (actual != ctx->worker) {
			ctx->actual = actual;
			printf("pthread-affinity: worker=%u requested=%#lx actual=%u samples=%u\n",
			       ctx->worker, 1UL << ctx->worker, actual, i + 1U);
			print_fail("wrong-lcpu");
			bool_store(ctx->failed, true);
			return -1;
		}
	}

	ctx->actual = actual;
	printf("pthread-affinity: worker=%u requested=%#lx actual=%u samples=%u\n",
	       ctx->worker, 1UL << ctx->worker, ctx->actual, ctx->rounds);
	fflush(stdout);
	return 0;
}

static void *worker_main(void *arg)
{
	struct worker_ctx *ctx = arg;

	printf("pthread-affinity: debug worker-start worker=%u tid=%d\n",
	       ctx->worker, (int)gettid());
	fflush(stdout);
	(void) sample_worker(ctx);
	return NULL;
}

int main(int argc, char **argv)
{
	unsigned int rounds = DEFAULT_ROUNDS;
	unsigned int workers;
	pthread_t *threads = NULL;
	struct worker_ctx *ctxs = NULL;
	unsigned int ready_count = 0;
	bool start_flag = false;
	bool failed = false;
	int rc = 1;

	if (!argv_get_uint(argc, argv, "pthread_affinity_rounds=", &rounds))
		rounds = DEFAULT_ROUNDS;

	workers = online_workers();
	threads = calloc(workers > 1 ? workers - 1 : 1, sizeof(*threads));
	ctxs = calloc(workers, sizeof(*ctxs));
	if (!threads || !ctxs) {
		print_fail("oom");
		goto out;
	}

	for (unsigned int i = 0; i < workers; i++) {
		ctxs[i].worker = i;
		ctxs[i].rounds = rounds;
		ctxs[i].ready_count = &ready_count;
		ctxs[i].start_flag = &start_flag;
		ctxs[i].failed = &failed;
		ctxs[i].progress = 0U;
	}

	for (unsigned int i = 1; i < workers; i++) {
		if (pthread_create(&threads[i - 1], NULL, worker_main, &ctxs[i])) {
			print_fail("pthread-create");
			bool_store(&failed, true);
			goto join_threads;
		}
	}

	ready_inc(&ready_count);
	printf("pthread-affinity: debug ready worker=0 count=%u workers=%u\n",
	       ready_load(&ready_count), workers);
	fflush(stdout);
	while (ready_load(&ready_count) < workers && !bool_load(&failed))
		sched_yield();

	printf("pthread-affinity: debug start count=%u failed=%d\n",
	       ready_load(&ready_count), bool_load(&failed));
	fflush(stdout);
	bool_store(&start_flag, true);
	if (sample_worker(&ctxs[0]))
		goto join_threads;

join_threads:
	for (unsigned int i = 1; i < workers; i++)
		pthread_join(threads[i - 1], NULL);

	if (!bool_load(&failed)) {
		printf("pthread-affinity: PASS workers=%u distinct=%u\n",
		       workers, workers);
		rc = 0;
	}

out:
	free(threads);
	free(ctxs);
	return rc;
}
