/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * virtgpu_drm_ioctl_test — G5 ioctl replay test for libukvirtgpu_drm.
 *
 * Exercises the DRM_IOCTL_VIRTGPU_* shim against the fake VirtIO-GPU
 * backend.  Validates:
 *   - open / GETPARAM truth table
 *   - CONTEXT_INIT (capset_id = VIRTGPU_CAPSET_VENUS = 4)
 *   - RESOURCE_CREATE_BLOB (guest + host3d)
 *   - MAP (host-coherent pointer)
 *   - EXECBUFFER (Venus command stream)
 *   - WAIT (synchronisation placeholder)
 *   - Generic ioctl dispatcher (struct wire encoding)
 *   - close / resource teardown
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <uk/drm_virtgpu.h>
#include <drm/virtgpu_drm.h>
#include <uk/virtio_gpu.h>

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
    struct uk_drm_virtgpu_dev dev;
    uint64_t val = 0;
    uint32_t bo_handle = 0, res_handle = 0;
    uint64_t offset = 0;
    int rc;

    printf("virtgpu_drm_ioctl_test: G5 ioctl replay against fake VirtIO-GPU backend\n");

    /* ── open ──────────────────────────────────────────────────────────── */
    CHECK("open", uk_drm_virtgpu_open(&dev, 0));
    CHECK_NZ("open:vdev non-null", dev._vdev);

    /* ── GETPARAM ──────────────────────────────────────────────────────── */
    CHECK("getparam:3d_features",
          uk_drm_virtgpu_getparam(&dev, VIRTGPU_PARAM_3D_FEATURES, &val));
    CHECK_NZ("getparam:3d_features value", val);

    val = 0;
    CHECK("getparam:resource_blob",
          uk_drm_virtgpu_getparam(&dev, VIRTGPU_PARAM_RESOURCE_BLOB, &val));
    CHECK_NZ("getparam:resource_blob value", val);

    val = 0;
    CHECK("getparam:context_init",
          uk_drm_virtgpu_getparam(&dev, VIRTGPU_PARAM_CONTEXT_INIT, &val));
    CHECK_NZ("getparam:context_init value", val);

    val = 0;
    CHECK("getparam:supported_capset_ids",
          uk_drm_virtgpu_getparam(&dev, VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs, &val));
    /* Venus capset id=4 → bit 4 should be set */
    CHECK_NZ("getparam:supported_capset_ids has_venus", val & (1ull << UK_VIRTIO_GPU_CAPSET_VENUS));

    /* GETPARAM via ioctl dispatcher */
    {
        struct drm_virtgpu_getparam gp = { .param = VIRTGPU_PARAM_CAPSET_QUERY_FIX, .value = 0 };
        CHECK("ioctl:GETPARAM", uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_VIRTGPU_GETPARAM, &gp));
        CHECK_NZ("ioctl:GETPARAM value", gp.value);
    }

    /* Unknown param should return -EINVAL */
    rc = uk_drm_virtgpu_getparam(&dev, 0xFFFFFFFF, &val);
    if (rc != -EINVAL) FAIL("getparam:unknown_param returns EINVAL", rc);
    PASS("getparam:unknown_param returns EINVAL");

    /* ── CONTEXT_INIT ──────────────────────────────────────────────────── */
    CHECK("context_init:venus",
          uk_drm_virtgpu_context_init(&dev, VIRTGPU_CAPSET_VENUS, 1));
    CHECK_NZ("context_init:ctx_initialized", dev.ctx_initialized);
    if (dev.capset_id != VIRTGPU_CAPSET_VENUS)
        FAIL("context_init:capset_id", (int)dev.capset_id);
    PASS("context_init:capset_id");

    /* Second CONTEXT_INIT should fail with EEXIST */
    rc = uk_drm_virtgpu_context_init(&dev, VIRTGPU_CAPSET_VENUS, 1);
    if (rc != -EEXIST) FAIL("context_init:duplicate returns EEXIST", rc);
    PASS("context_init:duplicate returns EEXIST");

    /* CONTEXT_INIT via ioctl dispatcher */
    {
        /* Re-open for ioctl-path test so we get a fresh context slot */
        struct uk_drm_virtgpu_dev dev2;
        struct drm_virtgpu_context_set_param params[2] = {
            { .param = VIRTGPU_CONTEXT_PARAM_CAPSET_ID, .value = VIRTGPU_CAPSET_VENUS },
            { .param = VIRTGPU_CONTEXT_PARAM_NUM_RINGS, .value = 1 },
        };
        struct drm_virtgpu_context_init ci = {
            .num_params = 2,
            .ctx_set_params = (uint64_t)(uintptr_t)params,
        };
        CHECK("ioctl:dev2_open", uk_drm_virtgpu_open(&dev2, 0));
        CHECK("ioctl:CONTEXT_INIT", uk_drm_virtgpu_ioctl(&dev2, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &ci));
        CHECK_NZ("ioctl:CONTEXT_INIT:ctx_initialized", dev2.ctx_initialized);
        uk_drm_virtgpu_close(&dev2);
        PASS("ioctl:CONTEXT_INIT:close");
    }

    /* ── RESOURCE_CREATE_BLOB ──────────────────────────────────────────── */
    bo_handle = 0; res_handle = 0;
    CHECK("resource_create_blob:guest",
          uk_drm_virtgpu_resource_create_blob(&dev,
              VIRTGPU_BLOB_MEM_GUEST,
              VIRTGPU_BLOB_FLAG_USE_MAPPABLE,
              4096,
              &bo_handle, &res_handle));
    CHECK_NZ("resource_create_blob:guest:bo_handle", bo_handle);
    CHECK_NZ("resource_create_blob:guest:res_handle", res_handle);

    {
        uint32_t bo2 = 0, res2 = 0;
        CHECK("resource_create_blob:host3d",
              uk_drm_virtgpu_resource_create_blob(&dev,
                  VIRTGPU_BLOB_MEM_HOST3D,
                  VIRTGPU_BLOB_FLAG_USE_MAPPABLE | VIRTGPU_BLOB_FLAG_USE_SHAREABLE,
                  65536,
                  &bo2, &res2));
        CHECK_NZ("resource_create_blob:host3d:bo_handle", bo2);
    }

    /* Zero size should fail */
    {
        uint32_t bx = 0, rx = 0;
        rc = uk_drm_virtgpu_resource_create_blob(&dev,
                 VIRTGPU_BLOB_MEM_GUEST, VIRTGPU_BLOB_FLAG_USE_MAPPABLE,
                 0, &bx, &rx);
        if (rc != -EINVAL) FAIL("resource_create_blob:zero_size returns EINVAL", rc);
        PASS("resource_create_blob:zero_size returns EINVAL");
    }

    /* Via ioctl dispatcher */
    {
        struct drm_virtgpu_resource_create_blob rb = {
            .blob_mem   = VIRTGPU_BLOB_MEM_GUEST,
            .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE,
            .size       = 8192,
            .bo_handle  = 0,
            .res_handle = 0,
        };
        CHECK("ioctl:RESOURCE_CREATE_BLOB",
              uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &rb));
        CHECK_NZ("ioctl:RESOURCE_CREATE_BLOB:bo_handle", rb.bo_handle);
    }

    /* ── MAP ───────────────────────────────────────────────────────────── */
    offset = 0;
    CHECK("map:blob", uk_drm_virtgpu_map(&dev, bo_handle, &offset));
    CHECK_NZ("map:blob:offset non-zero", offset);

    /* Verify we get the same VA on second call (idempotent) */
    {
        uint64_t offset2 = 0;
        CHECK("map:blob:idempotent", uk_drm_virtgpu_map(&dev, bo_handle, &offset2));
        if (offset2 != offset) FAIL("map:blob:idempotent:same_addr", (int)(offset2 - offset));
        PASS("map:blob:idempotent:same_addr");
    }

    /* Write through the mapping */
    {
        uint8_t *ptr = (uint8_t *)(uintptr_t)offset;
        ptr[0] = 0xDE; ptr[1] = 0xAD;
        if (ptr[0] != 0xDE || ptr[1] != 0xAD) FAIL("map:write_readback", 0);
        PASS("map:write_readback");
    }

    /* MAP via ioctl */
    {
        struct drm_virtgpu_map m = { .handle = bo_handle, .offset = 0 };
        CHECK("ioctl:MAP", uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_VIRTGPU_MAP, &m));
        CHECK_NZ("ioctl:MAP:offset", m.offset);
    }

    /* ── EXECBUFFER ────────────────────────────────────────────────────── */
    {
        /* Minimal Venus command stream header (4 bytes cmd_id = 1 = bind) */
        uint8_t venus_cmd[8] = { 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 };
        CHECK("execbuffer", uk_drm_virtgpu_execbuffer(&dev, venus_cmd, sizeof(venus_cmd)));
    }

    /* EXECBUFFER via ioctl */
    {
        uint8_t cmd2[4] = { 0x02, 0x00, 0x00, 0x00 };
        struct drm_virtgpu_execbuffer eb = {
            .flags   = 0,
            .size    = sizeof(cmd2),
            .command = (uint64_t)(uintptr_t)cmd2,
        };
        CHECK("ioctl:EXECBUFFER",
              uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_VIRTGPU_EXECBUFFER, &eb));
    }

    /* EXECBUFFER without context should fail */
    {
        struct uk_drm_virtgpu_dev devx;
        CHECK("execbuffer:no_ctx_dev_open", uk_drm_virtgpu_open(&devx, 0));
        uint8_t cmd[4] = {0};
        rc = uk_drm_virtgpu_execbuffer(&devx, cmd, sizeof(cmd));
        if (rc != -EINVAL) FAIL("execbuffer:no_ctx returns EINVAL", rc);
        PASS("execbuffer:no_ctx returns EINVAL");
        uk_drm_virtgpu_close(&devx);
    }

    /* ── WAIT ──────────────────────────────────────────────────────────── */
    CHECK("wait", uk_drm_virtgpu_wait(&dev, bo_handle));

    /* WAIT via ioctl */
    {
        struct drm_virtgpu_3d_wait w = { .handle = bo_handle, .flags = 0 };
        CHECK("ioctl:WAIT", uk_drm_virtgpu_ioctl(&dev, DRM_IOCTL_VIRTGPU_WAIT, &w));
    }

    /* ── Unknown ioctl ─────────────────────────────────────────────────── */
    {
        struct drm_virtgpu_getparam gp = {0};
        rc = uk_drm_virtgpu_ioctl(&dev, 0xFFFFFFFF, &gp);
        if (rc != -ENOSYS) FAIL("ioctl:unknown returns ENOSYS", rc);
        PASS("ioctl:unknown returns ENOSYS");
    }

    /* ── close ─────────────────────────────────────────────────────────── */
    uk_drm_virtgpu_close(&dev);
    if (dev._vdev || dev.ctx_initialized) FAIL("close:zeroed_state", 0);
    PASS("close:zeroed_state");

    printf("virtgpu_drm_ioctl_test: all checks passed\n");
    return 0;
}
