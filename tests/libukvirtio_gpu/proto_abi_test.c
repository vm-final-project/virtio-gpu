#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Native ABI contract for the planned real VirtIO-GPU backend.
 *
 * This test intentionally includes the future protocol header outside the
 * public uk/ API.  Until libs/libukvirtio_gpu/virtio_gpu_proto.h exists,
 * `make -C tests proto-abi` reports this test as BLOCKED instead of making
 * the current fake-backend native suite fail.
 */
#include "../../libs/libukvirtio_gpu/virtio_gpu_proto.h"

#define FAIL_IF(cond, code) \
	do { if (cond) { printf("proto_abi_test FAIL code=%d line=%d\n", (code), __LINE__); return (code); } } while (0)

#define ASSERT_SIZE(type, expected) \
	_Static_assert(sizeof(type) == (expected), #type " size must match VirtIO-GPU wire ABI")
#define ASSERT_OFF(type, member, expected) \
	_Static_assert(offsetof(type, member) == (expected), #type "." #member " offset must match VirtIO-GPU wire ABI")

#ifndef VIRTIO_GPU_FLAG_FENCE
#error "virtio_gpu_proto.h must define VIRTIO_GPU_FLAG_FENCE"
#endif
#ifndef VIRTIO_GPU_CMD_GET_DISPLAY_INFO
#error "virtio_gpu_proto.h must define command and response constants"
#endif
#ifndef VIRTIO_GPU_F_VIRGL
#error "virtio_gpu_proto.h must define VirtIO-GPU feature bits"
#endif

ASSERT_SIZE(struct virtio_gpu_ctrl_hdr, 24);
ASSERT_OFF(struct virtio_gpu_ctrl_hdr, type, 0);
ASSERT_OFF(struct virtio_gpu_ctrl_hdr, flags, 4);
ASSERT_OFF(struct virtio_gpu_ctrl_hdr, fence_id, 8);
ASSERT_OFF(struct virtio_gpu_ctrl_hdr, ctx_id, 16);
ASSERT_OFF(struct virtio_gpu_ctrl_hdr, ring_idx, 20);

ASSERT_SIZE(struct virtio_gpu_config, 20);
ASSERT_OFF(struct virtio_gpu_config, events_read, 0);
ASSERT_OFF(struct virtio_gpu_config, events_clear, 4);
ASSERT_OFF(struct virtio_gpu_config, num_scanouts, 8);
ASSERT_OFF(struct virtio_gpu_config, num_capsets, 12);
ASSERT_OFF(struct virtio_gpu_config, blob_alignment, 16);

ASSERT_SIZE(struct virtio_gpu_rect, 16);
ASSERT_OFF(struct virtio_gpu_rect, x, 0);
ASSERT_OFF(struct virtio_gpu_rect, y, 4);
ASSERT_OFF(struct virtio_gpu_rect, width, 8);
ASSERT_OFF(struct virtio_gpu_rect, height, 12);

ASSERT_SIZE(struct virtio_gpu_display_one, 24);
ASSERT_SIZE(struct virtio_gpu_resp_display_info, 408);
ASSERT_OFF(struct virtio_gpu_resp_display_info, pmodes, 24);

ASSERT_SIZE(struct virtio_gpu_resource_create_2d, 40);
ASSERT_OFF(struct virtio_gpu_resource_create_2d, resource_id, 24);
ASSERT_OFF(struct virtio_gpu_resource_create_2d, format, 28);
ASSERT_OFF(struct virtio_gpu_resource_create_2d, width, 32);
ASSERT_OFF(struct virtio_gpu_resource_create_2d, height, 36);

ASSERT_SIZE(struct virtio_gpu_resource_unref, 32);
ASSERT_OFF(struct virtio_gpu_resource_unref, resource_id, 24);

ASSERT_SIZE(struct virtio_gpu_set_scanout, 48);
ASSERT_OFF(struct virtio_gpu_set_scanout, r, 24);
ASSERT_OFF(struct virtio_gpu_set_scanout, scanout_id, 40);
ASSERT_OFF(struct virtio_gpu_set_scanout, resource_id, 44);

ASSERT_SIZE(struct virtio_gpu_resource_flush, 48);
ASSERT_OFF(struct virtio_gpu_resource_flush, r, 24);
ASSERT_OFF(struct virtio_gpu_resource_flush, resource_id, 40);

ASSERT_SIZE(struct virtio_gpu_transfer_to_host_2d, 56);
ASSERT_OFF(struct virtio_gpu_transfer_to_host_2d, r, 24);
ASSERT_OFF(struct virtio_gpu_transfer_to_host_2d, offset, 40);
ASSERT_OFF(struct virtio_gpu_transfer_to_host_2d, resource_id, 48);

ASSERT_SIZE(struct virtio_gpu_mem_entry, 16);
ASSERT_OFF(struct virtio_gpu_mem_entry, addr, 0);
ASSERT_OFF(struct virtio_gpu_mem_entry, length, 8);

ASSERT_SIZE(struct virtio_gpu_resource_attach_backing, 32);
ASSERT_OFF(struct virtio_gpu_resource_attach_backing, resource_id, 24);
ASSERT_OFF(struct virtio_gpu_resource_attach_backing, nr_entries, 28);

ASSERT_SIZE(struct virtio_gpu_resource_detach_backing, 32);
ASSERT_OFF(struct virtio_gpu_resource_detach_backing, resource_id, 24);

ASSERT_SIZE(struct virtio_gpu_get_capset_info, 32);
ASSERT_OFF(struct virtio_gpu_get_capset_info, capset_index, 24);

ASSERT_SIZE(struct virtio_gpu_resp_capset_info, 40);
ASSERT_OFF(struct virtio_gpu_resp_capset_info, capset_id, 24);
ASSERT_OFF(struct virtio_gpu_resp_capset_info, capset_max_version, 28);
ASSERT_OFF(struct virtio_gpu_resp_capset_info, capset_max_size, 32);

ASSERT_SIZE(struct virtio_gpu_get_capset, 32);
ASSERT_OFF(struct virtio_gpu_get_capset, capset_id, 24);
ASSERT_OFF(struct virtio_gpu_get_capset, capset_version, 28);

ASSERT_SIZE(struct virtio_gpu_get_edid, 32);
ASSERT_OFF(struct virtio_gpu_get_edid, scanout, 24);
ASSERT_SIZE(struct virtio_gpu_resp_edid, 1056);
ASSERT_OFF(struct virtio_gpu_resp_edid, size, 24);
ASSERT_OFF(struct virtio_gpu_resp_edid, edid, 32);

ASSERT_SIZE(struct virtio_gpu_resource_assign_uuid, 32);
ASSERT_OFF(struct virtio_gpu_resource_assign_uuid, resource_id, 24);
ASSERT_SIZE(struct virtio_gpu_ctx_create, 96);
ASSERT_OFF(struct virtio_gpu_ctx_create, context_init, 28);
ASSERT_SIZE(struct virtio_gpu_cmd_submit, 32);
ASSERT_SIZE(struct virtio_gpu_resource_create_blob, 56);
ASSERT_OFF(struct virtio_gpu_resource_create_blob, blob_mem, 28);
ASSERT_OFF(struct virtio_gpu_resource_create_blob, size, 48);
ASSERT_SIZE(struct virtio_gpu_resource_map_blob, 40);
ASSERT_SIZE(struct virtio_gpu_resp_map_info, 32);
ASSERT_SIZE(struct virtio_gpu_resource_unmap_blob, 32);

static int command_values_are_spec_values(void)
{
	FAIL_IF(VIRTIO_GPU_CMD_GET_DISPLAY_INFO != 0x0100u, 101);
	FAIL_IF(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D != 0x0101u, 102);
	FAIL_IF(VIRTIO_GPU_CMD_RESOURCE_UNREF != 0x0102u, 103);
	FAIL_IF(VIRTIO_GPU_CMD_SET_SCANOUT != 0x0103u, 104);
	FAIL_IF(VIRTIO_GPU_CMD_RESOURCE_FLUSH != 0x0104u, 105);
	FAIL_IF(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D != 0x0105u, 106);
	FAIL_IF(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING != 0x0106u, 107);
	FAIL_IF(VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING != 0x0107u, 108);
	FAIL_IF(VIRTIO_GPU_CMD_GET_CAPSET_INFO != 0x0108u, 109);
	FAIL_IF(VIRTIO_GPU_CMD_GET_CAPSET != 0x0109u, 110);
	FAIL_IF(VIRTIO_GPU_CMD_GET_EDID != 0x010au, 111);
	FAIL_IF(VIRTIO_GPU_RESP_OK_NODATA != 0x1100u, 112);
	FAIL_IF(VIRTIO_GPU_RESP_OK_DISPLAY_INFO != 0x1101u, 113);
	FAIL_IF(VIRTIO_GPU_RESP_OK_CAPSET_INFO != 0x1102u, 114);
	FAIL_IF(VIRTIO_GPU_RESP_OK_CAPSET != 0x1103u, 115);
	FAIL_IF(VIRTIO_GPU_RESP_OK_EDID != 0x1104u, 116);
	FAIL_IF(VIRTIO_GPU_RESP_ERR_UNSPEC != 0x1200u, 117);
	FAIL_IF(VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID != 0x1203u, 118);
	FAIL_IF(VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID != 0x010bu, 119);
	FAIL_IF(VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB != 0x010cu, 120);
	FAIL_IF(VIRTIO_GPU_CMD_CTX_CREATE != 0x0200u, 121);
	FAIL_IF(VIRTIO_GPU_CMD_SUBMIT_3D != 0x0207u, 122);
	FAIL_IF(VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB != 0x0208u, 123);
	FAIL_IF(VIRTIO_GPU_RESP_OK_MAP_INFO != 0x1106u, 124);
	return 0;
}

static int feature_values_are_spec_values(void)
{
	FAIL_IF(VIRTIO_GPU_FLAG_FENCE != 1u, 201);
	FAIL_IF(VIRTIO_GPU_F_VIRGL != 0u, 202);
	FAIL_IF(VIRTIO_GPU_F_EDID != 1u, 203);
	FAIL_IF(VIRTIO_GPU_F_RESOURCE_UUID != 2u, 204);
	FAIL_IF(VIRTIO_GPU_F_RESOURCE_BLOB != 3u, 205);
	FAIL_IF(VIRTIO_GPU_F_CONTEXT_INIT != 4u, 206);
	FAIL_IF(VIRTIO_GPU_F_BLOB_ALIGNMENT != 5u, 210);
	FAIL_IF(VIRTIO_GPU_BLOB_MEM_HOST3D != 0x0002u, 207);
	FAIL_IF(VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE != 0x0001u, 208);
	FAIL_IF(VIRTIO_GPU_MAP_CACHE_WC != 0x03u, 209);
	return 0;
}

int main(void)
{
	int rc;

	rc = command_values_are_spec_values();
	if (rc != 0)
		return rc;
	rc = feature_values_are_spec_values();
	if (rc != 0)
		return rc;

	printf("proto_abi_test passed ctrl_hdr=%zu display_info=%zu edid=%zu\n",
	       sizeof(struct virtio_gpu_ctrl_hdr),
	       sizeof(struct virtio_gpu_resp_display_info),
	       sizeof(struct virtio_gpu_resp_edid));
	return 0;
}
