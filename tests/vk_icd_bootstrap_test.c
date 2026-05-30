/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * vk_icd_bootstrap_test — G6 Vulkan ICD substrate test for libukvk_icd.
 *
 * Exercises the Vulkan ICD bootstrap layer against the fake VirtIO-GPU
 * backend. Validates:
 *   - ICD init via uk_vulkan_icd_init() (calls G5 open + Venus context)
 *   - Device info retrieval: device name, capset id, venus_available
 *   - Venus capset detection (id=4, bit 4 of supported_capsets)
 *   - has_resource_blob, has_context_init flags
 *   - rendering_status == "blocked:ring-buffer-frame-proof-missing"
 *   - Idempotent get_device_info
 *   - Error handling: NULL icd, uninitialised icd
 *   - uk_vulkan_icd_close() zeroes state
 *
 * Build: see tests/Makefile (native target)
 * Run:   ./build/vk_icd_bootstrap_test
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <uk/vulkan_icd.h>
#include <uk/drm_virtgpu.h>
#include <drm/virtgpu_drm.h>
#include <uk/virtio_gpu.h>

#define PASS(label)   do { printf("  PASS  %s\n", label); } while (0)
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
#define CHECK_STR(label, got, want) do { \
    if (strcmp((got), (want)) != 0) { \
        printf("  FAIL  %s  got='%s' want='%s'\n", label, got, want); \
        return 1; \
    } \
    PASS(label); \
} while (0)

int main(void)
{
    struct uk_vulkan_icd_dev icd;
    struct uk_vulkan_icd_info info;
    int rc;

    printf("vk_icd_bootstrap_test: G6 Vulkan ICD substrate against fake VirtIO-GPU backend\n");

    /* ── Error handling: NULL icd ────────────────────────────────────────── */
    rc = uk_vulkan_icd_init(NULL, 0);
    if (rc != -EINVAL) FAIL("init:null_icd returns EINVAL", rc);
    PASS("init:null_icd returns EINVAL");

    /* ── Init ────────────────────────────────────────────────────────────── */
    CHECK("init", uk_vulkan_icd_init(&icd, 0));
    CHECK_NZ("init:icd_initialized", icd.icd_initialized);
    CHECK_NZ("init:drm._vdev non-null", icd.drm._vdev);

    /* ── Device info: device name ────────────────────────────────────────── */
    CHECK_STR("info:device_name", icd.info.device_name, "virtio-gpu-venus");

    /* ── Device info: capset ─────────────────────────────────────────────── */
    if (icd.info.capset_id != VIRTGPU_CAPSET_VENUS)
        FAIL("info:capset_id == VENUS(4)", (int)icd.info.capset_id);
    PASS("info:capset_id == VENUS(4)");

    /* ── Device info: Venus available ────────────────────────────────────── */
    CHECK_NZ("info:venus_available", icd.info.venus_available);

    /* ── Device info: supported_capsets has Venus bit ────────────────────── */
    CHECK_NZ("info:supported_capsets has_bit4",
             icd.info.supported_capsets & (1ull << VIRTGPU_CAPSET_VENUS));

    /* ── Device info: capability flags ──────────────────────────────────── */
    CHECK_NZ("info:has_resource_blob", icd.info.has_resource_blob);
    CHECK_NZ("info:has_context_init",  icd.info.has_context_init);

    /* ── Rendering status is explicitly blocked ──────────────────────────── */
    if (!icd.info.rendering_status ||
        strcmp(icd.info.rendering_status, "blocked:ring-buffer-frame-proof-missing") != 0) {
        printf("  FAIL  info:rendering_status  got='%s'\n",
               icd.info.rendering_status ? icd.info.rendering_status : "(null)");
        return 1;
    }
    PASS("info:rendering_status == blocked:ring-buffer-frame-proof-missing");

    /* ── get_device_info ─────────────────────────────────────────────────── */
    memset(&info, 0, sizeof(info));
    CHECK("get_device_info", uk_vulkan_icd_get_device_info(&icd, &info));
    CHECK_STR("get_device_info:device_name", info.device_name, "virtio-gpu-venus");
    if (info.capset_id != VIRTGPU_CAPSET_VENUS)
        FAIL("get_device_info:capset_id", (int)info.capset_id);
    PASS("get_device_info:capset_id");
    CHECK_NZ("get_device_info:venus_available", info.venus_available);

    /* ── get_device_info: idempotent ─────────────────────────────────────── */
    {
        struct uk_vulkan_icd_info info2;
        memset(&info2, 0, sizeof(info2));
        CHECK("get_device_info:idempotent", uk_vulkan_icd_get_device_info(&icd, &info2));
        if (info2.capset_id != info.capset_id ||
            info2.venus_available != info.venus_available)
            FAIL("get_device_info:idempotent:same_result", 0);
        PASS("get_device_info:idempotent:same_result");
    }

    /* ── Error: get_device_info on uninit'd icd ──────────────────────────── */
    {
        struct uk_vulkan_icd_dev icd2;
        memset(&icd2, 0, sizeof(icd2));
        rc = uk_vulkan_icd_get_device_info(&icd2, &info);
        if (rc != -EINVAL) FAIL("get_device_info:uninit returns EINVAL", rc);
        PASS("get_device_info:uninit returns EINVAL");
    }

    /* ── Error: get_device_info NULL args ────────────────────────────────── */
    rc = uk_vulkan_icd_get_device_info(NULL, &info);
    if (rc != -EINVAL) FAIL("get_device_info:null_icd returns EINVAL", rc);
    PASS("get_device_info:null_icd returns EINVAL");

    rc = uk_vulkan_icd_get_device_info(&icd, NULL);
    if (rc != -EINVAL) FAIL("get_device_info:null_info returns EINVAL", rc);
    PASS("get_device_info:null_info returns EINVAL");

    /* ── capset_name helper ──────────────────────────────────────────────── */
    {
        const char *name = uk_vulkan_icd_capset_name(VIRTGPU_CAPSET_VENUS);
        CHECK_NZ("capset_name:venus non-null", name);
        CHECK_NZ("capset_name:venus non-empty", name[0]);
    }

    /* ── Venus context initialized via G5 ───────────────────────────────── */
    CHECK_NZ("ctx_initialized via G5", icd.ctx_initialized);
    if (icd.drm.capset_id != VIRTGPU_CAPSET_VENUS)
        FAIL("drm:capset_id == VENUS", (int)icd.drm.capset_id);
    PASS("drm:capset_id == VENUS");

    /* ── close zeroes state ──────────────────────────────────────────────── */
    uk_vulkan_icd_close(&icd);
    if (icd.icd_initialized || icd.drm._vdev || icd.ctx_initialized)
        FAIL("close:zeroed_state", 0);
    PASS("close:zeroed_state");

    /* ── close on NULL is safe ───────────────────────────────────────────── */
    uk_vulkan_icd_close(NULL);
    PASS("close:null_safe");

    printf("vk_icd_bootstrap_test: all checks passed\n");
    return 0;
}
