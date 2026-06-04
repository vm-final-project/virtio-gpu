/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * fd-compatible render-node facade for libukvirtgpu_drm.
 *
 * The host-native API below mirrors the state owned by a Unikraft uk_file/devfs
 * render-node open: each opened file has its own DRM translator state and mmap
 * offset registry. Real fd allocation/devfs publication can wrap these calls.
 */
#ifndef UK_DRM_VIRTGPU_FDIO_H
#define UK_DRM_VIRTGPU_FDIO_H

#include <stddef.h>
#include <stdint.h>

#include <uk/drm_virtgpu.h>

#define UK_DRM_VIRTGPU_RENDER_NODE_PATH "/dev/dri/renderD128"
#define UK_DRM_VIRTGPU_FDIO_MAX_MMAPS 256

struct uk_drm_virtgpu_mmap_entry {
	uint64_t offset;
	uint64_t size;
	uint32_t bo_handle;
	void *ptr;
};

struct uk_drm_virtgpu_file {
	struct uk_drm_virtgpu_dev dev;
	struct uk_drm_virtgpu_mmap_entry mmaps[UK_DRM_VIRTGPU_FDIO_MAX_MMAPS];
	uint64_t next_offset;
	int opened;
};

const char *uk_drm_virtgpu_render_node_path(void);
int uk_drm_virtgpu_file_open(struct uk_drm_virtgpu_file *file,
			      uint32_t gpu_idx);
int uk_drm_virtgpu_file_ioctl(struct uk_drm_virtgpu_file *file,
			       unsigned long request, void *arg);
int uk_drm_virtgpu_file_mmap(struct uk_drm_virtgpu_file *file,
			      uint64_t offset, size_t len, void **addr_out);
void uk_drm_virtgpu_file_close(struct uk_drm_virtgpu_file *file);

#endif /* UK_DRM_VIRTGPU_FDIO_H */
