/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Mesa alignment:
 *   Analogue: src/virtio/venus-protocol/vn_protocol_driver*.h compute/object
 *             command encoders.
 *   Same: build real Vk* structs and call generated vn_encode_vk* helpers.
 *   VOGUE adaptation: expose a compact scalar API for ggml-vulkan.
 */
/*
 * libukvulkan_venus — Venus compute dispatch encoder (venus_compute.c)
 *
 * Thin bridge from VOGUE's scalar `uk_venus_encode_*` API onto the encoders
 * generated from the pinned Mesa `../venus-protocol` (see GENERATOR.md). Each
 * function builds the real `Vk*` struct(s) from its scalar arguments and calls
 * the generated `vn_encode_vk*`, so the in-image wire format IS the Mesa Venus
 * format (no hand-rolled byte layout here). Vulkan handles are bare uint64
 * guest IDs (`id == (uintptr_t)handle`, per the vn_cs.h shim).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <uk/venus.h>
#include <uk/vn_cs.h>
#include "vn_protocol_driver.h"

/* ggml descriptor sets / pipeline layouts are small; these caps bound the
 * temporary handle/struct arrays used to marshal scalar inputs. */
#define UK_VENUS_MAX_BINDINGS 64u

#define H(T, id) ((T)(uintptr_t)(id))
#define ENC(arg) struct vn_cs_encoder _vn = { .e = (arg) }

/* ── Fences ─────────────────────────────────────────────────────────────── */

void uk_venus_encode_vkCreateFence(struct uk_venus_encoder *enc,
				   uint64_t device, uint64_t fence_handle,
				   int signaled)
{
	ENC(enc);
	VkFenceCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u,
	};
	VkFence out = H(VkFence, fence_handle);
	vn_encode_vkCreateFence(&_vn, 0, H(VkDevice, device), &ci, NULL, &out);
}

void uk_venus_encode_vkResetFences(struct uk_venus_encoder *enc,
				   uint64_t device,
				   uint32_t n_fences,
				   const uint64_t *fences)
{
	ENC(enc);
	VkFence f[UK_VENUS_MAX_BINDINGS];
	if (n_fences > UK_VENUS_MAX_BINDINGS)
		n_fences = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_fences; i++)
		f[i] = H(VkFence, fences[i]);
	vn_encode_vkResetFences(&_vn, 0, H(VkDevice, device), n_fences,
				n_fences ? f : NULL);
}

void uk_venus_encode_vkWaitForFences(struct uk_venus_encoder *enc,
				     uint64_t device,
				     uint32_t n_fences,
				     const uint64_t *fences,
				     uint64_t timeout_ns)
{
	ENC(enc);
	VkFence f[UK_VENUS_MAX_BINDINGS];
	if (n_fences > UK_VENUS_MAX_BINDINGS)
		n_fences = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_fences; i++)
		f[i] = H(VkFence, fences[i]);
	vn_encode_vkWaitForFences(&_vn, 0, H(VkDevice, device), n_fences,
				  n_fences ? f : NULL, VK_TRUE, timeout_ns);
}

/* ── Queue submit with command buffers ─────────────────────────────────── */

void uk_venus_encode_vkQueueSubmit(struct uk_venus_encoder *enc,
				   uint64_t queue,
				   uint32_t n_cmd_bufs,
				   const uint64_t *cmd_bufs,
				   uint64_t fence)
{
	ENC(enc);
	VkCommandBuffer cb[UK_VENUS_MAX_BINDINGS];
	if (n_cmd_bufs > UK_VENUS_MAX_BINDINGS)
		n_cmd_bufs = UK_VENUS_MAX_BINDINGS;
	for (uint32_t i = 0; i < n_cmd_bufs; i++)
		cb[i] = H(VkCommandBuffer, cmd_bufs[i]);
	VkSubmitInfo si = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = n_cmd_bufs,
		.pCommandBuffers = n_cmd_bufs ? cb : NULL,
	};
	vn_encode_vkQueueSubmit(&_vn, 0, H(VkQueue, queue), 1, &si,
				H(VkFence, fence));
}

void uk_venus_encode_vkQueueWaitIdle(struct uk_venus_encoder *enc,
				     uint64_t queue)
{
	ENC(enc);
	vn_encode_vkQueueWaitIdle(&_vn, 0, H(VkQueue, queue));
}
