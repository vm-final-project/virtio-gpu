/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drm/virtgpu_drm.h>
#include <uk/drm_virtgpu.h>
#include <uk/drm_virtgpu_fdio.h>

#include "test_harness.h"

int main(void)
{
	struct test_state t = { .suite = "virtgpu_drm_test" };
	struct uk_drm_virtgpu_dev dev;
	struct uk_drm_virtgpu_file file0;
	struct uk_drm_virtgpu_file file1;
	struct drm_virtgpu_getparam gp = { 0 };
	struct drm_virtgpu_resource_create_blob rb = { 0 };
	struct drm_virtgpu_map map = { 0 };
	struct drm_gem_close close_args = { 0 };
	uint64_t value = 0;
	uint8_t *mapped = NULL;
	int rc;

	TEST_CHECK(&t, "open core device", uk_drm_virtgpu_open(&dev, 0) == 0);
	TEST_CHECK(&t, "core vdev present", dev._vdev != NULL);
	TEST_CHECK(&t, "getparam 3d features",
		   uk_drm_virtgpu_getparam(&dev, VIRTGPU_PARAM_3D_FEATURES, &value) == 0 && value != 0);
	TEST_CHECK(&t, "getparam resource blob",
		   uk_drm_virtgpu_getparam(&dev, VIRTGPU_PARAM_RESOURCE_BLOB, &value) == 0 && value != 0);
	TEST_CHECK(&t, "context init venus",
		   uk_drm_virtgpu_context_init(&dev, VIRTGPU_CAPSET_VENUS, 1) == 0);
	TEST_CHECK(&t, "context marked initialized", dev.ctx_initialized);

	rb.blob_mem = VIRTGPU_BLOB_MEM_GUEST;
	rb.blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE;
	rb.size = 4096;
	TEST_CHECK(&t, "create blob via ioctl",
		   uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &rb) == 0);
	TEST_CHECK(&t, "blob handle assigned", rb.bo_handle != 0 && rb.res_handle != 0);

	map.handle = rb.bo_handle;
	TEST_CHECK(&t, "map blob", uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_VIRTGPU_MAP, &map) == 0);
	TEST_CHECK(&t, "map offset non-zero", map.offset != 0);
	mapped = (uint8_t *)(uintptr_t)map.offset;
	mapped[0] = 0xde;
	mapped[1] = 0xad;
	TEST_CHECK(&t, "mapped write/readback", mapped[0] == 0xde && mapped[1] == 0xad);

	{
		uint8_t cmd[8] = { 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 };
		TEST_CHECK(&t, "execbuffer", uk_drm_virtgpu_execbuffer(&dev, cmd, sizeof(cmd)) == 0);
	}
	TEST_CHECK(&t, "wait blob", uk_drm_virtgpu_wait(&dev, rb.bo_handle) == 0);

	close_args.handle = rb.bo_handle;
	TEST_CHECK(&t, "gem close", uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_GEM_CLOSE, &close_args) == 0);
	map.handle = rb.bo_handle;
	rc = uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_VIRTGPU_MAP, &map);
	TEST_CHECK(&t, "closed handle map fails", rc == -ENOENT);
	uk_drm_virtgpu_close(&dev);

	TEST_CHECK(&t, "render node path",
		   strcmp(uk_drm_virtgpu_render_node_path(), UK_DRM_VIRTGPU_RENDER_NODE_PATH) == 0);
	TEST_CHECK(&t, "open fdio file0", uk_drm_virtgpu_file_open(&file0, 0) == 0);
	TEST_CHECK(&t, "open fdio file1", uk_drm_virtgpu_file_open(&file1, 0) == 0);

	value = 0;
	gp.param = VIRTGPU_PARAM_RESOURCE_BLOB;
	gp.value = (uint64_t)(uintptr_t)&value;
	TEST_CHECK(&t, "fdio getparam",
		   uk_drm_virtgpu_file_ioctl(&file0, DRM_IOCTL_VIRTGPU_GETPARAM, &gp) == 0 && value != 0);

	rb = (struct drm_virtgpu_resource_create_blob) {
		.blob_mem = VIRTGPU_BLOB_MEM_GUEST,
		.blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE,
		.size = 8192,
	};
	TEST_CHECK(&t, "fdio create blob",
		   uk_drm_virtgpu_file_ioctl(&file0,
					     DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB,
					     &rb) == 0);
	map = (struct drm_virtgpu_map) { .handle = rb.bo_handle };
	TEST_CHECK(&t, "fdio map ioctl",
		   uk_drm_virtgpu_file_ioctl(&file0, DRM_IOCTL_VIRTGPU_MAP, &map) == 0);
	TEST_CHECK(&t, "fdio map page aligned", (map.offset & 0xfffu) == 0);
	TEST_CHECK(&t, "fdio mmap same file",
		   uk_drm_virtgpu_file_mmap(&file0, map.offset, 4096, (void **)&mapped) == 0 &&
		   mapped != NULL);
	rc = uk_drm_virtgpu_file_mmap(&file1, map.offset, 4096, (void **)&mapped);
	TEST_CHECK(&t, "fdio mmap cross file fails", rc == -ENOENT);

	close_args.handle = rb.bo_handle;
	TEST_CHECK(&t, "fdio gem close",
		   uk_drm_virtgpu_file_ioctl(&file0, DRM_IOCTL_GEM_CLOSE, &close_args) == 0);
	map = (struct drm_virtgpu_map) { .handle = rb.bo_handle };
	rc = uk_drm_virtgpu_file_ioctl(&file0, DRM_IOCTL_VIRTGPU_MAP, &map);
	TEST_CHECK(&t, "fdio closed handle map fails", rc == -ENOENT);

	uk_drm_virtgpu_file_close(&file1);
	uk_drm_virtgpu_file_close(&file0);
	return test_finish(&t);
}
