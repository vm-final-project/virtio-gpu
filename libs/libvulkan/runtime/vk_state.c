/* SPDX-License-Identifier: MIT */
/*
 * Mesa alignment:
 *   Analogue: src/vulkan/runtime object/device state helpers.
 *   Same: keep object ids and lifetime flags in a state object.
 *   VOGUE adaptation: singleton state for the static ggml-vulkan ABI subset.
 */
#include <errno.h>
#include <string.h>

#include "vk_state.h"

uint64_t uk_vulkan_state_alloc_handle(struct uk_vulkan_state *s)
{
	if (!s)
		return 0;
	if (!s->next_handle)
		s->next_handle = UK_VULKAN_STATE_HANDLE_FIRST_DYNAMIC;
	return __atomic_fetch_add(&s->next_handle, 1, __ATOMIC_SEQ_CST);
}

int uk_vulkan_state_init(struct uk_vulkan_state *s)
{
	if (!s)
		return -EINVAL;
	if (s->initialized)
		return 0;
	memset(s, 0, sizeof(*s));
	s->next_handle = UK_VULKAN_STATE_HANDLE_FIRST_DYNAMIC;
	s->instance = 0x0002000000000001ULL;
	s->physical_device = 0x0002000000000002ULL;
	s->device = 0x0002000000000003ULL;
	s->queue = 0x0002000000000004ULL;
	s->initialized = 1;
	return 0;
}

void uk_vulkan_state_fini(struct uk_vulkan_state *s)
{
	if (!s)
		return;
	memset(s, 0, sizeof(*s));
}
