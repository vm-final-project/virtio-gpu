#pragma once
/*
 * libukvenus — Venus (Vulkan-over-VirtIO-GPU) command-stream encoder.
 *
 * The Venus protocol serialises Vulkan API calls into a byte stream that is
 * submitted to the host via the VirtIO-GPU SUBMIT_3D command (capset id 4).
 * This header exposes the Unikraft-facing encoder API used by app-venus-test
 * and the ggml-vulkan GPU acceleration path.
 *
 * Wire format: each command is prefixed by a 4-byte VkCommandTypeEXT
 * followed by PACKED, little-endian encoded fields.  No alignment padding is
 * inserted; uint64_t fields are NOT 8-byte aligned in the stream.
 */
#include <stddef.h>
#include <stdint.h>
#include <uk/virtio_gpu.h>

/* Venus capset identifier (VirtIO-GPU spec §5.7.6.8). */
#define UK_VENUS_CAPSET_ID 4u

/* Max size of a single SUBMIT_3D payload. */
#define UK_VENUS_CMD_BUF_MAX (64u * 1024u)

/* Default host-visible command ring blob size. */
#define UK_VENUS_RING_DEFAULT_SIZE UK_VENUS_CMD_BUF_MAX
#define UK_VENUS_RING_DEFAULT_BLOB_ID 0x76656e757372696eull /* "venusrin" */

/* Venus wire command types (VkCommandTypeEXT, generated from vk.xml). */
/* Instance / device bootstrap */
#define VN_CMD_vkCreateInstance                   0u
#define VN_CMD_vkDestroyInstance                  1u
#define VN_CMD_vkEnumeratePhysicalDevices         2u
#define VN_CMD_vkGetPhysicalDeviceFeatures        3u
#define VN_CMD_vkGetPhysicalDeviceProperties      6u
#define VN_CMD_vkGetPhysicalDeviceQueueFamilyProperties 7u
#define VN_CMD_vkGetPhysicalDeviceMemoryProperties 8u
#define VN_CMD_vkCreateDevice                    11u
#define VN_CMD_vkDestroyDevice                   12u
#define VN_CMD_vkGetDeviceQueue                  17u
#define VN_CMD_vkGetDeviceQueue2                 155u
#define VN_CMD_vkQueueSubmit                     18u
#define VN_CMD_vkQueueWaitIdle                   19u
#define VN_CMD_vkDeviceWaitIdle                  20u
/* Memory */
#define VN_CMD_vkAllocateMemory                  21u
#define VN_CMD_vkFreeMemory                      22u
#define VN_CMD_vkMapMemory                       23u
#define VN_CMD_vkUnmapMemory                     24u
#define VN_CMD_vkBindBufferMemory                28u
#define VN_CMD_vkGetBufferMemoryRequirements     30u
/* Synchronization */
#define VN_CMD_vkCreateFence                     35u
#define VN_CMD_vkDestroyFence                    36u
#define VN_CMD_vkResetFences                     37u
#define VN_CMD_vkWaitForFences                   39u
/* Buffers */
#define VN_CMD_vkCreateBuffer                    50u
#define VN_CMD_vkDestroyBuffer                   51u
/* Shaders */
#define VN_CMD_vkCreateShaderModule              59u
#define VN_CMD_vkDestroyShaderModule             60u
/* Pipelines */
#define VN_CMD_vkCreateComputePipelines          66u
#define VN_CMD_vkDestroyPipeline                 67u
#define VN_CMD_vkCreatePipelineLayout            68u
#define VN_CMD_vkDestroyPipelineLayout           69u
/* Descriptors */
#define VN_CMD_vkCreateDescriptorSetLayout       72u
#define VN_CMD_vkDestroyDescriptorSetLayout      73u
#define VN_CMD_vkCreateDescriptorPool            74u
#define VN_CMD_vkDestroyDescriptorPool           75u
#define VN_CMD_vkAllocateDescriptorSets          77u
#define VN_CMD_vkUpdateDescriptorSets            79u
/* Command pools and buffers */
#define VN_CMD_vkCreateCommandPool               85u
#define VN_CMD_vkDestroyCommandPool              86u
#define VN_CMD_vkAllocateCommandBuffers          88u
#define VN_CMD_vkFreeCommandBuffers              89u
#define VN_CMD_vkBeginCommandBuffer              90u
#define VN_CMD_vkEndCommandBuffer                91u
/* Command recording */
#define VN_CMD_vkCmdBindPipeline                 93u
#define VN_CMD_vkCmdBindDescriptorSets          103u
#define VN_CMD_vkCmdDispatch                    110u
#define VN_CMD_vkCmdCopyBuffer                  112u
#define VN_CMD_vkCmdFillBuffer                  118u
#define VN_CMD_vkCmdPipelineBarrier             126u
#define VN_CMD_vkCmdPushConstants               132u

/* Venus transport command types (MESA extensions, VK_EXT_command_serialization). */
#define VN_CMD_vkSetReplyCommandStreamMESA  178u
#define VN_CMD_vkSeekReplyCommandStreamMESA 179u
#define VN_CMD_vkExecuteCommandStreamsMESA  180u
#define VN_CMD_vkCreateRingMESA             188u
#define VN_CMD_vkDestroyRingMESA            189u
#define VN_CMD_vkNotifyRingMESA             190u

/* Pointer/array sentinel: Venus serializes pointer presence as uint64_t count. */
#define VN_PTR_PRESENT  1ull
#define VN_PTR_NULL     0ull

#define VN_COMMAND_FLAGS_NONE 0u


/*
 * Venus ring shared-memory layout (Mesa vn_ring_get_layout).
 * Each control field occupies a 64-byte cache-line to avoid false sharing.
 *   [  0]: uint32_t head   — host advances when commands are consumed
 *   [ 64]: uint32_t tail   — guest advances when commands are written
 *   [128]: uint32_t status — ring status flags
 *   [192]: uint8_t  buffer[]  — circular command data (size must be power of 2)
 */
#define UK_VENUS_RING_HEAD_OFFSET    0u
#define UK_VENUS_RING_TAIL_OFFSET   64u
#define UK_VENUS_RING_STATUS_OFFSET 128u
#define UK_VENUS_RING_BUFFER_OFFSET 192u
#define UK_VENUS_RING_CTRL_SIZE     192u  /* bytes before the data buffer */

/*
 * Venus capabilities (wire-format capset data returned by GET_CAPSET).
 * Only the fields required for bootstrap are decoded here.
 */
struct uk_venus_caps {
	uint32_t wire_format_version;
	uint32_t vk_xml_version;
	uint32_t vk_ext_command_serialization_spec_version;
	uint32_t vk_mesa_venus_protocol_spec_version;
	uint32_t supports_blob_id_0;
	uint32_t wire_format_version_v2; /* present when wire_format_version >= 2 */
};

/*
 * Encoder state.  Callers embed this in their own structures or declare it
 * on the stack and initialise with uk_venus_encoder_init().
 */
struct uk_venus_encoder {
	uint8_t *buf;        /* output buffer */
	size_t   cap;        /* total buffer capacity */
	size_t   pos;        /* current write position */
	int      overflow;   /* set on first overflow; encoder is then a no-op */
};

/*
 * Host-visible Venus command ring.
 *
 * Blob layout (Mesa vn_ring_get_layout):
 *   base[0]:   uint32 head   — host updates (read-only for guest)
 *   base[64]:  uint32 tail   — guest updates (write via store-release)
 *   base[128]: uint32 status — bidirectional ring status flags
 *   base[192]: uint8  buffer[buf_size] — circular command data
 *
 * After uk_venus_ring_register() the ring is in protocol mode: commands are
 * written to the circular buffer, tail is advanced, and vkNotifyRingMESA is
 * sent when the ring was idle.  Before register() the ring is in staging mode:
 * commands are appended linearly via uk_venus_ring_write/submit.
 */
struct uk_venus_ring {
	struct uk_virtio_gpu_context ctx;
	struct uk_virtio_gpu_blob blob;
	uint8_t *base;      /* mapped blob start (offset 0 = head field) */
	size_t size;        /* total blob size (UK_VENUS_RING_CTRL_SIZE + buf_size) */
	/* --- staging mode (linear write, no protocol) --- */
	size_t write_pos;
	uint64_t bytes_written;
	uint64_t commands_submitted;
	uint8_t ready;
	/* --- protocol mode (circular ring, registered with host) --- */
	uint64_t ring_id;       /* unique ID passed to vkCreateRingMESA */
	uint32_t buf_size;      /* power-of-2 data buffer size */
	uint32_t buf_mask;      /* buf_size - 1 */
	uint32_t cur_tail;      /* guest tail (monotonically increasing, mod wraps in shared mem) */
	uint8_t protocol_ready; /* 1 after uk_venus_ring_register() succeeds */
};

/*
 * uk_venus_encoder_init — bind encoder to a caller-provided buffer.
 * Returns 0 on success, -EINVAL if buf is NULL or cap is 0.
 */
int uk_venus_encoder_init(struct uk_venus_encoder *enc,
			  void *buf, size_t cap);

/* Reset position to 0 (does NOT free buffer). */
void uk_venus_encoder_reset(struct uk_venus_encoder *enc);

/* Number of bytes written since last reset (or init). */
size_t uk_venus_encoder_size(const struct uk_venus_encoder *enc);

/* Returns non-zero if any write overflowed the buffer. */
int uk_venus_encoder_overflow(const struct uk_venus_encoder *enc);

/* Low-level write helpers — used by encode functions below. */
void uk_venus_encode_uint32(struct uk_venus_encoder *enc, uint32_t v);
void uk_venus_encode_uint64(struct uk_venus_encoder *enc, uint64_t v);
/* Encode a size_t as uint64 (Venus targets 64-bit; vn_encode_size_t = uint64). */
void uk_venus_encode_size(struct uk_venus_encoder *enc, uint64_t v);
void uk_venus_encode_float32(struct uk_venus_encoder *enc, float v);
void uk_venus_encode_bytes(struct uk_venus_encoder *enc,
			   const void *src, size_t len);
void uk_venus_encode_cstring(struct uk_venus_encoder *enc, const char *s);
/* pointer presence: uint64_t (0=absent, 1=present) */
void uk_venus_encode_pointer_flag(struct uk_venus_encoder *enc, int present);
/* array presence/count: uint64_t (0=NULL, n=element count) — matches Mesa vn_encode_array_size */
void uk_venus_encode_array_size(struct uk_venus_encoder *enc, uint64_t n);
void uk_venus_encode_command_header(struct uk_venus_encoder *enc, uint32_t cmd_type, uint32_t flags);

/*
 * Encode a VkApplicationInfo into the stream.
 * app_name and engine_name may be NULL.
 */
void uk_venus_encode_VkApplicationInfo(struct uk_venus_encoder *enc,
				       const char *app_name,
				       uint32_t app_version,
				       const char *engine_name,
				       uint32_t engine_version,
				       uint32_t api_version);

/*
 * Encode vkCreateInstance into the stream.
 * app_info may be NULL (pass 0 for all app fields).
 * extension_names / layer_names are NULL-terminated arrays (pass NULL for none).
 */
void uk_venus_encode_vkCreateInstance(struct uk_venus_encoder *enc,
				      uint64_t instance_handle,
				      const char *app_name,
				      uint32_t api_version,
				      uint32_t n_extensions,
				      const char * const *extensions);

/*
 * Encode vkEnumeratePhysicalDevices into the stream.
 */
void uk_venus_encode_vkEnumeratePhysicalDevices(struct uk_venus_encoder *enc,
						uint64_t instance_handle,
						uint32_t max_devices,
						const uint64_t *device_handles);

/* Encode the minimal device/queue/submit commands needed for Vulkan smoke. */
void uk_venus_encode_vkCreateDevice(struct uk_venus_encoder *enc,
				    uint64_t physical_device_handle,
				    uint64_t device_handle,
				    uint32_t queue_family_index,
				    float queue_priority,
				    uint32_t n_extensions,
				    const char * const *extensions);
void uk_venus_encode_vkGetDeviceQueue(struct uk_venus_encoder *enc,
				      uint64_t device_handle,
				      uint32_t queue_family_index,
				      uint32_t queue_index,
				      uint64_t queue_handle);
void uk_venus_encode_vkGetDeviceQueue2(struct uk_venus_encoder *enc,
				       uint64_t device_handle,
				       uint32_t queue_family_index,
				       uint32_t queue_index,
				       uint32_t ring_idx,
				       uint64_t queue_handle);
void uk_venus_encode_vkQueueSubmit_empty(struct uk_venus_encoder *enc,
					 uint64_t queue_handle,
					 uint64_t fence_handle);

/* Reply-bearing Venus query commands (real round-trip via reply shmem). */
void uk_venus_encode_vkSetReplyCommandStreamMESA(struct uk_venus_encoder *enc,
						 uint32_t resource_id,
						 uint64_t offset,
						 uint64_t size);
void uk_venus_encode_vkGetPhysicalDeviceProperties(struct uk_venus_encoder *enc,
						   uint64_t physdev_handle);
void uk_venus_encode_vkGetPhysicalDeviceMemoryProperties(struct uk_venus_encoder *enc,
							 uint64_t physdev_handle);

/*
 * uk_venus_query_device_name — perform a REAL Venus round-trip on an existing
 * Venus context to read the host physical device's name. Submits
 * [vkSetReplyCommandStreamMESA][vkGetPhysicalDeviceProperties] over SUBMIT_3D
 * into a freshly-created host-visible reply blob, waits on the fence, and
 * extracts the deviceName from the host-written reply.
 *
 * physdev_handle must be a VkPhysicalDevice id already registered on ctx (e.g.
 * via vkEnumeratePhysicalDevices). On success returns 0 and writes a
 * NUL-terminated name into name_out; returns <0 if the round-trip failed or no
 * plausible name was found (caller should fall back to its default).
 */
int uk_venus_query_device_name(struct uk_virtio_gpu_dev *dev,
			       struct uk_virtio_gpu_context *ctx,
			       uint64_t physdev_handle,
			       char *name_out, unsigned int name_cap);

/*
 * uk_venus_query_memory_properties — REAL Venus round-trip that fills a
 * 520-byte VkPhysicalDeviceMemoryProperties with the host device's real memory
 * types and heaps (so ggml-vulkan selects a memory type index that actually
 * exists and is host-visible on the host GPU). physdev_handle must already be
 * registered on ctx. Returns 0 and fills props_out on success, <0 otherwise.
 */
int uk_venus_query_memory_properties(struct uk_virtio_gpu_dev *dev,
				     struct uk_virtio_gpu_context *ctx,
				     uint64_t physdev_handle,
				     void *props_out);

/* Create the device via Venus and read the host VkResult back (reply
 * round-trip), so the caller can detect a failing device creation. */
int uk_venus_create_device_checked(struct uk_virtio_gpu_dev *dev,
				   struct uk_virtio_gpu_context *ctx,
				   uint64_t physdev_handle,
				   uint64_t device_handle,
				   uint32_t queue_family_index,
				   int32_t *vk_result_out);

/* Real Venus round-trip for a buffer's VkMemoryRequirements (size/alignment/
 * memoryTypeBits) from the host. Returns 0 and fills outputs on success. */
int uk_venus_query_buffer_requirements(struct uk_virtio_gpu_dev *dev,
				       struct uk_virtio_gpu_context *ctx,
				       uint64_t device_handle,
				       uint64_t buffer_handle,
				       uint64_t *size_out,
				       uint64_t *align_out,
				       uint32_t *type_bits_out);

/*
 * Venus ring transport commands (Mesa VK_EXT_command_serialization extension).
 *
 * vkCreateRingMESA — registers a host-visible blob as a Venus command ring.
 *   ring_id:      guest-assigned unique identifier for this ring
 *   resource_id:  VirtIO-GPU resource ID of the HOST3D_GUEST blob
 *   blob_size:    total size of the blob (UK_VENUS_RING_CTRL_SIZE + buf_size)
 *   buf_size:     power-of-2 size of the circular data buffer
 *
 * vkDestroyRingMESA — destroys a previously created ring.
 *
 * vkNotifyRingMESA — notifies the host that new commands are in the ring.
 *   seqno: monotonically increasing notification sequence number
 */
void uk_venus_encode_vkCreateRingMESA(struct uk_venus_encoder *enc,
				      uint64_t ring_id,
				      uint32_t resource_id,
				      uint64_t blob_size,
				      uint64_t buf_size);
void uk_venus_encode_vkDestroyRingMESA(struct uk_venus_encoder *enc,
				       uint64_t ring_id);
void uk_venus_encode_vkNotifyRingMESA(struct uk_venus_encoder *enc,
				      uint64_t ring_id,
				      uint32_t seqno);

/*
 * Capset query helpers (uses libukvirtio_gpu transport).
 */
struct uk_virtio_gpu_dev;

/*
 * uk_venus_capset_get — retrieve and decode the Venus capset.
 * Returns 0 on success.  caps may be NULL (validates existence only).
 */
int uk_venus_capset_get(struct uk_virtio_gpu_dev *dev,
			struct uk_venus_caps *caps);

/*
 * uk_venus_probe — check that Venus is available and return a status string.
 * Always returns a non-NULL pointer.
 */
const char *uk_venus_probe(struct uk_virtio_gpu_dev *dev);

/*
 * uk_venus_context_create — create a Venus context (capset id 4).
 * Returns 0 on success; ctx_id receives the assigned context id on success.
 */
int uk_venus_context_create(struct uk_virtio_gpu_dev *dev, uint32_t *ctx_id);

/*
 * uk_venus_submit — encode + submit a pre-filled encoder buffer via SUBMIT_3D.
 * ctx must have been created with capset_id = UK_VENUS_CAPSET_ID.
 * Returns 0 on success.
 */
int uk_venus_submit(struct uk_virtio_gpu_dev *dev,
		    const struct uk_virtio_gpu_context *ctx,
		    const struct uk_venus_encoder *enc);

/*
 * uk_venus_ring_bind_current — bind the device+context that the generated
 * Venus submit/call wrappers route through (see vn_ring_shim.c). Call once
 * after a Venus context is created, before driving any generated wrapper.
 */
struct uk_virtio_gpu_context;
void uk_venus_ring_bind_current(struct uk_virtio_gpu_dev *dev,
				struct uk_virtio_gpu_context *ctx);

/*
 * Ring/blob helpers for the accelerated Venus substrate.
 * create() requires RESOURCE_BLOB + CONTEXT_INIT + host-visible mapping.
 *
 * Staging mode (existing behavior):
 *   uk_venus_ring_create  — allocate blob + context, map host-visible.
 *   uk_venus_ring_write   — append bytes linearly to the staging area.
 *   uk_venus_ring_submit  — submit staged bytes via SUBMIT_3D + fence.
 *   uk_venus_ring_destroy — release context, blob, and mapping.
 *
 * Protocol mode (full Venus ring protocol, Mesa-compatible):
 *   uk_venus_ring_register    — initialize head/tail/status, send vkCreateRingMESA.
 *   uk_venus_ring_cmd_write   — write to circular data buffer, advance cur_tail.
 *   uk_venus_ring_cmd_flush   — store tail to shared memory + vkNotifyRingMESA.
 *   uk_venus_ring_cmd_wait    — poll shared head until commands are consumed.
 *   uk_venus_ring_load_head   — read current head value from shared memory.
 *   uk_venus_ring_unregister  — send vkDestroyRingMESA.
 *
 * Ring blob size: pass (UK_VENUS_RING_CTRL_SIZE + buf_size) where buf_size is
 * a power of 2.  uk_venus_ring_register() computes buf_size automatically as
 * the largest power-of-2 that fits in (ring->size - UK_VENUS_RING_CTRL_SIZE).
 */
int uk_venus_ring_create(struct uk_virtio_gpu_dev *dev,
			 struct uk_venus_ring *ring,
			 size_t size, uint64_t blob_id);
void uk_venus_ring_destroy(struct uk_virtio_gpu_dev *dev,
			   struct uk_venus_ring *ring);
int uk_venus_ring_write(struct uk_venus_ring *ring, const void *data,
			size_t len, size_t *offset_out);
int uk_venus_ring_submit(struct uk_virtio_gpu_dev *dev,
			 struct uk_venus_ring *ring,
			 const struct uk_venus_encoder *enc,
			 uk_gpu_fence_id *fence_out);
const char *uk_venus_ring_status(struct uk_virtio_gpu_dev *dev);

/* Protocol mode — full Mesa-compatible Venus ring. */
int uk_venus_ring_register(struct uk_virtio_gpu_dev *dev,
			   struct uk_venus_ring *ring,
			   uint64_t ring_id);
int uk_venus_ring_unregister(struct uk_virtio_gpu_dev *dev,
			     struct uk_venus_ring *ring);
int uk_venus_ring_cmd_write(struct uk_venus_ring *ring,
			    const void *data, uint32_t len);
int uk_venus_ring_cmd_flush(struct uk_virtio_gpu_dev *dev,
			    struct uk_venus_ring *ring);
int uk_venus_ring_cmd_wait(struct uk_venus_ring *ring,
			   uint32_t timeout_iters);
uint32_t uk_venus_ring_load_head(const struct uk_venus_ring *ring);

/* ── Venus compute dispatch encoding (venus_compute.c) ────────────────────
 *
 * These functions encode Vulkan compute-dispatch calls into a venus encoder
 * using the Venus wire format (VkCommandTypeEXT packed protocol).  All Vulkan
 * object handles are uint64_t guest-side IDs; the Venus host side maps them
 * to real Vulkan objects.
 *
 * Claim boundary: encoding is proven correct by native wire-format tests.
 * End-to-end GPU compute inside Unikraft additionally requires the C++
 * runtime (lib-musl + lib-libcxx + lib-libcxxabi + lib-pthread-embedded)
 * to link ggml-vulkan.cpp, and those catalog libs must be available at
 * kraft build time.
 */

/* Memory allocation / mapping */
void uk_venus_encode_vkAllocateMemory(struct uk_venus_encoder *enc,
				      uint64_t device, uint64_t mem_handle,
				      uint64_t alloc_size, uint32_t mem_type_index);
void uk_venus_encode_vkFreeMemory(struct uk_venus_encoder *enc,
				  uint64_t device, uint64_t memory);

/* Buffers */
void uk_venus_encode_vkCreateBuffer(struct uk_venus_encoder *enc,
				    uint64_t device, uint64_t buffer_handle,
				    uint64_t size, uint32_t usage);
void uk_venus_encode_vkDestroyBuffer(struct uk_venus_encoder *enc,
				     uint64_t device, uint64_t buffer);
void uk_venus_encode_vkGetBufferMemoryRequirements(struct uk_venus_encoder *enc,
						   uint64_t device, uint64_t buffer);
void uk_venus_encode_vkBindBufferMemory(struct uk_venus_encoder *enc,
					uint64_t device, uint64_t buffer,
					uint64_t memory, uint64_t offset);

/* Shader modules */
void uk_venus_encode_vkCreateShaderModule(struct uk_venus_encoder *enc,
					  uint64_t device,
					  uint64_t shader_handle,
					  const uint32_t *spirv,
					  uint32_t spirv_words);
void uk_venus_encode_vkDestroyShaderModule(struct uk_venus_encoder *enc,
					   uint64_t device,
					   uint64_t shader_module);

/* Pipeline layout */
void uk_venus_encode_vkCreatePipelineLayout(struct uk_venus_encoder *enc,
					    uint64_t device,
					    uint64_t layout_handle,
					    uint32_t n_set_layouts,
					    const uint64_t *set_layout_handles,
					    uint32_t n_push_ranges,
					    uint32_t push_stage_flags,
					    uint32_t push_offset,
					    uint32_t push_size);
void uk_venus_encode_vkDestroyPipelineLayout(struct uk_venus_encoder *enc,
					     uint64_t device,
					     uint64_t pipeline_layout);

/* Compute pipelines */
void uk_venus_encode_vkCreateComputePipelines(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pipeline_handle,
					      uint64_t pipeline_layout,
					      uint64_t shader_module,
					      const char *entry_point);
void uk_venus_encode_vkDestroyPipeline(struct uk_venus_encoder *enc,
				       uint64_t device, uint64_t pipeline);

/* Descriptor set layout */
void uk_venus_encode_vkCreateDescriptorSetLayout(struct uk_venus_encoder *enc,
						 uint64_t device,
						 uint64_t layout_handle,
						 uint32_t n_bindings,
						 const uint32_t *binding_nums,
						 const uint32_t *desc_types,
						 const uint32_t *desc_counts,
						 const uint32_t *stage_flags);
void uk_venus_encode_vkDestroyDescriptorSetLayout(struct uk_venus_encoder *enc,
						  uint64_t device,
						  uint64_t layout);

/* Descriptor pool and sets */
void uk_venus_encode_vkCreateDescriptorPool(struct uk_venus_encoder *enc,
					    uint64_t device,
					    uint64_t pool_handle,
					    uint32_t max_sets,
					    uint32_t pool_size_count,
					    const uint32_t *desc_types,
					    const uint32_t *desc_counts);
void uk_venus_encode_vkDestroyDescriptorPool(struct uk_venus_encoder *enc,
					     uint64_t device, uint64_t pool);
void uk_venus_encode_vkAllocateDescriptorSets(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pool,
					      uint32_t n_sets,
					      const uint64_t *set_layout_handles,
					      const uint64_t *set_handles);
void uk_venus_encode_vkUpdateDescriptorSets_storage(struct uk_venus_encoder *enc,
						    uint64_t device,
						    uint64_t set,
						    uint32_t binding,
						    uint64_t buffer,
						    uint64_t buf_offset,
						    uint64_t buf_range);

/* Command pool and buffers */
void uk_venus_encode_vkCreateCommandPool(struct uk_venus_encoder *enc,
					 uint64_t device,
					 uint64_t pool_handle,
					 uint32_t queue_family_index);
void uk_venus_encode_vkDestroyCommandPool(struct uk_venus_encoder *enc,
					  uint64_t device, uint64_t pool);
void uk_venus_encode_vkAllocateCommandBuffers(struct uk_venus_encoder *enc,
					      uint64_t device,
					      uint64_t pool,
					      uint32_t n_bufs,
					      const uint64_t *cmd_buf_handles);
void uk_venus_encode_vkFreeCommandBuffers(struct uk_venus_encoder *enc,
					  uint64_t device,
					  uint64_t pool,
					  uint32_t n_bufs,
					  const uint64_t *cmd_bufs);
void uk_venus_encode_vkBeginCommandBuffer(struct uk_venus_encoder *enc,
					  uint64_t cmd_buf);
void uk_venus_encode_vkEndCommandBuffer(struct uk_venus_encoder *enc,
					uint64_t cmd_buf);

/* Command recording — compute dispatch */
void uk_venus_encode_vkCmdBindPipeline(struct uk_venus_encoder *enc,
				       uint64_t cmd_buf,
				       uint64_t pipeline);
void uk_venus_encode_vkCmdBindDescriptorSets(struct uk_venus_encoder *enc,
					     uint64_t cmd_buf,
					     uint64_t pipeline_layout,
					     uint32_t first_set,
					     uint32_t n_sets,
					     const uint64_t *sets);
void uk_venus_encode_vkCmdPushConstants(struct uk_venus_encoder *enc,
					uint64_t cmd_buf,
					uint64_t pipeline_layout,
					uint32_t stage_flags,
					uint32_t offset,
					uint32_t size,
					const void *values);
void uk_venus_encode_vkCmdDispatch(struct uk_venus_encoder *enc,
				   uint64_t cmd_buf,
				   uint32_t x, uint32_t y, uint32_t z);
void uk_venus_encode_vkCmdCopyBuffer(struct uk_venus_encoder *enc,
				     uint64_t cmd_buf,
				     uint64_t src_buf, uint64_t dst_buf,
				     uint64_t size);
void uk_venus_encode_vkCmdFillBuffer(struct uk_venus_encoder *enc,
				     uint64_t cmd_buf, uint64_t buffer,
				     uint64_t offset, uint64_t size,
				     uint32_t data);
void uk_venus_encode_vkCmdPipelineBarrier(struct uk_venus_encoder *enc,
					  uint64_t cmd_buf,
					  uint32_t src_stage, uint32_t dst_stage);

/* Fences */
void uk_venus_encode_vkCreateFence(struct uk_venus_encoder *enc,
				   uint64_t device, uint64_t fence_handle,
				   int signaled);
void uk_venus_encode_vkResetFences(struct uk_venus_encoder *enc,
				   uint64_t device,
				   uint32_t n_fences,
				   const uint64_t *fences);
void uk_venus_encode_vkWaitForFences(struct uk_venus_encoder *enc,
				     uint64_t device,
				     uint32_t n_fences,
				     const uint64_t *fences,
				     uint64_t timeout_ns);

/* Queue submit with command buffers */
void uk_venus_encode_vkQueueSubmit(struct uk_venus_encoder *enc,
				   uint64_t queue,
				   uint32_t n_cmd_bufs,
				   const uint64_t *cmd_bufs,
				   uint64_t fence);
void uk_venus_encode_vkQueueWaitIdle(struct uk_venus_encoder *enc,
				     uint64_t queue);

