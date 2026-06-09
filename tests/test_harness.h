/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include <stdio.h>

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

