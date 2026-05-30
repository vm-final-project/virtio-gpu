/* SPDX-License-Identifier: MIT */
/*
 * virtgpu_drm.h — subset of Linux 6.18 include/uapi/drm/virtgpu_drm.h
 *
 * Source: /home/jerrytsai/linux-version/linux-6.18/include/uapi/drm/virtgpu_drm.h
 * Reproduced for the libukvirtgpu_drm UAPI shim (vk.drm-shim gate).
 * Only the definitions used by Mesa Venus guest driver are retained.
 */
#ifndef VIRTGPU_DRM_H
#define VIRTGPU_DRM_H

#include <stdint.h>

/* Provide _IO/_IOR/_IOW/_IOWR if not already defined (host native builds). */
#ifndef _IOWR
#  include <sys/ioctl.h>
#endif

/* ── GETPARAM parameter IDs ───────────────────────────────────────────────── */
#define VIRTGPU_PARAM_3D_FEATURES        1
#define VIRTGPU_PARAM_CAPSET_QUERY_FIX   2
#define VIRTGPU_PARAM_RESOURCE_BLOB      3
#define VIRTGPU_PARAM_HOST_VISIBLE       4
#define VIRTGPU_PARAM_CROSS_DEVICE       5
#define VIRTGPU_PARAM_CONTEXT_INIT       6
#define VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs 7
#define VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME 8

/* ── EXECBUFFER flags ─────────────────────────────────────────────────────── */
#define VIRTGPU_EXECBUF_FENCE_FD_IN  0x01
#define VIRTGPU_EXECBUF_FENCE_FD_OUT 0x02
#define VIRTGPU_EXECBUF_RING_IDX     0x04

/* ── RESOURCE_CREATE_BLOB memory types ───────────────────────────────────── */
#define VIRTGPU_BLOB_MEM_GUEST        0x0001
#define VIRTGPU_BLOB_MEM_HOST3D       0x0002
#define VIRTGPU_BLOB_MEM_HOST3D_GUEST 0x0003
#define VIRTGPU_BLOB_FLAG_USE_MAPPABLE  0x0001
#define VIRTGPU_BLOB_FLAG_USE_SHAREABLE 0x0002
#define VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004

/* ── CONTEXT_INIT capset IDs ─────────────────────────────────────────────── */
#define VIRTGPU_CONTEXT_PARAM_CAPSET_ID     0x0001
#define VIRTGPU_CONTEXT_PARAM_NUM_RINGS     0x0002
#define VIRTGPU_CONTEXT_PARAM_POLL_RINGS_MASK 0x0003
#define VIRTGPU_CONTEXT_PARAM_DEBUG_NAME    0x0004
#define VIRTGPU_CAPSET_VENUS  4  /* Venus/Vulkan */

/* ── Wire structures ─────────────────────────────────────────────────────── */

struct drm_virtgpu_map {
    uint64_t offset;
    uint32_t handle;
    uint32_t pad;
};

struct drm_virtgpu_execbuffer {
    uint32_t flags;
    uint32_t size;
    uint64_t command;
    uint64_t bo_handles;
    uint32_t num_bo_handles;
    int32_t  fence_fd;
    uint32_t ring_idx;
    uint32_t syncobj_stride;
    uint32_t num_syncobjs;
    uint32_t pad;
    uint64_t syncobjs;
};

struct drm_virtgpu_getparam {
    uint64_t param;
    uint64_t value;
};

struct drm_virtgpu_resource_create {
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
    uint32_t bo_handle;
    uint32_t res_handle;
    uint32_t size;
    uint32_t stride;
};

struct drm_virtgpu_resource_info {
    uint32_t bo_handle;
    uint32_t res_handle;
    uint32_t size;
    uint32_t blob_mem;
};

struct drm_virtgpu_3d_box {
    uint32_t x, y, z;
    uint32_t w, h, d;
};

struct drm_virtgpu_3d_transfer_to_host {
    struct drm_virtgpu_3d_box box;
    uint32_t res_handle;
    uint32_t level;
    uint32_t offset;
    uint32_t stride;
    uint32_t layer_stride;
};

struct drm_virtgpu_3d_transfer_from_host {
    struct drm_virtgpu_3d_box box;
    uint32_t res_handle;
    uint32_t level;
    uint32_t offset;
    uint32_t stride;
    uint32_t layer_stride;
};

struct drm_virtgpu_3d_wait {
    uint32_t handle;
    uint32_t flags;
};

struct drm_virtgpu_get_caps {
    uint32_t cap_set_id;
    uint32_t cap_set_ver;
    uint64_t addr;
    uint32_t size;
    uint32_t pad;
};

struct drm_virtgpu_resource_create_blob {
    uint32_t blob_mem;
    uint32_t blob_flags;
    uint32_t bo_handle;
    uint32_t res_handle;
    uint64_t size;
    uint32_t pad;
    uint32_t cmd_size;
    uint64_t cmd;
    uint64_t blob_id;
};

struct drm_virtgpu_context_set_param {
    uint64_t param;
    uint64_t value;
};

struct drm_virtgpu_context_init {
    uint32_t num_params;
    uint32_t pad;
    uint64_t ctx_set_params;
};

/* ── Ioctl command numbers (matching Linux DRM_COMMAND_BASE=0x40) ─────────── */
#define DRM_VIRTGPU_MAP                 0x00
#define DRM_VIRTGPU_EXECBUFFER          0x01
#define DRM_VIRTGPU_GETPARAM            0x02
#define DRM_VIRTGPU_RESOURCE_CREATE     0x03
#define DRM_VIRTGPU_RESOURCE_INFO       0x04
#define DRM_VIRTGPU_TRANSFER_FROM_HOST  0x05
#define DRM_VIRTGPU_TRANSFER_TO_HOST    0x06
#define DRM_VIRTGPU_WAIT                0x07
#define DRM_VIRTGPU_GET_CAPS            0x08
#define DRM_VIRTGPU_RESOURCE_CREATE_BLOB 0x09
#define DRM_VIRTGPU_CONTEXT_INIT        0x0a

#define DRM_COMMAND_BASE 0x40
#define DRM_IOCTL_BASE   'd'

#define DRM_IO(nr)          _IO(DRM_IOCTL_BASE, nr)
#define DRM_IOR(nr,type)    _IOR(DRM_IOCTL_BASE, nr, type)
#define DRM_IOW(nr,type)    _IOW(DRM_IOCTL_BASE, nr, type)
#define DRM_IOWR(nr,type)   _IOWR(DRM_IOCTL_BASE, nr, type)

#define DRM_IOCTL_VIRTGPU_MAP \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_MAP, struct drm_virtgpu_map)
#define DRM_IOCTL_VIRTGPU_EXECBUFFER \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_EXECBUFFER, struct drm_virtgpu_execbuffer)
#define DRM_IOCTL_VIRTGPU_GETPARAM \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_GETPARAM, struct drm_virtgpu_getparam)
#define DRM_IOCTL_VIRTGPU_RESOURCE_CREATE \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_CREATE, struct drm_virtgpu_resource_create)
#define DRM_IOCTL_VIRTGPU_RESOURCE_INFO \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_INFO, struct drm_virtgpu_resource_info)
#define DRM_IOCTL_VIRTGPU_TRANSFER_FROM_HOST \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_TRANSFER_FROM_HOST, struct drm_virtgpu_3d_transfer_from_host)
#define DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_TRANSFER_TO_HOST, struct drm_virtgpu_3d_transfer_to_host)
#define DRM_IOCTL_VIRTGPU_WAIT \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_WAIT, struct drm_virtgpu_3d_wait)
#define DRM_IOCTL_VIRTGPU_GET_CAPS \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_GET_CAPS, struct drm_virtgpu_get_caps)
#define DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_CREATE_BLOB, struct drm_virtgpu_resource_create_blob)
#define DRM_IOCTL_VIRTGPU_CONTEXT_INIT \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_CONTEXT_INIT, struct drm_virtgpu_context_init)

#endif /* VIRTGPU_DRM_H */
