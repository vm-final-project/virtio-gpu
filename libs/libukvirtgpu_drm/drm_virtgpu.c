/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * libukvirtgpu_drm — Mesa/Linux virtgpu UAPI shim for Unikraft (vk.drm-shim gate)
 *
 * Translates DRM_IOCTL_VIRTGPU_* calls into VirtIO-GPU protocol commands
 * via libukvirtio_gpu. Provides the ioctl surface expected by Mesa's Venus
 * guest driver (src/virtio/vulkan/vn_renderer_virtgpu.c).
 */
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include <uk/drm_virtgpu.h>
#include <uk/virtio_gpu.h>

/* Extract virtgpu command number from a DRM ioctl request value.
 * The nr field occupies bits 0-7; DRM_COMMAND_BASE (0x40) offsets the
 * virtgpu-specific commands from the generic DRM command space. */
#define VIRTGPU_CMD(req) (((unsigned int)(req) & 0xFFu) - DRM_COMMAND_BASE)

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static struct uk_drm_virtgpu_bo *bo_alloc(struct uk_drm_virtgpu_dev *dev,
                                           uint32_t *handle_out)
{
    uint32_t h = dev->next_bo_handle;
    if (h >= UK_DRM_VIRTGPU_MAX_HANDLES)
        return NULL;
    dev->next_bo_handle++;
    struct uk_drm_virtgpu_bo *bo = &dev->handles[h];
    memset(bo, 0, sizeof(*bo));
    bo->bo_handle = h + 1; /* 1-based public handle */
    *handle_out = bo->bo_handle;
    return bo;
}

static struct uk_drm_virtgpu_bo *bo_lookup(struct uk_drm_virtgpu_dev *dev,
                                            uint32_t handle)
{
    if (handle == 0 || handle > dev->next_bo_handle)
        return NULL;
    struct uk_drm_virtgpu_bo *bo = &dev->handles[handle - 1];
    if (bo->bo_handle != handle)
        return NULL;
    return bo;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int uk_drm_virtgpu_open(struct uk_drm_virtgpu_dev *dev, uint32_t gpu_idx)
{
    struct uk_virtio_gpu_caps caps;
    struct uk_virtio_gpu_capset_info ci;
    int rc;

    (void)gpu_idx;
    memset(dev, 0, sizeof(*dev));
    dev->next_bo_handle = 0;

    rc = uk_virtio_gpu_probe(&dev->_vdev);
    if (rc)
        return rc;

    rc = uk_virtio_gpu_gl_caps_get(dev->_vdev, &caps);
    if (rc)
        return rc;

    dev->has_3d_features    = caps.has_virgl;
    dev->has_resource_blob  = caps.has_resource_blob;
    dev->has_host_visible   = caps.has_host_visible;
    dev->has_context_init   = caps.has_context_init;

    /* Build supported-capset bitmask from capset enumeration. */
    for (uint32_t i = 0; i < caps.num_capsets && i < 64u; i++) {
        if (uk_virtio_gpu_gl_capset_info_get(dev->_vdev, i, &ci) == 0
                && ci.id < 64u)
            dev->supported_capset_ids |= (1ull << ci.id);
    }

    return 0;
}

int uk_drm_virtgpu_getparam(struct uk_drm_virtgpu_dev *dev,
                             uint64_t param, uint64_t *value)
{
    if (!dev || !value)
        return -EINVAL;
    switch (param) {
    case VIRTGPU_PARAM_3D_FEATURES:
        *value = (uint64_t)dev->has_3d_features;
        break;
    case VIRTGPU_PARAM_CAPSET_QUERY_FIX:
        *value = 1;
        break;
    case VIRTGPU_PARAM_RESOURCE_BLOB:
        *value = (uint64_t)dev->has_resource_blob;
        break;
    case VIRTGPU_PARAM_HOST_VISIBLE:
        *value = (uint64_t)dev->has_host_visible;
        break;
    case VIRTGPU_PARAM_CROSS_DEVICE:
        *value = 0;
        break;
    case VIRTGPU_PARAM_CONTEXT_INIT:
        *value = (uint64_t)dev->has_context_init;
        break;
    case VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs:
        *value = dev->supported_capset_ids;
        break;
    case VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME:
        *value = 0;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

int uk_drm_virtgpu_context_init(struct uk_drm_virtgpu_dev *dev,
                                 uint32_t capset_id, uint32_t num_rings)
{
    int rc;
    (void)num_rings;

    if (!dev)
        return -EINVAL;
    if (dev->ctx_initialized)
        return -EEXIST;

    rc = uk_virtio_gpu_gl_context_create(dev->_vdev, capset_id,
                                          "uk-drm-virtgpu", &dev->_ctx);
    if (rc)
        return rc;

    dev->ctx_id          = dev->_ctx.id;
    dev->capset_id       = capset_id;
    dev->ctx_initialized = 1;
    return 0;
}

int uk_drm_virtgpu_execbuffer(struct uk_drm_virtgpu_dev *dev,
                               const void *cmd, uint32_t cmd_size)
{
    uk_gpu_fence_id fence;

    if (!dev || !dev->ctx_initialized)
        return -EINVAL;
    if (!cmd || cmd_size == 0)
        return -EINVAL;

    return uk_virtio_gpu_gl_context_submit(dev->_vdev, &dev->_ctx,
                                            cmd, cmd_size, &fence);
}

int uk_drm_virtgpu_resource_create_blob(struct uk_drm_virtgpu_dev *dev,
                                         uint32_t blob_mem,
                                         uint32_t blob_flags,
                                         uint64_t size,
                                         uint32_t *bo_handle_out,
                                         uint32_t *res_handle_out)
{
    struct uk_drm_virtgpu_bo *bo;
    uint32_t handle;
    int rc;
    static uint64_t next_blob_id = 1;

    if (!dev || !bo_handle_out || !res_handle_out || size == 0)
        return -EINVAL;

    bo = bo_alloc(dev, &handle);
    if (!bo)
        return -ENOMEM;

    rc = uk_virtio_gpu_gl_blob_create(dev->_vdev, size, blob_mem, blob_flags,
                                       next_blob_id++, &bo->_blob);
    if (rc) {
        bo->bo_handle = 0;
        dev->next_bo_handle--;
        return rc;
    }

    bo->res_handle      = bo->_blob.resource_id;
    bo->size            = size;
    bo->is_blob         = 1;
    bo->is_host_visible = (blob_flags & VIRTGPU_BLOB_FLAG_USE_MAPPABLE) ? 1 : 0;
    bo->blob_mem        = blob_mem;
    bo->blob_flags      = blob_flags;

    *bo_handle_out  = handle;
    *res_handle_out = bo->res_handle;
    return 0;
}

int uk_drm_virtgpu_map(struct uk_drm_virtgpu_dev *dev,
                        uint32_t bo_handle, uint64_t *offset_out)
{
    struct uk_drm_virtgpu_bo *bo;
    int rc;

    if (!dev || !offset_out)
        return -EINVAL;

    bo = bo_lookup(dev, bo_handle);
    if (!bo || !bo->is_blob)
        return -ENOENT;

    if (!bo->mapped) {
        rc = uk_virtio_gpu_gl_blob_map(dev->_vdev, &bo->_blob);
        if (rc)
            return rc;
        bo->mapped = bo->_blob.mapped_addr;
    }

    /* Return the CPU virtual address as the "mmap offset".
     * In the Unikraft unikernel context there is no fd/mmap; callers
     * use this address directly as the host-coherent pointer. */
    *offset_out = (uint64_t)(uintptr_t)bo->mapped;
    return 0;
}

int uk_drm_virtgpu_wait(struct uk_drm_virtgpu_dev *dev, uint32_t bo_handle)
{
    (void)dev;
    (void)bo_handle;
    /* The fake backend is synchronous; the real driver inserts a fence
     * on execbuffer submission. For now, return immediately. */
    return 0;
}

/* ── Generic ioctl dispatcher ────────────────────────────────────────────── */

int uk_drm_virtgpu_ioctl(struct uk_drm_virtgpu_dev *dev,
                          unsigned long request, void *arg)
{
    if (!dev || !arg)
        return -EINVAL;

    switch (VIRTGPU_CMD(request)) {

    case DRM_VIRTGPU_GETPARAM: {
        struct drm_virtgpu_getparam *p = arg;
        return uk_drm_virtgpu_getparam(dev, p->param, &p->value);
    }

    case DRM_VIRTGPU_CONTEXT_INIT: {
        struct drm_virtgpu_context_init *ci = arg;
        /* Parse the param array pointed to by ctx_set_params. */
        const struct drm_virtgpu_context_set_param *params =
            (const struct drm_virtgpu_context_set_param *)(uintptr_t)ci->ctx_set_params;
        uint32_t capset_id = VIRTGPU_CAPSET_VENUS;
        uint32_t num_rings = 1;
        for (uint32_t i = 0; i < ci->num_params && params; i++) {
            if (params[i].param == VIRTGPU_CONTEXT_PARAM_CAPSET_ID)
                capset_id = (uint32_t)params[i].value;
            else if (params[i].param == VIRTGPU_CONTEXT_PARAM_NUM_RINGS)
                num_rings = (uint32_t)params[i].value;
        }
        return uk_drm_virtgpu_context_init(dev, capset_id, num_rings);
    }

    case DRM_VIRTGPU_EXECBUFFER: {
        struct drm_virtgpu_execbuffer *eb = arg;
        const void *cmd = (const void *)(uintptr_t)eb->command;
        return uk_drm_virtgpu_execbuffer(dev, cmd, eb->size);
    }

    case DRM_VIRTGPU_RESOURCE_CREATE_BLOB: {
        struct drm_virtgpu_resource_create_blob *rb = arg;
        return uk_drm_virtgpu_resource_create_blob(
            dev, rb->blob_mem, rb->blob_flags, rb->size,
            &rb->bo_handle, &rb->res_handle);
    }

    case DRM_VIRTGPU_MAP: {
        struct drm_virtgpu_map *m = arg;
        return uk_drm_virtgpu_map(dev, m->handle, &m->offset);
    }

    case DRM_VIRTGPU_WAIT: {
        struct drm_virtgpu_3d_wait *w = arg;
        return uk_drm_virtgpu_wait(dev, w->handle);
    }

    default:
        return -ENOSYS;
    }
}

/* ── Teardown ────────────────────────────────────────────────────────────── */

void uk_drm_virtgpu_close(struct uk_drm_virtgpu_dev *dev)
{
    if (!dev)
        return;

    /* Unmap and destroy all live blobs. */
    for (uint32_t i = 0; i < dev->next_bo_handle; i++) {
        struct uk_drm_virtgpu_bo *bo = &dev->handles[i];
        if (!bo->is_blob || !bo->_blob.created)
            continue;
        if (bo->_blob.mapped)
            uk_virtio_gpu_gl_blob_unmap(dev->_vdev, &bo->_blob);
        uk_virtio_gpu_gl_blob_destroy(dev->_vdev, &bo->_blob);
    }

    if (dev->ctx_initialized)
        uk_virtio_gpu_gl_context_destroy(dev->_vdev, &dev->_ctx);

    memset(dev, 0, sizeof(*dev));
}
