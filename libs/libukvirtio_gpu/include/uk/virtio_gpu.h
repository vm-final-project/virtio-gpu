#pragma once
#include <stddef.h>
#include <stdint.h>
#include <uk/sglist.h>

typedef uint32_t uk_gpu_res_id;
typedef uint32_t uk_gpu_ctx_id;
typedef uint64_t uk_gpu_fence_id;

struct uk_fbdev;
struct uk_virtio_gpu_dev;
struct uk_gpu_rect { uint32_t x, y, w, h; };
struct uk_gpu_box { uint32_t x, y, z, w, h, d; };

/*
 * VOGUE profiling — per-phase accounting of the unique guest-side
 * virtio-gpu/Venus path (prompt vs decode).  Each instrumentation site
 * (across libvulkan / libukvulkan_venus / libukvirtio_gpu) feeds counters
 * here; vogue_prof_report() prints one VOGUE-TIMING line per metric/phase
 * which the host-side harness parses into the result JSON.
 *
 * "active" = guest CPU doing real translation work (encode/enqueue/notify).
 * "wait"   = guest spinning on the host (dequeue busy-wait / fence poll) —
 *            i.e. time blocked on the shared host GPU, not our own overhead.
 */
#define VOGUE_PROF_PHASE_PROMPT 0
#define VOGUE_PROF_PHASE_DECODE 1
void vogue_prof_set_phase(int phase);
void vogue_prof_reset(void);
void vogue_prof_reset_phase(int phase);
void vogue_prof_report(void);
void vogue_prof_add_l2(uint64_t ns);                                  /* vkQueueSubmit encode (active) */
void vogue_prof_add_submit(uint64_t active_ns, uint64_t wait_ns,
			   uint32_t bytes);                          /* host round-trip */
void vogue_prof_add_fence(uint64_t ns);                              /* fence-wait (wait) */
void vogue_prof_add_l3(uint64_t ns);                                 /* venus ring flush */
void vogue_prof_add_flush(uint64_t ns, uint32_t bytes, int rc);     /* uk_venus_submit batch flush */
void vogue_prof_add_encode(uint64_t ns);                            /* vk* command encoding */

#define UK_VIRTIO_GPU_F_VIRGL          (1ull << 0)
#define UK_VIRTIO_GPU_F_EDID           (1ull << 1)
#define UK_VIRTIO_GPU_F_RESOURCE_UUID  (1ull << 2)
#define UK_VIRTIO_GPU_F_RESOURCE_BLOB  (1ull << 3)
#define UK_VIRTIO_GPU_F_CONTEXT_INIT   (1ull << 4)
/* bit 5 — blob_alignment config field valid (VirtIO spec §5.7 F_BLOB_ALIGNMENT).
 * Requires F_RESOURCE_BLOB.  When negotiated, blob sizes and MAP_BLOB offsets
 * MUST be aligned to virtio_gpu_config.blob_alignment. */
#define UK_VIRTIO_GPU_F_BLOB_ALIGNMENT (1ull << 5)

#define UK_VIRTIO_GPU_CAPSET_VIRGL        1u
#define UK_VIRTIO_GPU_CAPSET_VIRGL2       2u
#define UK_VIRTIO_GPU_CAPSET_GFXSTREAM    3u
#define UK_VIRTIO_GPU_CAPSET_VENUS        4u
#define UK_VIRTIO_GPU_CAPSET_CROSS_DOMAIN 5u
#define UK_VIRTIO_GPU_CAPSET_DRM          6u
#define UK_VIRTIO_GPU_CAPSET_APIR         10u

#define UK_VIRTIO_GPU_BLOB_MEM_GUEST        0x0001u
#define UK_VIRTIO_GPU_BLOB_MEM_HOST3D       0x0002u
#define UK_VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST 0x0003u
#define UK_VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE 0x0001u
#define UK_VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE 0x0002u
#define UK_VIRTIO_GPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004u

#define UK_VIRTIO_GPU_MAP_CACHE_MASK     0x0fu
#define UK_VIRTIO_GPU_MAP_CACHE_NONE     0x00u
#define UK_VIRTIO_GPU_MAP_CACHE_CACHED   0x01u
#define UK_VIRTIO_GPU_MAP_CACHE_UNCACHED 0x02u
#define UK_VIRTIO_GPU_MAP_CACHE_WC       0x03u

struct uk_virtio_gpu_caps {
	uint32_t num_scanouts;
	uint32_t num_capsets;
	uint64_t device_features;
	uint64_t negotiated_features;
	uint8_t has_virgl;
	uint8_t has_edid;
	uint8_t has_resource_uuid;
	uint8_t has_resource_blob;
	uint8_t has_context_init;
	uint8_t uses_blob_scanout;
	uint8_t has_host_visible;
	uint8_t has_blob_alignment;
	uint32_t blob_alignment;
};

struct uk_virtio_gpu_capset_info {
	uint32_t id;
	uint32_t max_version;
	uint32_t max_size;
};

struct uk_virtio_gpu_shm_region {
	uint32_t id;
	uint32_t padding;
	uint64_t phys_addr;
	uint64_t size;
	void *addr;
};

struct uk_virtio_gpu_resource_3d {
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
	uint32_t flags;
};

struct uk_virtio_gpu_transfer_3d {
	struct uk_gpu_box box;
	uint64_t offset;
	uint32_t level;
	uint32_t stride;
	uint32_t layer_stride;
};

struct uk_virtio_gpu_blob {
	uk_gpu_res_id resource_id;
	uint32_t blob_mem;
	uint32_t blob_flags;
	uint32_t map_info;
	uint64_t blob_id;
	uint64_t size;
	uint64_t host_visible_offset;
	void *mapped_addr;
	uint64_t mapped_size;
	volatile uint32_t *reply_notif;
	uint8_t created;
	uint8_t mapped;
};

struct uk_virtio_gpu_context {
	uk_gpu_ctx_id id;
	uint32_t capset_id;
	uint8_t created;
};

#define UK_VIRTIO_GPU_APIR_PROTOCOL_MAJOR 0u
#define UK_VIRTIO_GPU_APIR_PROTOCOL_MINOR 1u
#define UK_VIRTIO_GPU_APIR_HANDSHAKE_MAGIC 0xab1eu

enum uk_virtio_gpu_apir_command_type {
	UK_VIRTIO_GPU_APIR_COMMAND_HANDSHAKE = 0,
	UK_VIRTIO_GPU_APIR_COMMAND_LOAD_LIBRARY = 1,
	UK_VIRTIO_GPU_APIR_COMMAND_FORWARD = 2,
	UK_VIRTIO_GPU_APIR_COMMAND_LENGTH = 3,
};

enum uk_virtio_gpu_apir_load_library_rc {
	UK_VIRTIO_GPU_APIR_LOAD_LIBRARY_SUCCESS = 0,
	UK_VIRTIO_GPU_APIR_LOAD_LIBRARY_HYPERCALL_INITIALIZATION_ERROR = 1,
	UK_VIRTIO_GPU_APIR_LOAD_LIBRARY_ALREADY_LOADED = 2,
	UK_VIRTIO_GPU_APIR_LOAD_LIBRARY_CFG_KEY_MISSING = 3,
	UK_VIRTIO_GPU_APIR_LOAD_LIBRARY_CANNOT_OPEN = 4,
	UK_VIRTIO_GPU_APIR_LOAD_LIBRARY_SYMBOL_MISSING = 5,
	UK_VIRTIO_GPU_APIR_LOAD_LIBRARY_INIT_BASE_INDEX = 6,
};

enum uk_virtio_gpu_apir_forward_rc {
	UK_VIRTIO_GPU_APIR_FORWARD_SUCCESS = 0,
	UK_VIRTIO_GPU_APIR_FORWARD_NO_DISPATCH_FN = 1,
	UK_VIRTIO_GPU_APIR_FORWARD_TIMEOUT = 2,
	UK_VIRTIO_GPU_APIR_FORWARD_FAILED_TO_SYNC_STREAMS = 3,
	UK_VIRTIO_GPU_APIR_FORWARD_BASE_INDEX = 4,
};

struct uk_virtio_gpu_apir_msg {
	uint32_t command_type;
	uint32_t flags;
	uint32_t reply_resource_id;
};

struct uk_virtio_gpu_apir_handshake {
	uint32_t guest_major;
	uint32_t guest_minor;
	uint32_t host_major;
	uint32_t host_minor;
};

struct uk_virtio_gpu_apir_status {
	uint32_t transport_rc;
	uint32_t host_rc;
	uint32_t reply_size;
};

struct uk_virtio_gpu_metrics {
	uint64_t probes;
	uint64_t resources_created;
	uint64_t resources_unrefed;
	uint64_t attach_calls;
	uint64_t detach_calls;
	uint64_t transfers_to_host;
	uint64_t transfers_from_host;
	uint64_t flushes;
	uint64_t fences_issued;
	uint64_t fence_waits;
	uint64_t contexts_created;
	uint64_t contexts_destroyed;
	uint64_t submits_3d;
	uint64_t blobs_created;
	uint64_t blobs_mapped;
	uint64_t blobs_unmapped;
	uint64_t bytes_to_host;
	uint64_t bytes_from_host;
};

/* Production 2D substrate API. */
int uk_virtio_gpu_probe(struct uk_virtio_gpu_dev **dev);
int uk_virtio_gpu_get_display_info(struct uk_virtio_gpu_dev *dev);
int uk_virtio_gpu_resource_create_2d(struct uk_virtio_gpu_dev *dev, uint32_t width, uint32_t height, uint32_t format, uk_gpu_res_id *res);
int uk_virtio_gpu_resource_attach_backing(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_sglist *sg);
int uk_virtio_gpu_transfer_to_host_2d(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_gpu_rect *r, uk_gpu_fence_id *fence);
int uk_virtio_gpu_resource_flush(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_gpu_rect *r, uk_gpu_fence_id *fence);

/* Coalesced TRANSFER_TO_HOST_2D + RESOURCE_FLUSH that carries one fence
 * instead of two. The transfer is issued un-fenced; the flush carries the
 * fence returned in *fence. The VirtIO-GPU spec guarantees in-order command
 * completion, so a single wait on the flush fence implies both commands done. */
int uk_virtio_gpu_transfer_and_flush_2d(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_gpu_rect *r, uk_gpu_fence_id *fence);
int uk_virtio_gpu_fence_wait(struct uk_virtio_gpu_dev *dev, uk_gpu_fence_id fence, uint64_t timeout_ns);

/* Compatibility prototypes implemented by the companion real Unikraft driver. */
struct uk_virtio_gpu_dev *uk_virtio_gpu_default_dev(void);
struct uk_virtio_gpu_dev *uk_virtio_gpu_from_fbdev(struct uk_fbdev *fbdev);
int uk_virtio_gpu_dev_caps_get(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_caps *caps);
int uk_virtio_gpu_dev_capset_info_get(struct uk_virtio_gpu_dev *dev, uint32_t index, struct uk_virtio_gpu_capset_info *info);
int uk_virtio_gpu_dev_shm_region_get(struct uk_virtio_gpu_dev *dev, uint32_t id, struct uk_virtio_gpu_shm_region *region);
int uk_virtio_gpu_caps_get(struct uk_fbdev *fbdev, struct uk_virtio_gpu_caps *caps);
int uk_virtio_gpu_capset_info_get(struct uk_fbdev *fbdev, uint32_t index, struct uk_virtio_gpu_capset_info *info);
const char *uk_virtio_gpu_capset_name(uint32_t capset_id);
int uk_virtio_gpu_context_create(struct uk_virtio_gpu_dev *dev, uint32_t capset_id, const char *debug_name, struct uk_virtio_gpu_context *ctx);
int uk_virtio_gpu_context_destroy(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_context *ctx);
int uk_virtio_gpu_context_attach_blob(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_context_detach_blob(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_context_submit(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const void *cmd, size_t cmd_len);
int uk_virtio_gpu_blob_create(struct uk_virtio_gpu_dev *dev, uint64_t size, uint32_t blob_mem, uint32_t blob_flags, uint64_t blob_id, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_blob_map(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_blob_unmap(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_blob_destroy(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_apir_encode_header(void *buf, size_t len, const struct uk_virtio_gpu_apir_msg *msg, size_t *used);
int uk_virtio_gpu_apir_decode_header(const void *buf, size_t len, struct uk_virtio_gpu_apir_msg *msg, size_t *used);
int uk_virtio_gpu_apir_encode_handshake(void *buf, size_t len, uint32_t reply_resource_id, size_t *used);
int uk_virtio_gpu_apir_decode_handshake_reply(const void *buf, size_t len, uint32_t host_rc, struct uk_virtio_gpu_apir_handshake *reply);
int uk_virtio_gpu_apir_handshake(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, struct uk_virtio_gpu_apir_handshake *reply, struct uk_virtio_gpu_apir_status *status);
int uk_virtio_gpu_apir_load_library(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, struct uk_virtio_gpu_apir_status *status);
int uk_virtio_gpu_apir_forward(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, uint32_t flags, const void *payload, size_t payload_len, struct uk_virtio_gpu_apir_status *status);

/* FULL VirtIO-GPU-GL/virgl-facing API contract. */
int uk_virtio_gpu_gl_caps_get(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_caps *caps);
int uk_virtio_gpu_gl_capset_info_get(struct uk_virtio_gpu_dev *dev, uint32_t index, struct uk_virtio_gpu_capset_info *info);
int uk_virtio_gpu_gl_capset_get(struct uk_virtio_gpu_dev *dev, uint32_t capset_id, uint32_t version, void *buf, size_t len, size_t *actual_len);
int uk_virtio_gpu_gl_get_edid(struct uk_virtio_gpu_dev *dev, uint32_t scanout_id, void *buf, size_t len, size_t *actual_len);
int uk_virtio_gpu_gl_set_scanout(struct uk_virtio_gpu_dev *dev, uint32_t scanout_id, uk_gpu_res_id res, const struct uk_gpu_rect *r);
int uk_virtio_gpu_gl_resource_detach_backing(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res);
int uk_virtio_gpu_gl_resource_unref(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res);
int uk_virtio_gpu_gl_resource_assign_uuid(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, uint8_t uuid[16]);
int uk_virtio_gpu_gl_resource_create_3d(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_resource_3d *desc, uk_gpu_res_id *res);
int uk_virtio_gpu_gl_transfer_to_host_3d(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_virtio_gpu_transfer_3d *xfer, uk_gpu_fence_id *fence);
int uk_virtio_gpu_gl_transfer_from_host_3d(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res, const struct uk_virtio_gpu_transfer_3d *xfer, uk_gpu_fence_id *fence);
int uk_virtio_gpu_gl_context_create(struct uk_virtio_gpu_dev *dev, uint32_t capset_id, const char *debug_name, struct uk_virtio_gpu_context *ctx);
int uk_virtio_gpu_gl_context_destroy(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_context *ctx);
int uk_virtio_gpu_gl_context_attach_resource(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, uk_gpu_res_id res);
int uk_virtio_gpu_gl_context_detach_resource(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, uk_gpu_res_id res);
int uk_virtio_gpu_gl_context_submit(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const void *cmd, size_t cmd_len, uk_gpu_fence_id *fence);
/*
 * Like uk_virtio_gpu_gl_context_submit() but attaches a per-ring CONTEXT fence
 * (VIRTIO_GPU_FLAG_INFO_RING_IDX + ring_idx) instead of a legacy CPU-timeline
 * fence. On a virglrenderer Venus host a ring_idx fence is retired only after
 * the GPU has completed all prior work on that ring's queue (vkr_queue_sync_submit:
 * empty vkQueueSubmit + sync-thread vkWaitForFences), whereas the legacy
 * (ring_idx==0) fence retires immediately on command decode. Because the
 * control-queue submit blocks for the used-ring response, this call returns
 * only after GPU completion — the cheap completion signal Venus already provides.
 * ring_idx must match a queue bound via vkGetDeviceQueue2/VkDeviceQueueTimelineInfoMESA.
 */
int uk_virtio_gpu_gl_context_submit_synced(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const void *cmd, size_t cmd_len, uint8_t ring_idx, uk_gpu_fence_id *fence);
int uk_virtio_gpu_gl_blob_create(struct uk_virtio_gpu_dev *dev, uint64_t size, uint32_t blob_mem, uint32_t blob_flags, uint64_t blob_id, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_gl_blob_create_with_ctx(struct uk_virtio_gpu_dev *dev, uint32_t ctx_id, uint64_t size, uint32_t blob_mem, uint32_t blob_flags, uint64_t blob_id, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_gl_blob_map(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_gl_blob_unmap(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_gl_blob_destroy(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_blob *blob);
int uk_virtio_gpu_gl_metrics_get(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_metrics *metrics);
int uk_virtio_gpu_gl_metrics_reset(struct uk_virtio_gpu_dev *dev);
const char *uk_virtio_gpu_gl_capset_name(uint32_t capset_id);
