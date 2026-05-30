/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * libukvirtgpu_drm — Mesa/Linux virtgpu UAPI shim for Unikraft (vk.drm-shim gate)
 *
 * Translates Linux DRM_IOCTL_VIRTGPU_* calls into VirtIO-GPU protocol
 * commands via libukvirtio_gpu. Provides the ioctl surface expected by
 * Mesa's Venus guest driver (src/virtio/vulkan/vn_renderer_virtgpu.c).
 *
 * Source lineage:
 *   Linux 6.18: include/uapi/drm/virtgpu_drm.h
 *   Mesa: src/virtio/vulkan/vn_renderer_virtgpu.c
 *   virglrenderer: src/venus/
 */
#ifndef UK_DRM_VIRTGPU_H
#define UK_DRM_VIRTGPU_H

#include <stdint.h>
#include <stddef.h>

#include <drm/virtgpu_drm.h>
#include <uk/virtio_gpu.h>

/* ── Handle table entry ──────────────────────────────────────────────────── */

#define UK_DRM_VIRTGPU_MAX_HANDLES 256

struct uk_drm_virtgpu_bo {
    uint32_t bo_handle;   /* DRM GEM handle (1-based table index) */
    uint32_t res_handle;  /* VirtIO-GPU resource ID */
    uint64_t size;
    int      is_blob;
    int      is_host_visible;
    uint32_t blob_mem;
    uint32_t blob_flags;
    void    *mapped;      /* host-coherent mapping, NULL if unmapped */
    struct uk_virtio_gpu_blob _blob; /* underlying blob object */
};

struct uk_drm_virtgpu_dev {
    /* GETPARAM truth table (set during open/probe) */
    int has_3d_features;
    int has_resource_blob;
    int has_host_visible;
    int has_context_init;
    uint64_t supported_capset_ids; /* bitmask */

    /* Handle table */
    struct uk_drm_virtgpu_bo handles[UK_DRM_VIRTGPU_MAX_HANDLES];
    uint32_t next_bo_handle;

    /* Context state */
    uint32_t ctx_id;
    int      ctx_initialized;
    uint32_t capset_id;  /* e.g. VIRTGPU_CAPSET_VENUS=4 */

    /* Underlying VirtIO-GPU device and context (set by open) */
    struct uk_virtio_gpu_dev     *_vdev;
    struct uk_virtio_gpu_context  _ctx;
};

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * Initialize the virtgpu device shim against the VirtIO-GPU device at
 * index `gpu_idx`. Probes capabilities and fills the GETPARAM table.
 * Returns 0 on success, negative on error.
 */
int uk_drm_virtgpu_open(struct uk_drm_virtgpu_dev *dev, uint32_t gpu_idx);

/*
 * Dispatch a DRM_IOCTL_VIRTGPU_* ioctl.
 * `request` is the ioctl number (DRM_IOCTL_VIRTGPU_GETPARAM, etc.).
 * `arg` is a pointer to the corresponding wire struct.
 * Returns 0 on success, -errno on failure.
 */
int uk_drm_virtgpu_ioctl(struct uk_drm_virtgpu_dev *dev,
                          unsigned long request, void *arg);

/* Convenience wrappers for the most-used ioctls */
int uk_drm_virtgpu_getparam(struct uk_drm_virtgpu_dev *dev,
                             uint64_t param, uint64_t *value);
int uk_drm_virtgpu_context_init(struct uk_drm_virtgpu_dev *dev,
                                 uint32_t capset_id, uint32_t num_rings);
int uk_drm_virtgpu_execbuffer(struct uk_drm_virtgpu_dev *dev,
                               const void *cmd, uint32_t cmd_size);
int uk_drm_virtgpu_resource_create_blob(struct uk_drm_virtgpu_dev *dev,
                                         uint32_t blob_mem, uint32_t blob_flags,
                                         uint64_t size,
                                         uint32_t *bo_handle_out,
                                         uint32_t *res_handle_out);
int uk_drm_virtgpu_map(struct uk_drm_virtgpu_dev *dev,
                        uint32_t bo_handle, uint64_t *offset_out);
int uk_drm_virtgpu_wait(struct uk_drm_virtgpu_dev *dev, uint32_t bo_handle);

/* Close and release all resources */
void uk_drm_virtgpu_close(struct uk_drm_virtgpu_dev *dev);

#endif /* UK_DRM_VIRTGPU_H */
