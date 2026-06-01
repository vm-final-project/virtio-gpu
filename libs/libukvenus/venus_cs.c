/*
 * libukvenus — Venus command-stream encoder.
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

/* size_t on 64-bit is 8 bytes; Mesa's vn_encode_size_t always uses uint64. */
void uk_venus_encode_size(struct uk_venus_encoder *enc, uint64_t v)
{
	uk_venus_encode_uint64(enc, v);
}

/*
 * uk_venus_encode_array_size — encode an array presence/count field.
 *
 * Mesa's vn_encode_array_size() writes uint64_t.  A value of 0 means the
 * array pointer is absent/NULL; a non-zero value is the element count.
 */
void uk_venus_encode_array_size(struct uk_venus_encoder *enc, uint64_t n)
{
	uk_venus_encode_uint64(enc, n);
}

void uk_venus_encode_float32(struct uk_venus_encoder *enc, float v)
{
	uint32_t bits;
	memcpy(&bits, &v, sizeof(bits));
	uk_venus_encode_uint32(enc, bits);
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

void uk_venus_encode_command_header(struct uk_venus_encoder *enc,
				    uint32_t cmd_type, uint32_t flags)
{
	uk_venus_encode_uint32(enc, cmd_type);
	uk_venus_encode_uint32(enc, flags);
}

/*
 * VkApplicationInfo encoding (mirrors vn_encode_VkApplicationInfo):
 *   sType=2, pNext=NULL, pApplicationName, appVersion,
 *   pEngineName, engineVersion, apiVersion
 */
void uk_venus_encode_VkApplicationInfo(struct uk_venus_encoder *enc,
				       const char *app_name,
				       uint32_t app_version,
				       const char *engine_name,
				       uint32_t engine_version,
				       uint32_t api_version)
{
	/* sType = VK_STRUCTURE_TYPE_APPLICATION_INFO = 0 */
	uk_venus_encode_uint32(enc, 0u);
	/* pNext = NULL */
	uk_venus_encode_pointer_flag(enc, 0);
	/* pApplicationName */
	uk_venus_encode_cstring(enc, app_name);
	uk_venus_encode_uint32(enc, app_version);
	/* pEngineName */
	uk_venus_encode_cstring(enc, engine_name);
	uk_venus_encode_uint32(enc, engine_version);
	uk_venus_encode_uint32(enc, api_version);
}

/*
 * vkCreateInstance encoding:
 *   cmd_type=0, pCreateInfo, pAllocator=NULL, pInstance
 *
 * VkInstanceCreateInfo:
 *   sType=1, pNext=NULL, flags=0, pApplicationInfo, layers, extensions
 */
void uk_venus_encode_vkCreateInstance(struct uk_venus_encoder *enc,
				      uint64_t instance_handle,
				      const char *app_name,
				      uint32_t api_version,
				      uint32_t n_extensions,
				      const char * const *extensions)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkCreateInstance,
				       VN_COMMAND_FLAGS_NONE);

	/* pCreateInfo pointer present */
	uk_venus_encode_pointer_flag(enc, 1);

	/* VkInstanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1 */
	uk_venus_encode_uint32(enc, 1u);
	/* pNext = NULL */
	uk_venus_encode_pointer_flag(enc, 0);
	/* flags = 0 */
	uk_venus_encode_uint32(enc, 0u);

	/* pApplicationInfo: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_VkApplicationInfo(enc, app_name, 0u, "unikraft-venus", 1u,
					  api_version);

	/* enabledLayerCount = 0, ppEnabledLayerNames = NULL (array_size = uint64) */
	uk_venus_encode_uint32(enc, 0u);
	uk_venus_encode_array_size(enc, 0);

	/* enabledExtensionCount, ppEnabledExtensionNames (array_size = uint64) */
	uk_venus_encode_uint32(enc, n_extensions);
	if (n_extensions && extensions) {
		uk_venus_encode_array_size(enc, n_extensions);
		for (uint32_t i = 0; i < n_extensions; i++)
			uk_venus_encode_cstring(enc, extensions[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}

	/* pAllocator = NULL */
	uk_venus_encode_pointer_flag(enc, 0);

	/* pInstance: present (pointer flag) + handle */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, instance_handle);
}

/*
 * vkEnumeratePhysicalDevices encoding:
 *   cmd_type=2, instance, pPhysicalDeviceCount, pPhysicalDevices
 */
void uk_venus_encode_vkEnumeratePhysicalDevices(struct uk_venus_encoder *enc,
						uint64_t instance_handle,
						uint32_t max_devices,
						const uint64_t *device_handles)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkEnumeratePhysicalDevices,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, instance_handle);

	/* pPhysicalDeviceCount: present */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, max_devices);

	/* pPhysicalDevices: array_size (uint64) then handle values */
	if (max_devices && device_handles) {
		uk_venus_encode_array_size(enc, max_devices);
		for (uint32_t i = 0; i < max_devices; i++)
			uk_venus_encode_uint64(enc, device_handles[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}
}

/*
 * vkCreateDevice encoding for the minimal queue + extension smoke path.
 * Mirrors Mesa venus-protocol's generated vn_encode_vkCreateDevice shape:
 * command header, physicalDevice, pCreateInfo, pAllocator=NULL, pDevice.
 */
void uk_venus_encode_vkCreateDevice(struct uk_venus_encoder *enc,
				    uint64_t physical_device_handle,
				    uint64_t device_handle,
				    uint32_t queue_family_index,
				    float queue_priority,
				    uint32_t n_extensions,
				    const char * const *extensions)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkCreateDevice,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, physical_device_handle);

	/* pCreateInfo present; VkDeviceCreateInfo.sType = 3 */
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint32(enc, 3u);
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* flags */

	/* one VkDeviceQueueCreateInfo; sType = 2 */
	uk_venus_encode_uint32(enc, 1u);      /* queueCreateInfoCount */
	uk_venus_encode_array_size(enc, 1u);  /* pQueueCreateInfos array size (uint64) */
	uk_venus_encode_uint32(enc, 2u);      /* VkDeviceQueueCreateInfo.sType */
	uk_venus_encode_pointer_flag(enc, 0); /* pNext */
	uk_venus_encode_uint32(enc, 0u);      /* queue flags */
	uk_venus_encode_uint32(enc, queue_family_index);
	uk_venus_encode_uint32(enc, 1u);      /* queueCount */
	uk_venus_encode_array_size(enc, 1u);  /* pQueuePriorities array size (uint64) */
	uk_venus_encode_float32(enc, queue_priority);

	/* layers unsupported; extensions are optional */
	uk_venus_encode_uint32(enc, 0u);
	uk_venus_encode_array_size(enc, 0);   /* ppEnabledLayerNames (uint64) */
	uk_venus_encode_uint32(enc, n_extensions);
	if (n_extensions && extensions) {
		uk_venus_encode_array_size(enc, n_extensions); /* ppEnabledExtensionNames (uint64) */
		for (uint32_t i = 0; i < n_extensions; i++)
			uk_venus_encode_cstring(enc, extensions[i]);
	} else {
		uk_venus_encode_array_size(enc, 0);
	}

	uk_venus_encode_pointer_flag(enc, 0); /* pEnabledFeatures */
	uk_venus_encode_pointer_flag(enc, 0); /* pAllocator */
	uk_venus_encode_pointer_flag(enc, 1); /* pDevice */
	uk_venus_encode_uint64(enc, device_handle);
}

void uk_venus_encode_vkGetDeviceQueue(struct uk_venus_encoder *enc,
				      uint64_t device_handle,
				      uint32_t queue_family_index,
				      uint32_t queue_index,
				      uint64_t queue_handle)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkGetDeviceQueue,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device_handle);
	uk_venus_encode_uint32(enc, queue_family_index);
	uk_venus_encode_uint32(enc, queue_index);
	uk_venus_encode_pointer_flag(enc, 1);
	uk_venus_encode_uint64(enc, queue_handle);
}

/*
 * vkSetReplyCommandStreamMESA (VN_CMD = 178) — tell the host where to write the
 * reply stream for subsequent reply-bearing commands in the same submission.
 * Mirrors Mesa vn_encode_vkSetReplyCommandStreamMESA + VkCommandStreamDescriptionMESA:
 *   [u32 cmd_type=178][u32 flags=0]
 *   [u64 pStream present=1]
 *     [u32 resourceId][u64 offset][u64 size]   (size_t encoded as u64)
 */
void uk_venus_encode_vkSetReplyCommandStreamMESA(struct uk_venus_encoder *enc,
						 uint32_t resource_id,
						 uint64_t offset,
						 uint64_t size)
{
	uk_venus_encode_command_header(enc,
				       (uint32_t)VN_CMD_vkSetReplyCommandStreamMESA,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_pointer_flag(enc, 1);   /* pStream present */
	uk_venus_encode_uint32(enc, resource_id);
	uk_venus_encode_uint64(enc, offset);    /* size_t */
	uk_venus_encode_uint64(enc, size);      /* size_t */
}

/*
 * vkGetPhysicalDeviceProperties (VN_CMD = 6) request form. Mirrors Mesa
 * vn_encode_vkGetPhysicalDeviceProperties: header, physicalDevice, then the
 * output pointer. The _partial body for VkPhysicalDeviceProperties is empty
 * (all limits/sparse fields are skipped on the request), so nothing follows the
 * pointer flag. The host writes the full VkPhysicalDeviceProperties into the
 * reply stream set by vkSetReplyCommandStreamMESA.
 */
void uk_venus_encode_vkGetPhysicalDeviceProperties(struct uk_venus_encoder *enc,
						   uint64_t physdev_handle)
{
	/* VK_COMMAND_GENERATE_REPLY_BIT_EXT (0x1): the host only writes the reply
	 * stream for commands whose flags request it (vn_dispatch_*). */
	uk_venus_encode_command_header(enc,
				       (uint32_t)VN_CMD_vkGetPhysicalDeviceProperties,
				       0x1u /* VK_COMMAND_GENERATE_REPLY_BIT_EXT */);
	uk_venus_encode_uint64(enc, physdev_handle);
	uk_venus_encode_pointer_flag(enc, 1);   /* pProperties present; partial=0 bytes */
}

/*
 * vkGetPhysicalDeviceMemoryProperties (VN_CMD = 8) request form. Mirrors Mesa
 * vn_encode_vkGetPhysicalDeviceMemoryProperties + the _partial body of
 * VkPhysicalDeviceMemoryProperties: two array sizes (VK_MAX_MEMORY_TYPES=32,
 * VK_MAX_MEMORY_HEAPS=16); the per-element partials are empty. The
 * GENERATE_REPLY flag tells the host to write the reply stream.
 */
void uk_venus_encode_vkGetPhysicalDeviceMemoryProperties(struct uk_venus_encoder *enc,
							 uint64_t physdev_handle)
{
	uk_venus_encode_command_header(enc,
				       (uint32_t)VN_CMD_vkGetPhysicalDeviceMemoryProperties,
				       0x1u /* VK_COMMAND_GENERATE_REPLY_BIT_EXT */);
	uk_venus_encode_uint64(enc, physdev_handle);
	uk_venus_encode_pointer_flag(enc, 1);     /* pMemoryProperties present */
	uk_venus_encode_array_size(enc, 32u);     /* VK_MAX_MEMORY_TYPES */
	uk_venus_encode_array_size(enc, 16u);     /* VK_MAX_MEMORY_HEAPS */
}

/*
 * vkGetDeviceQueue2 (VN_CMD = 155). The Venus host (virglrenderer
 * vkr_dispatch_vkGetDeviceQueue) REQUIRES vkGetDeviceQueue2 — the legacy
 * vkGetDeviceQueue unconditionally sets the context fatal. The pQueueInfo MUST
 * carry a VkDeviceQueueTimelineInfoMESA in its pNext with a non-zero ringIdx
 * (vkr_queue_assign_ring_idx), binding the queue to a host sync ring.
 *
 * Mirrors Mesa vn_encode_vkGetDeviceQueue2 + VkDeviceQueueInfo2(_pnext/_self) +
 * VkDeviceQueueTimelineInfoMESA_self:
 *   [u32 cmd=155][u32 flags=0][u64 device]
 *   [u64 pQueueInfo present=1]
 *     [u32 sType=DEVICE_QUEUE_INFO_2 1000145003]
 *     pnext chain:
 *       [u64 present=1][u32 sType=DEVICE_QUEUE_TIMELINE_INFO_MESA 1000384005]
 *       [u64 timeline.pNext=NULL 0][u32 ringIdx]
 *     self: [u32 flags][u32 queueFamilyIndex][u32 queueIndex]
 *   [u64 pQueue present=1][u64 queue_handle]
 */
void uk_venus_encode_vkGetDeviceQueue2(struct uk_venus_encoder *enc,
				       uint64_t device_handle,
				       uint32_t queue_family_index,
				       uint32_t queue_index,
				       uint32_t ring_idx,
				       uint64_t queue_handle)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkGetDeviceQueue2,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, device_handle);
	uk_venus_encode_pointer_flag(enc, 1);            /* pQueueInfo present */
	uk_venus_encode_uint32(enc, 1000145003u);        /* sType DEVICE_QUEUE_INFO_2 */
	/* pNext = VkDeviceQueueTimelineInfoMESA */
	uk_venus_encode_pointer_flag(enc, 1);            /* timeline present */
	uk_venus_encode_uint32(enc, 1000384005u);        /* sType TIMELINE_INFO_MESA */
	uk_venus_encode_pointer_flag(enc, 0);            /* timeline.pNext = NULL */
	uk_venus_encode_uint32(enc, ring_idx);           /* ringIdx (1..63) */
	/* VkDeviceQueueInfo2 self */
	uk_venus_encode_uint32(enc, 0u);                 /* flags */
	uk_venus_encode_uint32(enc, queue_family_index);
	uk_venus_encode_uint32(enc, queue_index);
	uk_venus_encode_pointer_flag(enc, 1);            /* pQueue present */
	uk_venus_encode_uint64(enc, queue_handle);
}

void uk_venus_encode_vkQueueSubmit_empty(struct uk_venus_encoder *enc,
					 uint64_t queue_handle,
					 uint64_t fence_handle)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkQueueSubmit,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, queue_handle);
	uk_venus_encode_uint32(enc, 0u);      /* submitCount */
	uk_venus_encode_uint64(enc, 0);       /* pSubmits array size */
	uk_venus_encode_uint64(enc, fence_handle);
}

/*
 * vkCreateRingMESA encoding (VN_CMD_vkCreateRingMESA = 188).
 *
 * Registers a host-visible blob as a Venus command ring.  The host (virglrenderer
 * vkr_ring_create) reads the layout fields and sets up a ring_thread to consume
 * commands written into the circular buffer by the guest.
 *
 * Wire layout (Mesa vn_encode_vkCreateRingMESA):
 *   [uint32 cmd_type=188] [uint32 flags=0]
 *   [uint64 ring_id]
 *   [uint64 pCreateInfo pointer=1]
 *     [uint32 sType=1000384000]          -- VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA
 *     [uint64 pNext=0]                   -- no extension chain
 *     [uint32 flags=0]                   -- VkRingCreateInfoMESA.flags
 *     [uint32 resourceId]                -- VirtIO-GPU blob resource ID
 *     [uint64 offset=0]                  -- ring starts at blob offset 0
 *     [uint64 size]                      -- total blob size (ctrl + data)
 *     [uint64 idleTimeout=0]             -- notify only on new data (no idle)
 *     [uint64 headOffset=0]              -- head at byte 0 (64B cache-line)
 *     [uint64 tailOffset=64]             -- tail at byte 64
 *     [uint64 statusOffset=128]          -- status at byte 128
 *     [uint64 bufferOffset=192]          -- circular data starts at byte 192
 *     [uint64 bufferSize]                -- power-of-2 data buffer size
 *     [uint64 extraOffset]               -- = bufferOffset + bufferSize
 *     [uint64 extraSize=0]               -- no extra shmem
 */
void uk_venus_encode_vkCreateRingMESA(struct uk_venus_encoder *enc,
				      uint64_t ring_id,
				      uint32_t resource_id,
				      uint64_t blob_size,
				      uint64_t buf_size)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkCreateRingMESA,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, ring_id);

	/* pCreateInfo present */
	uk_venus_encode_pointer_flag(enc, 1);

	/* VkRingCreateInfoMESA.sType = VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA */
	uk_venus_encode_uint32(enc, (uint32_t)VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA);
	/* pNext = NULL */
	uk_venus_encode_pointer_flag(enc, 0);

	/* VkRingCreateInfoMESA fields (in order from Mesa vn_encode_VkRingCreateInfoMESA_self) */
	uk_venus_encode_uint32(enc, 0u);                /* flags = 0 */
	uk_venus_encode_uint32(enc, resource_id);        /* resourceId */
	uk_venus_encode_size(enc, 0);                    /* offset = 0 (ring at blob start) */
	uk_venus_encode_size(enc, blob_size);            /* size (total blob) */
	uk_venus_encode_uint64(enc, 0);                  /* idleTimeout = 0 */
	uk_venus_encode_size(enc, (uint64_t)UK_VENUS_RING_HEAD_OFFSET);
	uk_venus_encode_size(enc, (uint64_t)UK_VENUS_RING_TAIL_OFFSET);
	uk_venus_encode_size(enc, (uint64_t)UK_VENUS_RING_STATUS_OFFSET);
	uk_venus_encode_size(enc, (uint64_t)UK_VENUS_RING_BUFFER_OFFSET);
	uk_venus_encode_size(enc, buf_size);             /* bufferSize */
	uk_venus_encode_size(enc, (uint64_t)UK_VENUS_RING_BUFFER_OFFSET + buf_size); /* extraOffset */
	uk_venus_encode_size(enc, 0);                    /* extraSize = 0 */
}

/*
 * vkDestroyRingMESA encoding (VN_CMD_vkDestroyRingMESA = 189).
 */
void uk_venus_encode_vkDestroyRingMESA(struct uk_venus_encoder *enc,
				       uint64_t ring_id)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkDestroyRingMESA,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, ring_id);
}

/*
 * vkNotifyRingMESA encoding (VN_CMD_vkNotifyRingMESA = 190).
 *
 * Tells the host thread that new commands are in the ring buffer.  Must be
 * sent after updating the shared tail field.  seqno is a monotonically
 * increasing notification counter; flags = 0.
 */
void uk_venus_encode_vkNotifyRingMESA(struct uk_venus_encoder *enc,
				      uint64_t ring_id,
				      uint32_t seqno)
{
	uk_venus_encode_command_header(enc, (uint32_t)VN_CMD_vkNotifyRingMESA,
				       VN_COMMAND_FLAGS_NONE);
	uk_venus_encode_uint64(enc, ring_id);
	uk_venus_encode_uint32(enc, seqno);
	uk_venus_encode_uint32(enc, 0u); /* VkRingNotifyFlagsMESA flags = 0 */
}
