/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * fd-compatible facade for the virtgpu DRM translator.
 *
 * The core translator returns direct CPU pointers for MAP because native VOGUE
 * callers do not have a Linux fd/mmap sequence. This facade is the compatibility
 * boundary: MAP returns an opaque page-aligned offset and file_mmap resolves it
 * through per-open state.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <uk/drm_virtgpu_fdio.h>

#define UK_DRM_VIRTGPU_PAGE_SIZE 4096ull
#define UK_DRM_VIRTGPU_MMAP_BASE 0x100000000ull

static uint64_t align_up_u64(uint64_t val, uint64_t align)
{
	return (val + align - 1u) & ~(align - 1u);
}

static struct uk_drm_virtgpu_mmap_entry *
mmap_lookup_handle(struct uk_drm_virtgpu_file *file, uint32_t bo_handle)
{
	for (size_t i = 0; i < UK_DRM_VIRTGPU_FDIO_MAX_MMAPS; i++) {
		struct uk_drm_virtgpu_mmap_entry *e = &file->mmaps[i];
		if (e->bo_handle == bo_handle && e->ptr)
			return e;
	}
	return NULL;
}

static struct uk_drm_virtgpu_mmap_entry *
mmap_alloc(struct uk_drm_virtgpu_file *file)
{
	for (size_t i = 0; i < UK_DRM_VIRTGPU_FDIO_MAX_MMAPS; i++) {
		struct uk_drm_virtgpu_mmap_entry *e = &file->mmaps[i];
		if (!e->bo_handle)
			return e;
	}
	return NULL;
}

static void mmap_remove_handle(struct uk_drm_virtgpu_file *file,
			       uint32_t bo_handle)
{
	for (size_t i = 0; i < UK_DRM_VIRTGPU_FDIO_MAX_MMAPS; i++) {
		struct uk_drm_virtgpu_mmap_entry *e = &file->mmaps[i];
		if (e->bo_handle == bo_handle)
			memset(e, 0, sizeof(*e));
	}
}

const char *uk_drm_virtgpu_render_node_path(void)
{
	return UK_DRM_VIRTGPU_RENDER_NODE_PATH;
}

int uk_drm_virtgpu_file_open(struct uk_drm_virtgpu_file *file,
			      uint32_t gpu_idx)
{
	int rc;

	if (!file)
		return -EINVAL;

	memset(file, 0, sizeof(*file));
	file->next_offset = UK_DRM_VIRTGPU_MMAP_BASE;

	rc = uk_drm_virtgpu_open(&file->dev, gpu_idx);
	if (rc)
		return rc;

	file->opened = 1;
	return 0;
}

int uk_drm_virtgpu_file_ioctl(struct uk_drm_virtgpu_file *file,
			       unsigned long request, void *arg)
{
	if (!file || !file->opened || !arg)
		return -EINVAL;

	if (request == DRM_IOCTL_VIRTGPU_MAP) {
		struct drm_virtgpu_map *map = arg;
		struct drm_virtgpu_resource_info info = {
			.bo_handle = map->handle,
		};
		struct uk_drm_virtgpu_mmap_entry *e;
		uint64_t ptr_as_offset = 0;
		uint64_t size;
		int rc;

		e = mmap_lookup_handle(file, map->handle);
		if (e) {
			map->offset = e->offset;
			return 0;
		}

		rc = uk_drm_virtgpu_resource_info(&file->dev, map->handle, &info);
		if (rc)
			return rc;

		rc = uk_drm_virtgpu_map(&file->dev, map->handle, &ptr_as_offset);
		if (rc)
			return rc;

		e = mmap_alloc(file);
		if (!e)
			return -ENOMEM;

		size = align_up_u64(info.size, UK_DRM_VIRTGPU_PAGE_SIZE);
		e->offset = file->next_offset;
		e->size = size;
		e->bo_handle = map->handle;
		e->ptr = (void *)(uintptr_t)ptr_as_offset;
		file->next_offset += size ? size : UK_DRM_VIRTGPU_PAGE_SIZE;
		file->next_offset = align_up_u64(file->next_offset,
						 UK_DRM_VIRTGPU_PAGE_SIZE);

		map->offset = e->offset;
		return 0;
	}

	if (request == DRM_IOCTL_GEM_CLOSE) {
		struct drm_gem_close *close = arg;
		int rc = uk_drm_virtgpu_ioctl(&file->dev, request, arg);
		if (!rc)
			mmap_remove_handle(file, close->handle);
		return rc;
	}

	return uk_drm_virtgpu_ioctl(&file->dev, request, arg);
}

int uk_drm_virtgpu_file_mmap(struct uk_drm_virtgpu_file *file,
			      uint64_t offset, size_t len, void **addr_out)
{
	if (!file || !file->opened || !addr_out || len == 0)
		return -EINVAL;
	if (offset & (UK_DRM_VIRTGPU_PAGE_SIZE - 1u))
		return -EINVAL;

	for (size_t i = 0; i < UK_DRM_VIRTGPU_FDIO_MAX_MMAPS; i++) {
		struct uk_drm_virtgpu_mmap_entry *e = &file->mmaps[i];
		uint64_t delta;

		if (!e->bo_handle || !e->ptr)
			continue;
		if (offset < e->offset || offset >= e->offset + e->size)
			continue;

		delta = offset - e->offset;
		if ((uint64_t)len > e->size - delta)
			return -EINVAL;

		*addr_out = (void *)((uintptr_t)e->ptr + (uintptr_t)delta);
		return 0;
	}

	return -ENOENT;
}

void uk_drm_virtgpu_file_close(struct uk_drm_virtgpu_file *file)
{
	if (!file || !file->opened)
		return;

	uk_drm_virtgpu_close(&file->dev);
	memset(file, 0, sizeof(*file));
}
