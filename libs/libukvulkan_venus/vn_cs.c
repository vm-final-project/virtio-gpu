/*
 * Mesa alignment:
 *   Analogue: src/virtio/venus-protocol/vn_protocol_driver*.h.
 *   Same: use generated vn_encode_* helpers as the Venus wire-format source.
 *   VOGUE adaptation: scalar helper wrappers for the current Unikraft callers.
 */
/*
 * libukvulkan_venus — Venus command-stream encoder.
 *
 * Implements the Venus wire format as specified by:
 *   mesa/src/virtio/venus-protocol/vn_protocol_driver_defines.h
 *   mesa/src/virtio/venus-protocol/vn_protocol_driver_instance.h
 *
 * Each Vulkan call is serialised as:
 *   [4-byte VkCommandTypeEXT] [packed fields...]
 *
 * String encoding: [8-byte length incl. NUL] [bytes] or [8-byte 0] for NULL.
 * Pointer encoding: [uint64 1] present / [uint64 0] absent, then the pointed-to data.
 * Array encoding: [uint64 count] followed by elements (0 = absent/NULL array).
 * All multi-byte values are little-endian.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <uk/venus.h>
#include <uk/vn_cs.h>
#include "vn_protocol_driver.h"

#define H(T, id) ((T)(uintptr_t)(id))
#define ENC(arg) struct vn_cs_encoder _vn = { .e = (arg) }

int uk_venus_encoder_init(struct uk_venus_encoder *enc, void *buf, size_t cap)
{
	if (!enc || !buf || !cap)
		return -EINVAL;
	enc->buf      = (uint8_t *)buf;
	enc->cap      = cap;
	enc->pos      = 0;
	enc->overflow = 0;
	return 0;
}

void uk_venus_encoder_reset(struct uk_venus_encoder *enc)
{
	if (enc) {
		enc->pos      = 0;
		enc->overflow = 0;
	}
}

size_t uk_venus_encoder_size(const struct uk_venus_encoder *enc)
{
	return enc ? enc->pos : 0;
}

int uk_venus_encoder_overflow(const struct uk_venus_encoder *enc)
{
	return enc ? enc->overflow : 1;
}

void uk_venus_encode_bytes(struct uk_venus_encoder *enc,
			   const void *src, size_t len)
{
	if (!enc || enc->overflow || !len)
		return;
	if (enc->pos + len > enc->cap) {
		enc->overflow = 1;
		return;
	}
	if (src)
		memcpy(enc->buf + enc->pos, src, len);
	else
		memset(enc->buf + enc->pos, 0, len);
	enc->pos += len;
}

/* Fast path: small fixed-size scalars skip the memcpy + length tracking that
 * uk_venus_encode_bytes carries. The Venus wire format is packed (no
 * alignment padding) so an unaligned 32-bit store is the correct emission.
 * Bounds-check inline; on overflow fall through to the generic path so the
 * encoder's overflow flag still trips. */
void uk_venus_encode_uint32(struct uk_venus_encoder *enc, uint32_t v)
{
	if (!enc || enc->overflow)
		return;
	if (enc->pos + sizeof(v) > enc->cap) {
		enc->overflow = 1;
		return;
	}
	__builtin_memcpy(enc->buf + enc->pos, &v, sizeof(v));
	enc->pos += sizeof(v);
}

void uk_venus_encode_uint64(struct uk_venus_encoder *enc, uint64_t v)
{
	if (!enc || enc->overflow)
		return;
	if (enc->pos + sizeof(v) > enc->cap) {
		enc->overflow = 1;
		return;
	}
	__builtin_memcpy(enc->buf + enc->pos, &v, sizeof(v));
	enc->pos += sizeof(v);
}

void uk_venus_encode_cstring(struct uk_venus_encoder *enc, const char *s)
{
	if (!s) {
		uk_venus_encode_uint64(enc, 0);
		return;
	}
	size_t n = strlen(s) + 1;
	uk_venus_encode_uint64(enc, (uint64_t)n); /* exact count incl. NUL */
	uk_venus_encode_bytes(enc, s, n);
	/* pad blob to 4-byte boundary (matches Mesa vn_encode_blob_array) */
	size_t rem = n & 3;
	if (rem) {
		uint32_t z = 0;
		uk_venus_encode_bytes(enc, &z, 4 - rem);
	}
}

void uk_venus_encode_pointer_flag(struct uk_venus_encoder *enc, int present)
{
	uk_venus_encode_uint64(enc, present ? VN_PTR_PRESENT : VN_PTR_NULL);
}

/* ggml uses a single physical device; bound the marshalling array. */
#define UK_VENUS_MAX_PHYSDEV 16u

/* ── Bootstrap + transport commands (delegated to generated Mesa encoders) ── */

void uk_venus_encode_vkCreateInstance(struct uk_venus_encoder *enc,
				      uint64_t instance_handle,
				      const char *app_name,
				      uint32_t api_version,
				      uint32_t n_extensions,
				      const char * const *extensions)
{
	ENC(enc);
	VkApplicationInfo ai = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = app_name,
		.applicationVersion = 0u,
		.pEngineName = "unikraft-venus",
		.engineVersion = 1u,
		.apiVersion = api_version,
	};
	VkInstanceCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &ai,
		.enabledExtensionCount = n_extensions,
		.ppEnabledExtensionNames = (n_extensions && extensions) ? extensions : NULL,
	};
	VkInstance out = H(VkInstance, instance_handle);
	vn_encode_vkCreateInstance(&_vn, 0, &ci, NULL, &out);
}

void uk_venus_encode_vkEnumeratePhysicalDevices(struct uk_venus_encoder *enc,
						uint64_t instance_handle,
						uint32_t max_devices,
						const uint64_t *device_handles)
{
	ENC(enc);
	VkPhysicalDevice pd[UK_VENUS_MAX_PHYSDEV];
	uint32_t count = max_devices;
	if (count > UK_VENUS_MAX_PHYSDEV)
		count = UK_VENUS_MAX_PHYSDEV;
	for (uint32_t i = 0; i < count; i++)
		pd[i] = device_handles ? H(VkPhysicalDevice, device_handles[i])
				       : VK_NULL_HANDLE;
	vn_encode_vkEnumeratePhysicalDevices(&_vn, 0, H(VkInstance, instance_handle),
					     &count,
					     (count && device_handles) ? pd : NULL);
}

void uk_venus_encode_vkCreateDevice(struct uk_venus_encoder *enc,
				    uint64_t physical_device_handle,
				    uint64_t device_handle,
				    uint32_t queue_family_index,
				    float queue_priority,
				    uint32_t n_extensions,
				    const char * const *extensions)
{
	ENC(enc);
	VkDeviceQueueCreateInfo qci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = queue_family_index,
		.queueCount = 1u,
		.pQueuePriorities = &queue_priority,
	};
	VkDeviceCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1u,
		.pQueueCreateInfos = &qci,
		.enabledExtensionCount = n_extensions,
		.ppEnabledExtensionNames = (n_extensions && extensions) ? extensions : NULL,
	};
	VkDevice out = H(VkDevice, device_handle);
	vn_encode_vkCreateDevice(&_vn, 0, H(VkPhysicalDevice, physical_device_handle),
				 &ci, NULL, &out);
}

void uk_venus_encode_vkGetDeviceQueue(struct uk_venus_encoder *enc,
				      uint64_t device_handle,
				      uint32_t queue_family_index,
				      uint32_t queue_index,
				      uint64_t queue_handle)
{
	ENC(enc);
	VkQueue out = H(VkQueue, queue_handle);
	vn_encode_vkGetDeviceQueue(&_vn, 0, H(VkDevice, device_handle),
				   queue_family_index, queue_index, &out);
}

void uk_venus_encode_vkGetDeviceQueue2(struct uk_venus_encoder *enc,
				       uint64_t device_handle,
				       uint32_t queue_family_index,
				       uint32_t queue_index,
				       uint32_t ring_idx,
				       uint64_t queue_handle)
{
	ENC(enc);
	VkDeviceQueueTimelineInfoMESA tl = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_TIMELINE_INFO_MESA,
		.pNext = NULL,
		.ringIdx = ring_idx,
	};
	VkDeviceQueueInfo2 qi = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
		.pNext = &tl,
		.flags = 0,
		.queueFamilyIndex = queue_family_index,
		.queueIndex = queue_index,
	};
	VkQueue out = H(VkQueue, queue_handle);
	vn_encode_vkGetDeviceQueue2(&_vn, 0, H(VkDevice, device_handle), &qi, &out);
}

void uk_venus_encode_vkQueueSubmit_empty(struct uk_venus_encoder *enc,
					 uint64_t queue_handle,
					 uint64_t fence_handle)
{
	ENC(enc);
	vn_encode_vkQueueSubmit(&_vn, 0, H(VkQueue, queue_handle), 0, NULL,
				H(VkFence, fence_handle));
}

void uk_venus_encode_vkSetReplyCommandStreamMESA(struct uk_venus_encoder *enc,
						 uint32_t resource_id,
						 uint64_t offset,
						 uint64_t size)
{
	ENC(enc);
	VkCommandStreamDescriptionMESA s = {
		.resourceId = resource_id,
		.offset = offset,
		.size = size,
	};
	vn_encode_vkSetReplyCommandStreamMESA(&_vn, 0, &s);
}

void uk_venus_encode_vkGetPhysicalDeviceProperties(struct uk_venus_encoder *enc,
						   uint64_t physdev_handle)
{
	ENC(enc);
	VkPhysicalDeviceProperties props = {0};
	vn_encode_vkGetPhysicalDeviceProperties(&_vn,
		VK_COMMAND_GENERATE_REPLY_BIT_EXT,
		H(VkPhysicalDevice, physdev_handle), &props);
}

void uk_venus_encode_vkGetPhysicalDeviceMemoryProperties(struct uk_venus_encoder *enc,
							 uint64_t physdev_handle)
{
	ENC(enc);
	VkPhysicalDeviceMemoryProperties props = {0};
	vn_encode_vkGetPhysicalDeviceMemoryProperties(&_vn,
		VK_COMMAND_GENERATE_REPLY_BIT_EXT,
		H(VkPhysicalDevice, physdev_handle), &props);
}

void uk_venus_encode_vkCreateRingMESA(struct uk_venus_encoder *enc,
				      uint64_t ring_id,
				      uint32_t resource_id,
				      uint64_t blob_size,
				      uint64_t buf_size)
{
	ENC(enc);
	VkRingCreateInfoMESA ci = {
		.sType = VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA,
		.pNext = NULL,
		.flags = 0,
		.resourceId = resource_id,
		.offset = 0,
		.size = blob_size,
		.idleTimeout = 0,
		.headOffset = UK_VENUS_RING_HEAD_OFFSET,
		.tailOffset = UK_VENUS_RING_TAIL_OFFSET,
		.statusOffset = UK_VENUS_RING_STATUS_OFFSET,
		.bufferOffset = UK_VENUS_RING_BUFFER_OFFSET,
		.bufferSize = buf_size,
		.extraOffset = (size_t)UK_VENUS_RING_BUFFER_OFFSET + buf_size,
		.extraSize = 0,
	};
	vn_encode_vkCreateRingMESA(&_vn, 0, ring_id, &ci);
}

void uk_venus_encode_vkDestroyRingMESA(struct uk_venus_encoder *enc,
				       uint64_t ring_id)
{
	ENC(enc);
	vn_encode_vkDestroyRingMESA(&_vn, 0, ring_id);
}

void uk_venus_encode_vkNotifyRingMESA(struct uk_venus_encoder *enc,
				      uint64_t ring_id,
				      uint32_t seqno)
{
	ENC(enc);
	vn_encode_vkNotifyRingMESA(&_vn, 0, ring_id, seqno, 0);
}
