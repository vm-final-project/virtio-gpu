#pragma once
/*
 * VirtIO-GPU wire ABI subset used by libukvirtio_gpu.
 * Source: VirtIO 1.3 CSD01, section 5.7 GPU Device.
 */
#include <stddef.h>
#include <stdint.h>

#define UKVGPU_MAX_SCANOUTS 16u
#define UKVGPU_MAX_CAPSET_PAYLOAD 4096u

#define UKVGPU_EVENT_DISPLAY (1u << 0)

#define UKVGPU_CMD_GET_DISPLAY_INFO       0x0100u
#define UKVGPU_CMD_RESOURCE_CREATE_2D     0x0101u
#define UKVGPU_CMD_RESOURCE_UNREF         0x0102u
#define UKVGPU_CMD_SET_SCANOUT            0x0103u
#define UKVGPU_CMD_RESOURCE_FLUSH         0x0104u
#define UKVGPU_CMD_TRANSFER_TO_HOST_2D    0x0105u
#define UKVGPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define UKVGPU_CMD_RESOURCE_DETACH_BACKING 0x0107u
#define UKVGPU_CMD_GET_CAPSET_INFO        0x0108u
#define UKVGPU_CMD_GET_CAPSET             0x0109u
#define UKVGPU_CMD_GET_EDID               0x010au
#define UKVGPU_CMD_RESOURCE_ASSIGN_UUID   0x010bu
#define UKVGPU_CMD_RESOURCE_CREATE_BLOB   0x010cu
#define UKVGPU_CMD_SET_SCANOUT_BLOB       0x010du

#define UKVGPU_CMD_CTX_CREATE             0x0200u
#define UKVGPU_CMD_CTX_DESTROY            0x0201u
#define UKVGPU_CMD_CTX_ATTACH_RESOURCE    0x0202u
#define UKVGPU_CMD_CTX_DETACH_RESOURCE    0x0203u
#define UKVGPU_CMD_RESOURCE_CREATE_3D     0x0204u
#define UKVGPU_CMD_TRANSFER_TO_HOST_3D    0x0205u
#define UKVGPU_CMD_TRANSFER_FROM_HOST_3D  0x0206u
#define UKVGPU_CMD_SUBMIT_3D              0x0207u
#define UKVGPU_CMD_RESOURCE_MAP_BLOB      0x0208u
#define UKVGPU_CMD_RESOURCE_UNMAP_BLOB    0x0209u

#define UKVGPU_RESP_OK_NODATA             0x1100u
#define UKVGPU_RESP_OK_DISPLAY_INFO       0x1101u
#define UKVGPU_RESP_OK_CAPSET_INFO        0x1102u
#define UKVGPU_RESP_OK_CAPSET             0x1103u
#define UKVGPU_RESP_OK_EDID               0x1104u
#define UKVGPU_RESP_OK_RESOURCE_UUID      0x1105u
#define UKVGPU_RESP_OK_MAP_INFO           0x1106u

#define UKVGPU_RESP_ERR_UNSPEC            0x1200u
#define UKVGPU_RESP_ERR_OUT_OF_MEMORY     0x1201u
#define UKVGPU_RESP_ERR_INVALID_SCANOUT_ID 0x1202u
#define UKVGPU_RESP_ERR_INVALID_RESOURCE_ID 0x1203u
#define UKVGPU_RESP_ERR_INVALID_CONTEXT_ID 0x1204u
#define UKVGPU_RESP_ERR_INVALID_PARAMETER 0x1205u

#define UKVGPU_FLAG_FENCE                 (1u << 0)
#define UKVGPU_FLAG_INFO_RING_IDX         (1u << 1)

#define UKVGPU_FORMAT_B8G8R8A8_UNORM      1u
#define UKVGPU_FORMAT_B8G8R8X8_UNORM      2u
#define UKVGPU_FORMAT_A8R8G8B8_UNORM      3u
#define UKVGPU_FORMAT_X8R8G8B8_UNORM      4u
#define UKVGPU_FORMAT_R8G8B8A8_UNORM      67u
#define UKVGPU_FORMAT_X8B8G8R8_UNORM      68u
#define UKVGPU_FORMAT_A8B8G8R8_UNORM      121u
#define UKVGPU_FORMAT_R8G8B8X8_UNORM      134u

#define UKVGPU_CONTEXT_INIT_CAPSET_ID_MASK 0x000000ffu

#define UKVGPU_BLOB_MEM_GUEST             0x0001u
#define UKVGPU_BLOB_MEM_HOST3D            0x0002u
#define UKVGPU_BLOB_MEM_HOST3D_GUEST      0x0003u
#define UKVGPU_BLOB_FLAG_USE_MAPPABLE     0x0001u
#define UKVGPU_BLOB_FLAG_USE_SHAREABLE    0x0002u
#define UKVGPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004u

#define UKVGPU_MAP_CACHE_MASK             0x0fu
#define UKVGPU_MAP_CACHE_NONE             0x00u
#define UKVGPU_MAP_CACHE_CACHED           0x01u
#define UKVGPU_MAP_CACHE_UNCACHED         0x02u
#define UKVGPU_MAP_CACHE_WC               0x03u

struct ukvgpu_config {
	uint32_t events_read;
	uint32_t events_clear;
	uint32_t num_scanouts;
	uint32_t num_capsets;
	/* Present when VIRTIO_GPU_F_BLOB_ALIGNMENT (bit 5) is negotiated.
	 * Blob sizes (CREATE_BLOB) and MAP_BLOB offsets must be aligned to
	 * this value.  Valid range: power-of-two, non-zero (spec §5.7). */
	uint32_t blob_alignment;
} __attribute__((packed));

struct ukvgpu_ctrl_hdr {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	uint32_t ctx_id;
	uint8_t ring_idx;
	uint8_t padding[3];
} __attribute__((packed));

struct ukvgpu_rect {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
} __attribute__((packed));

struct ukvgpu_display_one {
	struct ukvgpu_rect r;
	uint32_t enabled;
	uint32_t flags;
} __attribute__((packed));

struct ukvgpu_resp_display_info {
	struct ukvgpu_ctrl_hdr hdr;
	struct ukvgpu_display_one pmodes[UKVGPU_MAX_SCANOUTS];
} __attribute__((packed));

struct ukvgpu_resource_create_2d {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
} __attribute__((packed));

struct ukvgpu_resource_unref {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_set_scanout {
	struct ukvgpu_ctrl_hdr hdr;
	struct ukvgpu_rect r;
	uint32_t scanout_id;
	uint32_t resource_id;
} __attribute__((packed));

struct ukvgpu_resource_flush {
	struct ukvgpu_ctrl_hdr hdr;
	struct ukvgpu_rect r;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_transfer_to_host_2d {
	struct ukvgpu_ctrl_hdr hdr;
	struct ukvgpu_rect r;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_resource_attach_backing {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t nr_entries;
} __attribute__((packed));

struct ukvgpu_resource_detach_backing {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_mem_entry {
	uint64_t addr;
	uint32_t length;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_get_capset_info {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t capset_index;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_resp_capset_info {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t capset_id;
	uint32_t capset_max_version;
	uint32_t capset_max_size;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_get_capset {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t capset_id;
	uint32_t capset_version;
} __attribute__((packed));

struct ukvgpu_resp_capset {
	struct ukvgpu_ctrl_hdr hdr;
	uint8_t capset_data[UKVGPU_MAX_CAPSET_PAYLOAD];
} __attribute__((packed));

struct ukvgpu_get_edid {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t scanout;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_resp_edid {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t size;
	uint32_t padding;
	uint8_t edid[1024];
} __attribute__((packed));

struct ukvgpu_resource_assign_uuid {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_ctx_create {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t nlen;
	uint32_t context_init;
	char debug_name[64];
} __attribute__((packed));

struct ukvgpu_ctx_resource {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_resource_create_3d {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
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
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_transfer_host_3d {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t level;
	struct { uint32_t x, y, z, w, h, d; } box;
	uint64_t offset;
	uint32_t stride;
	uint32_t layer_stride;
} __attribute__((packed));

struct ukvgpu_cmd_submit_3d {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t size;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_resource_create_blob {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t blob_mem;
	uint32_t blob_flags;
	uint32_t nr_entries;
	uint64_t blob_id;
	uint64_t size;
} __attribute__((packed));

struct ukvgpu_resource_map_blob {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
	uint64_t offset;
} __attribute__((packed));

struct ukvgpu_resp_map_info {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t map_info;
	uint32_t padding;
} __attribute__((packed));

struct ukvgpu_resource_unmap_blob {
	struct ukvgpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

#define UKVGPU_STATIC_ASSERTS() \
	_Static_assert(sizeof(struct ukvgpu_ctrl_hdr) == 24, "virtio-gpu ctrl header size"); \
	_Static_assert(offsetof(struct ukvgpu_ctrl_hdr, fence_id) == 8, "virtio-gpu fence offset"); \
	_Static_assert(sizeof(struct ukvgpu_rect) == 16, "virtio-gpu rect size"); \
	_Static_assert(sizeof(struct ukvgpu_resource_create_2d) == 40, "resource_create_2d size"); \
	_Static_assert(sizeof(struct ukvgpu_set_scanout) == 48, "set_scanout size"); \
	_Static_assert(sizeof(struct ukvgpu_transfer_to_host_2d) == 56, "transfer_to_host_2d size"); \
	_Static_assert(sizeof(struct ukvgpu_resource_attach_backing) == 32, "attach_backing header size"); \
	_Static_assert(sizeof(struct ukvgpu_mem_entry) == 16, "mem_entry size"); \
	_Static_assert(sizeof(struct ukvgpu_resp_display_info) == 408, "display_info response size"); \
	_Static_assert(sizeof(struct ukvgpu_resp_capset_info) == 40, "capset_info response size"); \
	_Static_assert(sizeof(struct ukvgpu_get_capset) == 32, "get_capset request size"); \
	_Static_assert(sizeof(struct ukvgpu_ctx_create) == 96, "ctx_create size"); \
	_Static_assert(sizeof(struct ukvgpu_resource_create_blob) == 56, "resource_create_blob size"); \
	_Static_assert(sizeof(struct ukvgpu_resource_map_blob) == 40, "resource_map_blob size")

static inline uint32_t ukvgpu_resp_type(const struct ukvgpu_ctrl_hdr *hdr)
{
	return hdr ? hdr->type : 0;
}

/* Spec-compatible names used by native ABI tests and real backend code. */
#define VIRTIO_GPU_F_VIRGL 0u
#define VIRTIO_GPU_F_EDID 1u
#define VIRTIO_GPU_F_RESOURCE_UUID 2u
#define VIRTIO_GPU_F_RESOURCE_BLOB 3u
#define VIRTIO_GPU_F_CONTEXT_INIT 4u
#define VIRTIO_GPU_F_BLOB_ALIGNMENT 5u

#define VIRTIO_GPU_FLAG_FENCE UKVGPU_FLAG_FENCE
#define VIRTIO_GPU_FLAG_INFO_RING_IDX UKVGPU_FLAG_INFO_RING_IDX

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO UKVGPU_CMD_GET_DISPLAY_INFO
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D UKVGPU_CMD_RESOURCE_CREATE_2D
#define VIRTIO_GPU_CMD_RESOURCE_UNREF UKVGPU_CMD_RESOURCE_UNREF
#define VIRTIO_GPU_CMD_SET_SCANOUT UKVGPU_CMD_SET_SCANOUT
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH UKVGPU_CMD_RESOURCE_FLUSH
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D UKVGPU_CMD_TRANSFER_TO_HOST_2D
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING UKVGPU_CMD_RESOURCE_ATTACH_BACKING
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING UKVGPU_CMD_RESOURCE_DETACH_BACKING
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO UKVGPU_CMD_GET_CAPSET_INFO
#define VIRTIO_GPU_CMD_GET_CAPSET UKVGPU_CMD_GET_CAPSET
#define VIRTIO_GPU_CMD_GET_EDID UKVGPU_CMD_GET_EDID

#define VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID UKVGPU_CMD_RESOURCE_ASSIGN_UUID
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB UKVGPU_CMD_RESOURCE_CREATE_BLOB
#define VIRTIO_GPU_CMD_SET_SCANOUT_BLOB UKVGPU_CMD_SET_SCANOUT_BLOB
#define VIRTIO_GPU_CMD_CTX_CREATE UKVGPU_CMD_CTX_CREATE
#define VIRTIO_GPU_CMD_CTX_DESTROY UKVGPU_CMD_CTX_DESTROY
#define VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE UKVGPU_CMD_CTX_ATTACH_RESOURCE
#define VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE UKVGPU_CMD_CTX_DETACH_RESOURCE
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D UKVGPU_CMD_RESOURCE_CREATE_3D
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D UKVGPU_CMD_TRANSFER_TO_HOST_3D
#define VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D UKVGPU_CMD_TRANSFER_FROM_HOST_3D
#define VIRTIO_GPU_CMD_SUBMIT_3D UKVGPU_CMD_SUBMIT_3D
#define VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB UKVGPU_CMD_RESOURCE_MAP_BLOB
#define VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB UKVGPU_CMD_RESOURCE_UNMAP_BLOB
#define VIRTIO_GPU_RESP_OK_RESOURCE_UUID UKVGPU_RESP_OK_RESOURCE_UUID
#define VIRTIO_GPU_RESP_OK_MAP_INFO UKVGPU_RESP_OK_MAP_INFO
#define VIRTIO_GPU_BLOB_MEM_GUEST UKVGPU_BLOB_MEM_GUEST
#define VIRTIO_GPU_BLOB_MEM_HOST3D UKVGPU_BLOB_MEM_HOST3D
#define VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST UKVGPU_BLOB_MEM_HOST3D_GUEST
#define VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE UKVGPU_BLOB_FLAG_USE_MAPPABLE
#define VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE UKVGPU_BLOB_FLAG_USE_SHAREABLE
#define VIRTIO_GPU_BLOB_FLAG_USE_CROSS_DEVICE UKVGPU_BLOB_FLAG_USE_CROSS_DEVICE
#define VIRTIO_GPU_MAP_CACHE_MASK UKVGPU_MAP_CACHE_MASK
#define VIRTIO_GPU_MAP_CACHE_NONE UKVGPU_MAP_CACHE_NONE
#define VIRTIO_GPU_MAP_CACHE_CACHED UKVGPU_MAP_CACHE_CACHED
#define VIRTIO_GPU_MAP_CACHE_UNCACHED UKVGPU_MAP_CACHE_UNCACHED
#define VIRTIO_GPU_MAP_CACHE_WC UKVGPU_MAP_CACHE_WC
#define VIRTIO_GPU_RESP_OK_NODATA UKVGPU_RESP_OK_NODATA
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO UKVGPU_RESP_OK_DISPLAY_INFO
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO UKVGPU_RESP_OK_CAPSET_INFO
#define VIRTIO_GPU_RESP_OK_CAPSET UKVGPU_RESP_OK_CAPSET
#define VIRTIO_GPU_RESP_OK_EDID UKVGPU_RESP_OK_EDID
#define VIRTIO_GPU_RESP_ERR_UNSPEC UKVGPU_RESP_ERR_UNSPEC
#define VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID UKVGPU_RESP_ERR_INVALID_RESOURCE_ID

#define virtio_gpu_config ukvgpu_config
#define virtio_gpu_ctrl_hdr ukvgpu_ctrl_hdr
#define virtio_gpu_rect ukvgpu_rect
#define virtio_gpu_display_one ukvgpu_display_one
#define virtio_gpu_resp_display_info ukvgpu_resp_display_info
#define virtio_gpu_resource_create_2d ukvgpu_resource_create_2d
#define virtio_gpu_resource_unref ukvgpu_resource_unref
#define virtio_gpu_set_scanout ukvgpu_set_scanout
#define virtio_gpu_resource_flush ukvgpu_resource_flush
#define virtio_gpu_transfer_to_host_2d ukvgpu_transfer_to_host_2d
#define virtio_gpu_mem_entry ukvgpu_mem_entry
#define virtio_gpu_resource_attach_backing ukvgpu_resource_attach_backing
#define virtio_gpu_resource_detach_backing ukvgpu_resource_detach_backing
#define virtio_gpu_get_capset_info ukvgpu_get_capset_info
#define virtio_gpu_resp_capset_info ukvgpu_resp_capset_info
#define virtio_gpu_get_capset ukvgpu_get_capset
#define virtio_gpu_get_edid ukvgpu_get_edid
#define virtio_gpu_resp_edid ukvgpu_resp_edid
#define virtio_gpu_resource_assign_uuid ukvgpu_resource_assign_uuid

#define virtio_gpu_ctx_create ukvgpu_ctx_create
#define virtio_gpu_ctx_resource ukvgpu_ctx_resource
#define virtio_gpu_resource_create_3d ukvgpu_resource_create_3d
#define virtio_gpu_transfer_host_3d ukvgpu_transfer_host_3d
#define virtio_gpu_cmd_submit ukvgpu_cmd_submit_3d
#define virtio_gpu_resource_create_blob ukvgpu_resource_create_blob
#define virtio_gpu_resource_map_blob ukvgpu_resource_map_blob
#define virtio_gpu_resp_map_info ukvgpu_resp_map_info
#define virtio_gpu_resource_unmap_blob ukvgpu_resource_unmap_blob
