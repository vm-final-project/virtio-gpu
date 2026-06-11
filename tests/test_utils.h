/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <uk/virtio_gpu.h>

struct test_state {
	const char *suite;
	unsigned checks;
	unsigned failures;
};

static inline void test_check_impl(struct test_state *state,
				   const char *label,
				   int ok,
				   const char *file,
				   int line)
{
	state->checks++;
	if (ok) {
		printf("  PASS  %s\n", label);
		return;
	}

	state->failures++;
	printf("  FAIL  %s  at %s:%d\n", label, file, line);
}

static inline int test_finish(struct test_state *state)
{
	if (state->failures == 0) {
		printf("%s: PASS checks=%u\n", state->suite, state->checks);
		return 0;
	}

	printf("%s: FAIL failures=%u/%u\n",
	       state->suite,
	       state->failures,
	       state->checks);
	return 1;
}

#define TEST_CHECK(state, label, cond) \
	test_check_impl((state), (label), !!(cond), __FILE__, __LINE__)

/* Probe the fake VirtIO-GPU device, recording the result as a check. Returns
 * the device on success or NULL on failure (callers should test_finish early). */
static inline struct uk_virtio_gpu_dev *test_device_init(struct test_state *state)
{
	struct uk_virtio_gpu_dev *dev = NULL;

	TEST_CHECK(state, "probe fake device",
		   uk_virtio_gpu_probe(&dev) == 0 && dev != NULL);
	return dev;
}

static inline void test_device_cleanup(struct uk_virtio_gpu_dev *dev)
{
	free(dev);
}

/* Create a context on the fake device, recording the result as a check.
 * Returns the context by value (zeroed if creation fails). */
static inline struct uk_virtio_gpu_context
test_context_create(struct test_state *state, struct uk_virtio_gpu_dev *dev,
		     uint32_t capset_id, const char *debug_name)
{
	struct uk_virtio_gpu_context ctx = { 0 };

	TEST_CHECK(state, "context create",
		   uk_virtio_gpu_gl_context_create(dev, capset_id, debug_name, &ctx) == 0);
	return ctx;
}

static inline void test_context_destroy(struct test_state *state,
					struct uk_virtio_gpu_dev *dev,
					struct uk_virtio_gpu_context *ctx)
{
	TEST_CHECK(state, "context destroy",
		   uk_virtio_gpu_gl_context_destroy(dev, ctx) == 0);
}
