/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * virtgpu_drm_fdio_test - fd-compatible render-node facade for DRM UAPI.
 *
 * This test models the path Mesa expects:
 *   open("/dev/dri/renderD128") -> ioctl(fd, DRM_IOCTL_*) -> mmap(fd, offset)
 *
 * Host-native tests do not allocate a real Unikraft fd, but exercise the same
 * per-open state and callback boundary that the devfs/fdio glue uses.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <drm/virtgpu_drm.h>
#include <uk/drm_virtgpu_fdio.h>

#define PASS(label) do { printf("  PASS  %s\n", label); } while (0)
#define FAIL(label, rc) do { \
	printf("  FAIL  %s  rc=%d\n", label, rc); \
	return 1; \
} while (0)
#define CHECK(label, expr) do { \
	int _rc = (expr); \
	if (_rc != 0) FAIL(label, _rc); \
	PASS(label); \
} while (0)
#define CHECK_NZ(label, val) do { \
	if (!(val)) FAIL(label, 0); \
	PASS(label); \
} while (0)

int main(void)
{
	struct uk_drm_virtgpu_file f0;
	struct uk_drm_virtgpu_file f1;
	uint64_t value = 0;
	uint32_t bo = 0;
	void *mapped = NULL;
	int rc;

	printf("virtgpu_drm_fdio_test: fdio-style DRM render-node facade\n");

	CHECK("render_node_path",
	      uk_drm_virtgpu_render_node_path() &&
	      uk_drm_virtgpu_render_node_path()[0] == '/' ? 0 : -EINVAL);

	CHECK("open:f0", uk_drm_virtgpu_file_open(&f0, 0));
	CHECK("open:f1", uk_drm_virtgpu_file_open(&f1, 0));

	{
		struct drm_virtgpu_getparam gp = {
			.param = VIRTGPU_PARAM_RESOURCE_BLOB,
			.value = (uint64_t)(uintptr_t)&value,
		};
		CHECK("file_ioctl:GETPARAM",
		      uk_drm_virtgpu_file_ioctl(&f0, DRM_IOCTL_VIRTGPU_GETPARAM, &gp));
		CHECK_NZ("file_ioctl:GETPARAM:value", value);
	}

	{
		struct drm_virtgpu_resource_create_blob rb = {
			.blob_mem = VIRTGPU_BLOB_MEM_GUEST,
			.blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE,
			.size = 8192,
		};
		CHECK("file_ioctl:RESOURCE_CREATE_BLOB",
		      uk_drm_virtgpu_file_ioctl(&f0,
			  DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &rb));
		CHECK_NZ("file_ioctl:RESOURCE_CREATE_BLOB:bo", rb.bo_handle);
		bo = rb.bo_handle;
	}

	{
		struct drm_virtgpu_map map = { .handle = bo };
		CHECK("file_ioctl:MAP",
		      uk_drm_virtgpu_file_ioctl(&f0, DRM_IOCTL_VIRTGPU_MAP, &map));
		CHECK_NZ("file_ioctl:MAP:offset", map.offset);
		if (map.offset & 0xffful)
			FAIL("file_ioctl:MAP:page_aligned_offset", (int)map.offset);
		PASS("file_ioctl:MAP:page_aligned_offset");

		CHECK("file_mmap:resolve_same_fd",
		      uk_drm_virtgpu_file_mmap(&f0, map.offset, 4096, &mapped));
		CHECK_NZ("file_mmap:resolve_same_fd:pointer", mapped);

		rc = uk_drm_virtgpu_file_mmap(&f1, map.offset, 4096, &mapped);
		if (rc != -ENOENT)
			FAIL("file_mmap:cross_fd_offset returns ENOENT", rc);
		PASS("file_mmap:cross_fd_offset returns ENOENT");

		rc = uk_drm_virtgpu_file_mmap(&f0, map.offset + 8192, 4096, &mapped);
		if (rc != -ENOENT)
			FAIL("file_mmap:unknown_offset returns ENOENT", rc);
		PASS("file_mmap:unknown_offset returns ENOENT");
	}

	{
		struct drm_gem_close close_args = { .handle = bo };
		struct drm_virtgpu_map map = { .handle = bo };
		CHECK("file_ioctl:GEM_CLOSE",
		      uk_drm_virtgpu_file_ioctl(&f0, DRM_IOCTL_GEM_CLOSE, &close_args));
		rc = uk_drm_virtgpu_file_ioctl(&f0, DRM_IOCTL_VIRTGPU_MAP, &map);
		if (rc != -ENOENT)
			FAIL("file_ioctl:GEM_CLOSE:map returns ENOENT", rc);
		PASS("file_ioctl:GEM_CLOSE:map returns ENOENT");
	}

	uk_drm_virtgpu_file_close(&f1);
	uk_drm_virtgpu_file_close(&f0);
	PASS("close");

	printf("virtgpu_drm_fdio_test: all checks passed\n");
	return 0;
}
